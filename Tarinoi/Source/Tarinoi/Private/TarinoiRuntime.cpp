// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiRuntime.h"

#include "Algo/StableSort.h"
#include "Bindings/TarinoiBindings.h"
#include "Data/TarinoiDatabase.h"
#include "Data/TarinoiLayerFilter.h"
#include "Sync/TarinoiApiImporter.h"
#include "Sync/TarinoiCredentials.h"
#include "Sync/TarinoiSnapshot.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"
#include "TarinoiSettings.h"

const TCHAR* FTarinoiDialogueLine::SystemCardId = TEXT("__system__");

namespace
{
	/** Splits "pin>>target". Returns false for a connection without the separator. */
	bool SplitConnection(const FString& Connection, FString& OutPin, FString& OutTarget)
	{
		return Connection.Split(TEXT(">>"), &OutPin, &OutTarget, ESearchCase::CaseSensitive);
	}

	/** A card's authored data as display strings, for Blueprints. */
	TMap<FString, FString> DataStrings(const TSharedPtr<FJsonObject>& Data)
	{
		TMap<FString, FString> Out;
		if (!Data.IsValid())
		{
			return Out;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Data->Values)
		{
			const TSharedPtr<FJsonValue>& Value = Field.Value;
			const bool bStructured = Value.IsValid() && (Value->Type == EJson::Object || Value->Type == EJson::Array);
			Out.Add(Field.Key, bStructured ? TarinoiJson::Stringify(Value) : TarinoiJson::Str(Value));
		}
		return Out;
	}

	/** A card's vertical position in the authoring graph. Cards without one sort last. */
	double GeometryY(const TSharedPtr<FJsonObject>& Card)
	{
		const TSharedPtr<FJsonObject> Geo = TarinoiJson::Obj(Card, TEXT("geo"));
		const TSharedPtr<FJsonValue> Y = Geo.IsValid() ? Geo->TryGetField(TEXT("y")) : nullptr;
		return Y.IsValid() && Y->Type == EJson::Number ? Y->AsNumber() : TNumericLimits<double>::Max();
	}

	FString InputCondition(const TSharedPtr<FJsonObject>& Card)
	{
		// input_pin is stored as an explicit null when unused; Obj treats that as absent.
		return TarinoiJson::Str(TarinoiJson::Obj(Card, TEXT("input_pin")), TEXT("condition"));
	}
}

UTarinoiRuntime::UTarinoiRuntime()
{
	Bindings = CreateDefaultSubobject<UTarinoiBindings>(TEXT("Bindings"));
}

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

bool UTarinoiRuntime::Configure()
{
	return ConfigureWith(*GetDefault<UTarinoiSettings>());
}

bool UTarinoiRuntime::ConfigureWith(const UTarinoiSettings& Settings)
{
	Settings.ApplyLogLevel();

	ProjectId = Settings.GetProjectId();
	ApiPath = Settings.ApiPath;
	bOfflineMode = Settings.bOfflineMode;

	if (ProjectId.IsEmpty())
	{
		UE_LOG(LogTarinoi, Error, TEXT("Runtime: cannot work out which project to load. Set the API path in Project Settings > Plugins > Tarinoi."));
		return false;
	}

	// Offline mode plays the snapshot bundled at build time, copied somewhere writable first.
	if (bOfflineMode && !TarinoiSnapshot::Seed(ProjectId))
	{
		return false;
	}

	Database = FTarinoiDatabase::Acquire(ProjectId);
	if (!Database.IsValid())
	{
		return false;
	}
	Database->bCommittedOnly = Settings.bCommittedOnly;

	// The dispatcher holds the bindings by reference, so later bindings still take effect.
	Dispatcher = MakeUnique<FTarinoiDispatcher>(Bindings);

	LoadGlobalCache();

	if (!DocumentStore)
	{
		DocumentStore = CreateConfiguredStore(Settings);
	}
	DocumentStore->Setup(Database, this);
	return true;
}

UTarinoiDocumentStore* UTarinoiRuntime::CreateConfiguredStore(const UTarinoiSettings& Settings)
{
	if (!Settings.DocumentStoreClass.IsNull())
	{
		if (UClass* StoreClass = Settings.DocumentStoreClass.LoadSynchronous())
		{
			if (!StoreClass->HasAnyClassFlags(CLASS_Abstract))
			{
				return NewObject<UTarinoiDocumentStore>(this, StoreClass);
			}
		}

		UE_LOG(LogTarinoi, Error, TEXT("Runtime: could not use the document store class '%s'. Using the built-in store instead."),
			*Settings.DocumentStoreClass.ToString());
	}

	return NewObject<UTarinoiSqliteDocumentStore>(this);
}

void UTarinoiRuntime::Shutdown()
{
	if (ActiveImport.IsValid())
	{
		ActiveImport->Cancel();
		ActiveImport.Reset();
	}

	Database.Reset();
	Dispatcher.Reset();
	State = ETarinoiDialogueState::Idle;
	Choices.Reset();
	Visited.Reset();
}

bool UTarinoiRuntime::IsConfigured() const
{
	return Database.IsValid() && Database->IsOpen() && Dispatcher.IsValid();
}

bool UTarinoiRuntime::EnsureConfigured() const
{
	if (IsConfigured())
	{
		return true;
	}

	UE_LOG(LogTarinoi, Error, TEXT("Runtime: not configured. Call Configure first, or check the log for why it failed."));
	return false;
}

// -----------------------------------------------------------------------------
// Sync
// -----------------------------------------------------------------------------

void UTarinoiRuntime::Sync()
{
	if (!EnsureConfigured())
	{
		return;
	}

	if (bOfflineMode)
	{
		OnSyncCompleted.Broadcast(FTarinoiSyncStats());
		return;
	}

	if (ActiveImport.IsValid())
	{
		UE_LOG(LogTarinoi, Verbose, TEXT("Runtime: a sync is already running."));
		return;
	}

	OnSyncStarted.Broadcast();

	const TSharedRef<ITarinoiHttpTransport> Transport = HttpTransport.IsValid()
		? HttpTransport.ToSharedRef()
		: ITarinoiHttpTransport::CreateDefault();
	const TSharedRef<FTarinoiApiImporter> Importer = MakeShared<FTarinoiApiImporter>(Transport);
	ActiveImport = Importer;

	TWeakObjectPtr<UTarinoiRuntime> WeakThis(this);
	Importer->Start(ApiPath, FTarinoiCredentials::Read(FTarinoiCredentials::ApiKeyName), Database,
		FTarinoiApiImporter::FOnProgress::CreateLambda([WeakThis](const FString& Message, float Fraction)
		{
			if (UTarinoiRuntime* This = WeakThis.Get())
			{
				This->OnSyncProgress.Broadcast(Message, Fraction);
			}
		}),
		FTarinoiApiImporter::FOnComplete::CreateLambda([WeakThis](const FTarinoiSyncResult& Result)
		{
			UTarinoiRuntime* This = WeakThis.Get();
			if (!This)
			{
				return;
			}

			This->ActiveImport.Reset();
			if (!Result.bSuccess)
			{
				UE_LOG(LogTarinoi, Error, TEXT("Runtime: %s"), *Result.Error);
				This->OnSyncFailed.Broadcast(Result.Error);
				return;
			}

			UE_LOG(LogTarinoi, Log, TEXT("Runtime: sync complete: %s"), *Result.Stats.ToString());
			This->LoadGlobalCache();
			This->OnSyncCompleted.Broadcast(Result.Stats);
		}));
}

// -----------------------------------------------------------------------------
// Dialogue control
// -----------------------------------------------------------------------------

void UTarinoiRuntime::StartDialogue(const FString& CollectionId, const FString& CardId)
{
	if (!EnsureConfigured())
	{
		return;
	}

	State = ETarinoiDialogueState::Idle;
	CurrentCollectionId = CollectionId;
	Visited.Reset();
	SessionStartCardId = CardId;

	SessionSeen.Reset();
	if (HistoryStore.GetObject())
	{
		for (const FString& Seen : ITarinoiHistoryStore::Execute_GetVisited(HistoryStore.GetObject(), CardId))
		{
			SessionSeen.Add(Seen);
		}
	}

	LoadAndProcessCard(CollectionId, CardId);
}

void UTarinoiRuntime::Advance()
{
	if (State != ETarinoiDialogueState::NpcLine)
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: Advance ignored: nothing to advance past (state %s)."), *UEnum::GetValueAsString(State));
		return;
	}

	// A system line has no card of its own; it stashed where to go next.
	if (CurrentCardId == FTarinoiDialogueLine::SystemCardId)
	{
		const FString Target = PendingNavTarget;
		PendingNavTarget.Reset();
		State = ETarinoiDialogueState::Idle;
		LoadAndProcessCard(CurrentCollectionId, Target);
		return;
	}

	const TSharedPtr<FJsonObject> Card = CurrentCard;
	const FString CardId = CurrentCardId;
	const FString CollectionId = CurrentCollectionId;
	State = ETarinoiDialogueState::Idle;
	FollowConnections(Card, CardId, CollectionId);
}

void UTarinoiRuntime::SelectChoice(int32 Index)
{
	if (State != ETarinoiDialogueState::PcChoice)
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: SelectChoice ignored: no choices are open (state %s)."), *UEnum::GetValueAsString(State));
		return;
	}

	if (!Choices.IsValidIndex(Index))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: choice %d is out of range (0..%d)."), Index, Choices.Num() - 1);
		return;
	}

	const FTarinoiDialogueChoice Chosen = Choices[Index];
	State = ETarinoiDialogueState::Idle;
	Choices.Reset();
	SessionSeen.Add(Chosen.CardId);

	// Functions run only for the option actually taken: running the others would fire their
	// side effects for lines the player never saw.
	EvalCardFunctions(Chosen.Card, Chosen.CardId);

	OnChoiceMade.Broadcast(MakeLine(Chosen.Card, Chosen.CardId, Chosen.CollectionId));
	FollowConnections(Chosen.Card, Chosen.CardId, Chosen.CollectionId);
}

void UTarinoiRuntime::SelectPin(const FString& PinName)
{
	if (State != ETarinoiDialogueState::AwaitingPin)
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: SelectPin ignored: not waiting for a pin (state %s)."), *UEnum::GetValueAsString(State));
		return;
	}

	FString Target;
	if (const TArray<TSharedPtr<FJsonValue>>* Connections = TarinoiJson::Arr(CurrentCard, TEXT("connections")))
	{
		for (const TSharedPtr<FJsonValue>& Connection : *Connections)
		{
			FString Pin, To;
			if (SplitConnection(TarinoiJson::Str(Connection), Pin, To) && Pin.Equals(PinName, ESearchCase::CaseSensitive))
			{
				Target = To;
				break;
			}
		}
	}

	if (Target.IsEmpty())
	{
		RaiseError(FString::Printf(TEXT("Card '%s' has no pin named '%s'."), *CurrentCardId, *PinName));
		return;
	}

	State = ETarinoiDialogueState::Idle;
	LoadAndProcessCard(CurrentCollectionId, Target);
}

void UTarinoiRuntime::AbortDialogue()
{
	Choices.Reset();
	Visited.Reset();
	FinishDialogue();
}

TArray<FTarinoiStartCard> UTarinoiRuntime::GetStartCards()
{
	TArray<FTarinoiStartCard> Cards;
	if (!EnsureConfigured())
	{
		return Cards;
	}

	for (const FTarinoiStartCardRow& Row : DocumentStore->QueryStartCards())
	{
		FTarinoiStartCard& Card = Cards.AddDefaulted_GetRef();
		Card.CardId = Row.DocumentId;
		Card.CollectionId = Row.CollectionId;
		Card.CollectionLabel = CollectionLabel(Row.CollectionId);
		Card.Label = FString::Printf(TEXT("%s <%s>"), Row.Label.IsEmpty() ? TEXT("Start") : *Row.Label, *Row.DocumentId);
	}

	// Stable, so entry points within a collection keep query order.
	Algo::StableSort(Cards, [](const FTarinoiStartCard& A, const FTarinoiStartCard& B)
	{
		return A.CollectionLabel.Compare(B.CollectionLabel, ESearchCase::CaseSensitive) < 0;
	});
	return Cards;
}

FTarinoiValue UTarinoiRuntime::EvalExpression(const FString& Expression)
{
	return Dispatcher.IsValid() ? Dispatcher->EvalValue(Expression) : FTarinoiValue::None();
}

// -----------------------------------------------------------------------------
// Traversal
// -----------------------------------------------------------------------------

void UTarinoiRuntime::RaiseError(const FString& Message)
{
	UE_LOG(LogTarinoi, Error, TEXT("Runtime: %s"), *Message);
	OnDialogueError.Broadcast(Message);
}

void UTarinoiRuntime::LoadAndProcessCard(const FString& CollectionId, const FString& CardId)
{
	if (CheckLoop(CardId))
	{
		return;
	}

	const TSharedPtr<FJsonObject> Card = DocumentStore->LoadCard(CollectionId, CardId);
	if (!Card.IsValid())
	{
		RaiseError(FString::Printf(TEXT("Card '%s' was not found in collection '%s'."), *CardId, *CollectionId));
		return;
	}

	ProcessCard(Card, CardId, CollectionId);
}

void UTarinoiRuntime::ProcessCard(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId)
{
	const FString Condition = InputCondition(Card);
	if (!Condition.IsEmpty() && !EvalGuarded(Condition, FString::Printf(TEXT("input_pin on card '%s'"), *CardId)))
	{
		// The card refused entry: walk on from it rather than stopping.
		FollowConnections(Card, CardId, CollectionId);
		return;
	}

	const FString BaseRef = TarinoiJson::Str(Card, TEXT("base_ref"));

	// Line cards defer their functions: an NPC line fires them when shown, a PC line when chosen.
	// Firing them here would trigger side effects for options the player never picks.
	if (BaseRef != TEXT("line"))
	{
		EvalCardFunctions(Card, CardId);
	}

	if (BaseRef == TEXT("line"))
	{
		ProcessLine(Card, CardId, CollectionId);
	}
	else if (BaseRef == TEXT("jump"))
	{
		FollowJump(Card, CardId);
	}
	else if (BaseRef == TEXT("start") || BaseRef == TEXT("blank"))
	{
		FollowConnections(Card, CardId, CollectionId);
	}
	else
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' has an unrecognised type '%s'; passing through it."), *CardId, *BaseRef);
		FollowConnections(Card, CardId, CollectionId);
	}
}

void UTarinoiRuntime::ProcessLine(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId)
{
	// A spent shown_once card is not a valid continuation. Reached on its own it is the same dead
	// end as a card whose entry conditions all failed, so the dialogue ends. Its functions do not
	// run: nobody saw it.
	if (IsSpentShownOnce(Card, CardId))
	{
		UE_LOG(LogTarinoi, Error,
			TEXT("Runtime: card '%s' in '%s' is shown_once and has already been seen, and nothing else continues from here; ending the dialogue."),
			*CardId, *CollectionId);
		FinishDialogue();
		return;
	}

	if (IsPcCard(Card))
	{
		// A player line reached on its own is still a choice: of one.
		Choices = {MakeChoice(Card, CardId, CollectionId, 0)};
		State = ETarinoiDialogueState::PcChoice;
		Visited.Reset();
		OnChoicesReady.Broadcast(Choices);
		return;
	}

	EvalCardFunctions(Card, CardId);

	// An NPC line counts as seen the moment it is displayed; a PC line once it is chosen.
	SessionSeen.Add(CardId);

	State = ETarinoiDialogueState::NpcLine;
	CurrentCard = Card;
	CurrentCardId = CardId;
	CurrentCollectionId = CollectionId;
	Visited.Reset();
	OnLineReady.Broadcast(MakeLine(Card, CardId, CollectionId));
}

void UTarinoiRuntime::FollowConnections(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId)
{
	if (Dispatcher.IsValid())
	{
		Dispatcher->SetContextCard(Card);
	}

	const TArray<TSharedPtr<FJsonValue>>* Connections = TarinoiJson::Arr(Card, TEXT("connections"));
	if (!Connections || Connections->Num() == 0)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Runtime: card '%s' in '%s' leads nowhere; ending the dialogue. Connect it to another card or to flow:end."),
			*CardId, *CollectionId);
		FinishDialogue();
		return;
	}

	TArray<FString> DefaultTargets;
	TArray<TPair<FString, FString>> NamedPins;

	for (const TSharedPtr<FJsonValue>& Connection : *Connections)
	{
		FString Pin, Target;
		if (!SplitConnection(TarinoiJson::Str(Connection), Pin, Target))
		{
			continue;
		}

		if (Target == TEXT("flow:end"))
		{
			FinishDialogue();
			return;
		}

		if (Pin == TEXT("default"))
		{
			if (!DefaultTargets.ContainsByPredicate([&Target](const FString& T) { return T.Equals(Target, ESearchCase::CaseSensitive); }))
			{
				DefaultTargets.Add(Target);
			}
		}
		else if (!NamedPins.ContainsByPredicate([&Pin](const TPair<FString, FString>& P) { return P.Key.Equals(Pin, ESearchCase::CaseSensitive); }))
		{
			// First wins: a duplicate pin name is an authoring mistake, and picking the first
			// keeps behaviour predictable.
			NamedPins.Emplace(Pin, Target);
		}
	}

	if (NamedPins.Num() > 0)
	{
		FollowNamedPins(Card, CardId, CollectionId, NamedPins);
		return;
	}

	if (DefaultTargets.Num() == 0)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Runtime: card '%s' in '%s' has connections but none name a target; ending the dialogue."),
			*CardId, *CollectionId);
		FinishDialogue();
		return;
	}

	if (DefaultTargets.Num() == 1)
	{
		LoadAndProcessCard(CollectionId, DefaultTargets[0]);
		return;
	}

	BuildChoicesFromTargets(DefaultTargets, CollectionId, CardId);
}

void UTarinoiRuntime::FollowNamedPins(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId,
	const TArray<TPair<FString, FString>>& NamedPins)
{
	const FString Selector = TarinoiJson::Str(Card, TEXT("output_selector"));

	if (!Selector.IsEmpty() && Selector.Contains(TEXT("$")))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' has an unfilled template in its output selector '%s'; asking for the pin instead."),
			*CardId, *Selector);
	}
	else if (!Selector.IsEmpty() && Dispatcher.IsValid() && Dispatcher->HasCall(Selector))
	{
		const FString PinName = Dispatcher->EvalCall(Selector).ToString();
		UE_LOG(LogTarinoi, Verbose, TEXT("output_selector '%s' [card:%s] -> '%s'"), *Selector, *CardId, *PinName);

		const TPair<FString, FString>* Match = NamedPins.FindByPredicate([&PinName](const TPair<FString, FString>& P)
		{
			return P.Key.Equals(PinName, ESearchCase::CaseSensitive);
		});

		if (!Match || Match->Value.IsEmpty())
		{
			// Stalls deliberately rather than guessing: the selector and the authored pins
			// disagree, and picking one would hide the mistake.
			PendingSystemLines.Reset();
			TArray<FString> Available;
			for (const TPair<FString, FString>& Pin : NamedPins)
			{
				Available.Add(Pin.Key);
			}
			RaiseError(FString::Printf(TEXT("Card '%s' has no pin '%s', which its output selector returned. Pins available: %s."),
				*CardId, *PinName, *FString::Join(Available, TEXT(", "))));
			return;
		}

		if (PendingSystemLines.Num() > 0)
		{
			ShowSystemLine(CollectionId, Match->Value);
			return;
		}

		LoadAndProcessCard(CollectionId, Match->Value);
		return;
	}
	else if (!Selector.IsEmpty())
	{
		UE_LOG(LogTarinoi, Error, TEXT("Runtime: card '%s' uses the output selector '%s', which is not bound; asking for the pin instead."),
			*CardId, *Selector);
	}

	CurrentCard = Card;
	CurrentCardId = CardId;
	CurrentCollectionId = CollectionId;
	State = ETarinoiDialogueState::AwaitingPin;

	TArray<FString> Pins;
	for (const TPair<FString, FString>& Pin : NamedPins)
	{
		Pins.Add(Pin.Key);
	}
	OnPinChoiceNeeded.Broadcast(Pins);
}

void UTarinoiRuntime::ShowSystemLine(const FString& CollectionId, const FString& NavTarget)
{
	const FString Message = PendingSystemLines[0];
	PendingSystemLines.Reset();
	PendingNavTarget = NavTarget;

	CurrentCard = MakeShared<FJsonObject>();
	CurrentCardId = FTarinoiDialogueLine::SystemCardId;
	CurrentCollectionId = CollectionId;
	State = ETarinoiDialogueState::NpcLine;

	FTarinoiDialogueLine Line;
	Line.CardId = FTarinoiDialogueLine::SystemCardId;
	Line.CollectionId = CollectionId;
	Line.EntityRef = TEXT("system");
	Line.LineMode = TEXT("system");
	Line.Line = Message;
	Line.bIsSystem = true;
	Line.DataJson = MakeShared<FJsonObject>();
	OnLineReady.Broadcast(Line);
}

void UTarinoiRuntime::BuildChoicesFromTargets(const TArray<FString>& TargetIds, const FString& CollectionId, const FString& SourceCardId)
{
	TArray<FTarinoiDialogueChoice> Candidates;
	int32 LineCandidates = 0;
	int32 ShownOnceFiltered = 0;

	for (const FString& TargetId : TargetIds)
	{
		const TSharedPtr<FJsonObject> Card = DocumentStore->LoadCard(CollectionId, TargetId);
		if (!Card.IsValid())
		{
			continue;
		}

		if (TarinoiJson::Str(Card, TEXT("base_ref")) != TEXT("line"))
		{
			UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' is not a line, so it cannot be offered as a choice; skipping it."), *TargetId);
			continue;
		}

		++LineCandidates;

		// Checked before the condition, so a spent option costs nothing to evaluate.
		if (IsSpentShownOnce(Card, TargetId))
		{
			UE_LOG(LogTarinoi, Verbose, TEXT("Runtime: card '%s' is shown_once and already seen; leaving it out."), *TargetId);
			++ShownOnceFiltered;
			continue;
		}

		const FString Condition = InputCondition(Card);
		if (!Condition.IsEmpty() && !EvalGuarded(Condition, FString::Printf(TEXT("input_pin on card '%s'"), *TargetId)))
		{
			continue;
		}

		Candidates.Add(MakeChoice(Card, TargetId, CollectionId, Candidates.Num()));
	}

	if (Candidates.Num() == 0)
	{
		// Every way of running out of continuations ends the same way; only the message differs.
		if (ShownOnceFiltered > 0)
		{
			UE_LOG(LogTarinoi, Error,
				TEXT("Runtime: nothing follows card '%s': of %d possible continuation(s), %d were shown_once cards already seen and the rest were ruled out by their conditions. Ending the dialogue. Give the card a fallback option without shown_once to keep it reachable."),
				*SourceCardId, LineCandidates, ShownOnceFiltered);
		}
		else if (LineCandidates > 0)
		{
			UE_LOG(LogTarinoi, Error,
				TEXT("Runtime: nothing follows card '%s': all %d possible continuation(s) were ruled out by their conditions. Ending the dialogue."),
				*SourceCardId, LineCandidates);
		}

		FinishDialogue();
		return;
	}

	// Authors express reading order by laying cards out top to bottom, so connection order is an
	// implementation detail. Stable, so equal positions keep their original order.
	Algo::StableSort(Candidates, [](const FTarinoiDialogueChoice& A, const FTarinoiDialogueChoice& B)
	{
		return GeometryY(A.Card) < GeometryY(B.Card);
	});

	Choices = SelectByKind(MoveTemp(Candidates), SourceCardId);

	if (Choices.Num() == 1)
	{
		// Only one option survived, so there is nothing to choose.
		const FString Only = Choices[0].CardId;
		Choices.Reset();
		LoadAndProcessCard(CollectionId, Only);
		return;
	}

	State = ETarinoiDialogueState::PcChoice;
	Visited.Reset();
	OnChoicesReady.Broadcast(Choices);
}

TArray<FTarinoiDialogueChoice> UTarinoiRuntime::SelectByKind(TArray<FTarinoiDialogueChoice> Sorted, const FString& SourceCardId) const
{
	// As in-app playback: any player line makes this a choice set, whose player lines are offered
	// and whose non-player lines are an authoring error and dropped. Otherwise it is a non-player
	// set, and only the first line whose condition passed is shown: the conditions are how the
	// author picks the line, not a menu.
	TArray<FTarinoiDialogueChoice> Pc, Npc;
	for (FTarinoiDialogueChoice& Choice : Sorted)
	{
		(IsPcCard(Choice.Card) ? Pc : Npc).Add(MoveTemp(Choice));
	}

	TArray<FTarinoiDialogueChoice> Kept;
	if (Pc.Num() > 0)
	{
		if (Npc.Num() > 0)
		{
			UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' leads to both player and non-player lines; the %d non-player line(s) are dropped."),
				*SourceCardId, Npc.Num());
		}
		Kept = MoveTemp(Pc);
	}
	else
	{
		if (Npc.Num() > 1)
		{
			// A repeated (or missing) condition means the later lines can never be reached.
			FTarinoiStringSet Seen;
			for (const FTarinoiDialogueChoice& Choice : Npc)
			{
				bool bAlreadySeen = false;
				Seen.Add(InputCondition(Choice.Card), &bAlreadySeen);
				if (bAlreadySeen)
				{
					UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' leads to several lines sharing the same condition; only the first can be reached."),
						*SourceCardId);
					break;
				}
			}

			UE_LOG(LogTarinoi, Verbose, TEXT("Runtime: %d non-player lines pass from card '%s'; showing the first, '%s'."),
				Npc.Num(), *SourceCardId, *Npc[0].CardId);
		}
		Kept.Add(MoveTemp(Npc[0]));
	}

	for (int32 Index = 0; Index < Kept.Num(); ++Index)
	{
		Kept[Index].Index = Index;
	}
	return Kept;
}

void UTarinoiRuntime::FollowJump(const TSharedPtr<FJsonObject>& Card, const FString& CardId)
{
	// A jump's destination is its one card-link property, data.target: the target card's bare
	// document id, with no collection, possibly on another board.
	const FString Target = TarinoiJson::Str(TarinoiJson::Obj(Card, TEXT("data")), TEXT("target"));
	if (Target.IsEmpty())
	{
		RaiseError(FString::Printf(TEXT("Jump card '%s' does not say where to jump to."), *CardId));
		return;
	}

	if (CheckLoop(Target))
	{
		return;
	}

	FString TargetCollection;
	TSharedPtr<FJsonObject> TargetCard;
	if (!DocumentStore->LocateCard(Target, TargetCollection, TargetCard))
	{
		RaiseError(FString::Printf(TEXT("Jump card '%s' points at a card that does not exist: '%s'."), *CardId, *Target));
		return;
	}

	CurrentCollectionId = TargetCollection;
	ProcessCard(TargetCard, Target, TargetCollection);
}

bool UTarinoiRuntime::CheckLoop(const FString& CardId)
{
	// Cleared whenever the dialogue stops for the player, so returning to a card in a later turn is
	// fine; only going round without ever reaching the player is a problem.
	bool bAlreadyVisited = false;
	Visited.Add(CardId, &bAlreadyVisited);
	if (!bAlreadyVisited)
	{
		return false;
	}

	RaiseError(FString::Printf(TEXT("The dialogue loops back to card '%s' without ever stopping for the player. Ending it here."), *CardId));
	return true;
}

void UTarinoiRuntime::FinishDialogue()
{
	State = ETarinoiDialogueState::Idle;

	if (HistoryStore.GetObject() && !SessionStartCardId.IsEmpty())
	{
		ITarinoiHistoryStore::Execute_SaveVisited(HistoryStore.GetObject(), SessionStartCardId, SessionSeen.Array());
	}

	OnDialogueEnded.Broadcast();
}

// -----------------------------------------------------------------------------
// Card functions and conditions
// -----------------------------------------------------------------------------

void UTarinoiRuntime::EvalCardFunctions(const TSharedPtr<FJsonObject>& Card, const FString& CardId)
{
	const TSharedPtr<FJsonObject> Data = TarinoiJson::Obj(Card, TEXT("data"));
	if (!Dispatcher.IsValid() || !Data.IsValid())
	{
		return;
	}

	// Order follows the card's props, since these calls have side effects that authors sequence
	// deliberately. Expressions props does not list run last: props can lag behind a template
	// change, so it is an ordering hint rather than an inventory.
	TTarinoiMap<int32> Order;
	if (const TArray<TSharedPtr<FJsonValue>>* Props = TarinoiJson::Arr(Card, TEXT("props")))
	{
		for (int32 Index = 0; Index < Props->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Prop = (*Props)[Index];
			if (Prop.IsValid() && Prop->Type == EJson::Object)
			{
				Order.Add(TarinoiJson::Str(Prop->AsObject(), TEXT("name")), Index);
			}
		}
	}

	struct FCall
	{
		FString Name;
		FString Expression;
		int32 Order;
	};

	TArray<FCall> Calls;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Data->Values)
	{
		if (Field.Value.IsValid() && Field.Value->Type == EJson::String && Field.Value->AsString().StartsWith(TEXT("Fn."), ESearchCase::CaseSensitive))
		{
			const int32* Position = Order.Find(Field.Key);
			Calls.Add({Field.Key, Field.Value->AsString(), Position ? *Position : MAX_int32});
		}
	}

	Algo::StableSort(Calls, [](const FCall& A, const FCall& B) { return A.Order < B.Order; });

	for (const FCall& Call : Calls)
	{
		if (Call.Expression.Contains(TEXT("$")))
		{
			UE_LOG(LogTarinoi, Warning, TEXT("Runtime: card '%s' has an unfilled template in '%s' (%s); skipping it."),
				*CardId, *Call.Name, *Call.Expression);
		}
		else if (Dispatcher->HasCall(Call.Expression))
		{
			Dispatcher->EvalCall(Call.Expression);
		}
		else
		{
			UE_LOG(LogTarinoi, Error, TEXT("Runtime: card '%s' calls '%s' in '%s', which is not bound. Regenerate your bindings."),
				*CardId, *Call.Expression, *Call.Name);
		}
	}
}

bool UTarinoiRuntime::EvalGuarded(const FString& Condition, const FString& Where)
{
	// A '$' means a template was never filled in. Failing it would silently hide content while
	// the author is still working, so it passes with a warning instead.
	if (Condition.Contains(TEXT("$")))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Runtime: unfilled template in the condition on %s (%s); treating it as met."), *Where, *Condition);
		return true;
	}

	return Dispatcher.IsValid() && Dispatcher->EvalCondition(Condition);
}

// -----------------------------------------------------------------------------
// Caches
// -----------------------------------------------------------------------------

void UTarinoiRuntime::LoadGlobalCache()
{
	if (!Database.IsValid() || !Database->IsOpen())
	{
		return;
	}

	Collections.Reset();
	CollectionLabels.Reset();
	CollectionIdentifiers.Reset();
	Entities.Reset();
	Lists.Reset();

	LoadCollections();
	LoadEntities();
	LoadLists();

	if (Dispatcher.IsValid())
	{
		Dispatcher->SetLists(Lists);
	}
}

void UTarinoiRuntime::LoadCollections()
{
	TArray<FTarinoiDocumentRow> Rows;
	Database->Query(TEXT("SELECT * FROM documents WHERE document_type = 'collection-manifest'"), {},
		[&Rows](const FTarinoiSqlRow& Row) { Rows.Add(FTarinoiDocumentRow::FromSql(Row)); });

	for (const FTarinoiDocumentRow& Row : TarinoiLayerFilter::Merge(Rows, Database->bCommittedOnly))
	{
		const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Row.Payload);
		if (!Payload.IsValid())
		{
			continue;
		}

		Collections.Add(Row.DocumentId, Payload);
		CollectionLabels.Add(Row.DocumentId, TarinoiJson::Str(Payload, TEXT("label")));
		CollectionIdentifiers.Add(Row.DocumentId, Row.Identifier);
	}

	// Fall back to the collections table, which the importer rebuilds and which survives even
	// when the manifest documents themselves are absent.
	if (Collections.Num() > 0)
	{
		return;
	}

	Database->Query(TEXT("SELECT * FROM collections"), {}, [this](const FTarinoiSqlRow& Row)
	{
		const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Row.GetString(TEXT("payload")));
		if (!Payload.IsValid())
		{
			return;
		}

		const FString Id = Row.GetString(TEXT("collection_id"));
		const FString Name = Row.GetString(TEXT("collection_name"));
		Collections.Add(Id, Payload);
		CollectionLabels.Add(Id, Name.IsEmpty() ? TarinoiJson::Str(Payload, TEXT("label")) : Name);
		CollectionIdentifiers.Add(Id, Name);
	});
}

void UTarinoiRuntime::LoadEntities()
{
	TArray<FTarinoiDocumentRow> Rows;
	Database->Query(TEXT("SELECT * FROM documents WHERE document_type = 'entity'"), {},
		[&Rows](const FTarinoiSqlRow& Row) { Rows.Add(FTarinoiDocumentRow::FromSql(Row)); });

	for (const FTarinoiDocumentRow& Row : TarinoiLayerFilter::Merge(Rows, Database->bCommittedOnly))
	{
		const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Row.Payload);
		if (Payload.IsValid() && !Row.Identifier.IsEmpty())
		{
			Entities.Add(Row.Identifier, Payload);
		}
	}
}

void UTarinoiRuntime::LoadLists()
{
	// The Ls.* key is the collection's own name, not its document id. Older content carries it at
	// payload.collection_name; current content has it only as the manifest document's identifier,
	// which the payload never includes.
	TTarinoiMap<FString> ListCollections;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : Collections)
	{
		if (TarinoiJson::Str(Entry.Value, TEXT("collection_type")) != TEXT("list-collection"))
		{
			continue;
		}

		FString Name = TarinoiJson::Str(Entry.Value, TEXT("collection_name"));
		if (Name.IsEmpty())
		{
			Name = CollectionIdentifiers.FindRef(Entry.Key);
		}

		if (!Name.IsEmpty())
		{
			ListCollections.Add(Entry.Key, Name);
		}
	}

	if (ListCollections.Num() == 0)
	{
		return;
	}

	TArray<FString> Placeholders;
	FTarinoiSqlArgs Args;
	for (const TPair<FString, FString>& Entry : ListCollections)
	{
		Placeholders.Add(TEXT("?"));
		Args.Add(Entry.Key);
	}

	TArray<FTarinoiDocumentRow> Rows;
	Database->Query(*FString::Printf(TEXT("SELECT * FROM documents WHERE collection_id IN (%s)"), *FString::Join(Placeholders, TEXT(","))),
		Args, [&Rows](const FTarinoiSqlRow& Row) { Rows.Add(FTarinoiDocumentRow::FromSql(Row)); });

	for (const FTarinoiDocumentRow& Row : TarinoiLayerFilter::Merge(Rows, Database->bCommittedOnly))
	{
		const FString* CollectionName = ListCollections.Find(Row.CollectionId);
		const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Row.Payload);
		if (!CollectionName || !Payload.IsValid() || Row.Identifier.IsEmpty())
		{
			continue;
		}

		// list_options is current; options is the older name.
		const TArray<TSharedPtr<FJsonValue>>* Options = TarinoiJson::Arr(Payload, TEXT("list_options"));
		if (!Options)
		{
			Options = TarinoiJson::Arr(Payload, TEXT("options"));
		}
		if (!Options)
		{
			continue;
		}

		TArray<TSharedPtr<FJsonObject>>& Items = Lists.Add(*CollectionName + TEXT("/") + Row.Identifier);
		for (const TSharedPtr<FJsonValue>& Option : *Options)
		{
			if (Option.IsValid() && Option->Type == EJson::Object)
			{
				Items.Add(Option->AsObject());
			}
		}
	}
}

TSharedPtr<FJsonObject> UTarinoiRuntime::GetEntityPayload(const FString& Identifier) const
{
	return Entities.FindRef(Identifier);
}

FString UTarinoiRuntime::CollectionLabel(const FString& CollectionId) const
{
	const FString* Label = CollectionLabels.Find(CollectionId);
	return Label && !Label->IsEmpty() ? *Label : CollectionId;
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool UTarinoiRuntime::IsPcCard(const TSharedPtr<FJsonObject>& Card) const
{
	const FString Mode = TarinoiJson::Str(Card, TEXT("line_mode"));
	if (Mode == TEXT("pc"))
	{
		return true;
	}
	if (Mode == TEXT("npc"))
	{
		return false;
	}

	// "inherit", or unset: the speaking entity decides.
	return TarinoiJson::Flag(GetEntityPayload(TarinoiJson::Str(Card, TEXT("entity_ref"))), TEXT("is_player_character"));
}

bool UTarinoiRuntime::IsSpentShownOnce(const TSharedPtr<FJsonObject>& Card, const FString& CardId) const
{
	return TarinoiJson::Flag(Card, TEXT("shown_once")) && SessionSeen.Contains(CardId);
}

FTarinoiDialogueChoice UTarinoiRuntime::MakeChoice(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId, int32 Index) const
{
	const TSharedPtr<FJsonObject> Data = TarinoiJson::Obj(Card, TEXT("data"));

	FTarinoiDialogueChoice Choice;
	Choice.Index = Index;
	Choice.CardId = CardId;
	Choice.CollectionId = CollectionId;
	Choice.EntityRef = TarinoiJson::Str(Card, TEXT("entity_ref"));
	Choice.LineMode = TarinoiJson::Str(Card, TEXT("line_mode"));
	Choice.Line = TarinoiJson::Str(Data, TEXT("line"));
	Choice.Data = DataStrings(Data);
	Choice.bVisited = SessionSeen.Contains(CardId);
	Choice.Card = Card;
	return Choice;
}

FTarinoiDialogueLine UTarinoiRuntime::MakeLine(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId) const
{
	const FString EntityRef = TarinoiJson::Str(Card, TEXT("entity_ref"));
	const FString EntityLabel = TarinoiJson::Str(GetEntityPayload(EntityRef), TEXT("label"));
	const FString Mode = TarinoiJson::Str(Card, TEXT("line_mode"));
	const TSharedPtr<FJsonObject> Data = TarinoiJson::Obj(Card, TEXT("data"));

	FTarinoiDialogueLine Line;
	Line.CardId = CardId;
	Line.CollectionId = CollectionId;
	Line.EntityRef = EntityRef;
	Line.EntityLabel = EntityLabel.IsEmpty() ? EntityRef : EntityLabel;
	Line.LineMode = Mode.IsEmpty() ? FString(TEXT("inherit")) : Mode;
	Line.Line = TarinoiJson::Str(Data, TEXT("line"));
	Line.BaseRef = TarinoiJson::Str(Card, TEXT("base_ref"));
	Line.TemplateRef = TarinoiJson::Str(Card, TEXT("template_ref"));
	Line.Data = DataStrings(Data);
	Line.DataJson = Data.IsValid() ? Data : MakeShared<FJsonObject>();
	return Line;
}
