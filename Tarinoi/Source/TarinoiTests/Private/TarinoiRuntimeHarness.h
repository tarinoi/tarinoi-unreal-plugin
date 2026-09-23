// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Data/TarinoiDatabase.h"
#include "Data/TarinoiDocumentStore.h"
#include "Data/TarinoiLayerFilter.h"
#include "HAL/FileManager.h"
#include "Sync/TarinoiHttpTransport.h"
#include "TarinoiJson.h"
#include "TarinoiRuntime.h"
#include "TarinoiSettings.h"
#include "TarinoiTestBindings.h"
#include "UObject/StrongObjectPtr.h"

#include "TarinoiRuntimeHarness.generated.h"

/** Serves cards from memory and records what was asked for. */
UCLASS(NotBlueprintable)
class UTarinoiFakeDocumentStore : public UTarinoiDocumentStore
{
	GENERATED_BODY()

public:
	TTarinoiMap<TSharedPtr<FJsonObject>> Cards;
	TArray<FString> LoadedCardIds;
	TArray<FTarinoiStartCardRow> StartCards;

	void Add(const FString& CardId, const TSharedPtr<FJsonObject>& Card, const FString& CollectionId = TEXT("col1"))
	{
		Cards.Add(CollectionId + TEXT("/") + CardId, Card);
	}

	virtual TSharedPtr<FJsonObject> LoadCard(const FString& CollectionId, const FString& CardId) override
	{
		LoadedCardIds.Add(CardId);
		return Cards.FindRef(CollectionId + TEXT("/") + CardId);
	}

	virtual TSharedPtr<FJsonObject> GetDocument(const FString& DocumentId, const FString& CollectionId) override
	{
		return LoadCard(CollectionId.IsEmpty() ? FString(TEXT("col1")) : CollectionId, DocumentId);
	}

	virtual bool LocateCard(const FString& CardId, FString& OutCollectionId, TSharedPtr<FJsonObject>& OutCard) override
	{
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : Cards)
		{
			FString Collection, Id;
			Entry.Key.Split(TEXT("/"), &Collection, &Id);
			if (Id.Equals(CardId, ESearchCase::CaseSensitive))
			{
				LoadedCardIds.Add(CardId);
				OutCollectionId = Collection;
				OutCard = Entry.Value;
				return true;
			}
		}
		return false;
	}

	virtual TArray<FTarinoiStartCardRow> QueryStartCards() override { return StartCards; }
};

/** Records every runtime event. Dynamic delegates need UFUNCTION handlers, hence a UCLASS. */
UCLASS()
class UTarinoiRuntimeRecorder : public UObject
{
	GENERATED_BODY()

public:
	TArray<FTarinoiDialogueLine> Lines;
	TArray<TArray<FTarinoiDialogueChoice>> ChoiceSets;
	TArray<FTarinoiDialogueLine> ChoicesMade;
	TArray<FString> Errors;
	TArray<TArray<FString>> PinRequests;
	int32 EndedCount = 0;
	int32 SyncStartedCount = 0;
	TArray<FTarinoiSyncStats> SyncsCompleted;
	TArray<FString> SyncsFailed;

	void Listen(UTarinoiRuntime* Runtime)
	{
		Runtime->OnLineReady.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleLine);
		Runtime->OnChoicesReady.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleChoices);
		Runtime->OnChoiceMade.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleChoiceMade);
		Runtime->OnDialogueError.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleError);
		Runtime->OnPinChoiceNeeded.AddDynamic(this, &UTarinoiRuntimeRecorder::HandlePins);
		Runtime->OnDialogueEnded.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleEnded);
		Runtime->OnSyncStarted.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleSyncStarted);
		Runtime->OnSyncCompleted.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleSyncCompleted);
		Runtime->OnSyncFailed.AddDynamic(this, &UTarinoiRuntimeRecorder::HandleSyncFailed);
	}

	UFUNCTION() void HandleLine(const FTarinoiDialogueLine& Line) { Lines.Add(Line); }
	UFUNCTION() void HandleChoices(const TArray<FTarinoiDialogueChoice>& Choices) { ChoiceSets.Add(Choices); }
	UFUNCTION() void HandleChoiceMade(const FTarinoiDialogueLine& Line) { ChoicesMade.Add(Line); }
	UFUNCTION() void HandleError(const FString& Message) { Errors.Add(Message); }
	UFUNCTION() void HandlePins(const TArray<FString>& Pins) { PinRequests.Add(Pins); }
	UFUNCTION() void HandleEnded() { ++EndedCount; }
	UFUNCTION() void HandleSyncStarted() { ++SyncStartedCount; }
	UFUNCTION() void HandleSyncCompleted(const FTarinoiSyncStats& Stats) { SyncsCompleted.Add(Stats); }
	UFUNCTION() void HandleSyncFailed(const FString& Error) { SyncsFailed.Add(Error); }
};

/** Records which functions ran, in order, and returns a configurable pin. */
UCLASS(NotBlueprintable)
class UTarinoiSpyFunctions : public UTarinoiFunctionCollection
{
	GENERATED_BODY()

public:
	TArray<FString> Calls;
	FString PinToReturn = TEXT("a");

	UFUNCTION() bool True() { Calls.Add(TEXT("True")); return true; }
	UFUNCTION() bool False() { Calls.Add(TEXT("False")); return false; }
	UFUNCTION() void Effect() { Calls.Add(TEXT("Effect")); }
	UFUNCTION() void First() { Calls.Add(TEXT("First")); }
	UFUNCTION() void Second() { Calls.Add(TEXT("Second")); }
	UFUNCTION() FString PickPin() { Calls.Add(TEXT("PickPin")); return PinToReturn; }
};

/** A selector that posts system lines before routing, as a skill check does. */
UCLASS(NotBlueprintable)
class UTarinoiSystemLineFunctions : public UTarinoiFunctionCollection
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UTarinoiRuntime> Runtime;

	TArray<FString> ExtraLines;

	UFUNCTION()
	FString CheckSkill()
	{
		Runtime->PostSystemLine(TEXT("You rolled well."));
		for (const FString& Extra : ExtraLines)
		{
			Runtime->PostSystemLine(Extra);
		}
		return TEXT("success");
	}
};

/** A server that answers only when the test says so, so a sync can be caught mid-flight. */
struct FTarinoiDeferredTransport : ITarinoiHttpTransport
{
	int32 Requests = 0;
	TMap<FString, FString> Headers;
	TFunction<void(const FTarinoiHttpResponse&)> Pending;

	virtual void Get(const FString& Url, const TMap<FString, FString>& InHeaders, TFunction<void(const FTarinoiHttpResponse&)> OnComplete) override
	{
		++Requests;
		Headers = InHeaders;
		Pending = MoveTemp(OnComplete);
	}

	void Complete(int32 Status, const FString& Body)
	{
		FTarinoiHttpResponse Response;
		Response.Status = Status;
		Response.Body = Body;
		TFunction<void(const FTarinoiHttpResponse&)> Callback = MoveTemp(Pending);
		Pending.Reset();
		if (Callback)
		{
			Callback(Response);
		}
	}
};

/** Builds card payloads without a wall of JSON in every test. */
class FTarinoiCardBuilder
{
public:
	static FTarinoiCardBuilder Of(const TCHAR* BaseRef) { FTarinoiCardBuilder B; B.Card->SetStringField(TEXT("base_ref"), BaseRef); return B; }
	static FTarinoiCardBuilder Line(const TCHAR* Text = TEXT("")) { return Of(TEXT("line")).Data(TEXT("line"), Text); }
	static FTarinoiCardBuilder Start() { return Of(TEXT("start")); }
	static FTarinoiCardBuilder Blank() { return Of(TEXT("blank")); }

	FTarinoiCardBuilder& Mode(const TCHAR* LineMode) { Card->SetStringField(TEXT("line_mode"), LineMode); return *this; }
	FTarinoiCardBuilder& Entity(const TCHAR* EntityRef) { Card->SetStringField(TEXT("entity_ref"), EntityRef); return *this; }

	/** Sets the entry condition; nullptr stores an explicit JSON null, as unused pins are stored. */
	FTarinoiCardBuilder& Condition(const TCHAR* Condition)
	{
		if (Condition)
		{
			TSharedPtr<FJsonObject> Pin = MakeShared<FJsonObject>();
			Pin->SetStringField(TEXT("condition"), Condition);
			Card->SetObjectField(TEXT("input_pin"), Pin);
		}
		else
		{
			Card->SetField(TEXT("input_pin"), MakeShared<FJsonValueNull>());
		}
		return *this;
	}

	FTarinoiCardBuilder& Connect(std::initializer_list<const TCHAR*> Connections)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Connection : Connections)
		{
			Values.Add(MakeShared<FJsonValueString>(Connection));
		}
		Card->SetArrayField(TEXT("connections"), Values);
		return *this;
	}

	/** A plain default connection to each target. */
	FTarinoiCardBuilder& To(std::initializer_list<const TCHAR*> Targets)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Target : Targets)
		{
			Values.Add(MakeShared<FJsonValueString>(FString(TEXT("default>>")) + Target));
		}
		Card->SetArrayField(TEXT("connections"), Values);
		return *this;
	}

	FTarinoiCardBuilder& To(const TCHAR* Target) { return To({Target}); }
	FTarinoiCardBuilder& Selector(const TCHAR* Expression) { Card->SetStringField(TEXT("output_selector"), Expression); return *this; }
	FTarinoiCardBuilder& ShownOnce() { Card->SetBoolField(TEXT("shown_once"), true); return *this; }

	FTarinoiCardBuilder& Geo(double Y)
	{
		TSharedPtr<FJsonObject> G = MakeShared<FJsonObject>();
		G->SetNumberField(TEXT("x"), 0);
		G->SetNumberField(TEXT("y"), Y);
		Card->SetObjectField(TEXT("geo"), G);
		return *this;
	}

	FTarinoiCardBuilder& Data(const TCHAR* Key, const TCHAR* Value) { DataObject()->SetStringField(Key, Value); return *this; }
	FTarinoiCardBuilder& Data(const TCHAR* Key, double Value) { DataObject()->SetNumberField(Key, Value); return *this; }

	FTarinoiCardBuilder& Props(std::initializer_list<const TCHAR*> Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TCHAR* Name : Names)
		{
			TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
			Prop->SetStringField(TEXT("name"), Name);
			Values.Add(MakeShared<FJsonValueObject>(Prop));
		}
		Card->SetArrayField(TEXT("props"), Values);
		return *this;
	}

	/** A jump's destination: the target card's bare document id. */
	FTarinoiCardBuilder& Jump(const TCHAR* CardId)
	{
		Card->SetStringField(TEXT("base_ref"), TEXT("jump"));
		return Data(TEXT("target"), CardId);
	}

	operator TSharedPtr<FJsonObject>() const { return Card; }

private:
	TSharedPtr<FJsonObject> DataObject()
	{
		TSharedPtr<FJsonObject> Data = TarinoiJson::Obj(Card, TEXT("data"));
		if (!Data.IsValid())
		{
			Data = MakeShared<FJsonObject>();
			Card->SetObjectField(TEXT("data"), Data);
		}
		return Data;
	}

	TSharedPtr<FJsonObject> Card = MakeShared<FJsonObject>();
};

/**
 * A runtime over a fake store and a throwaway database, with every event recorded.
 *
 * A real database backs the caches (entities, collections, lists), because they are populated by
 * real queries with the layer merge applied, and that path is worth exercising. Cards come from
 * the fake store, which keeps the tests about traversal rather than SQL.
 */
struct FTarinoiRuntimeHarness
{
	FString ProjectId;
	TStrongObjectPtr<UTarinoiSettings> Settings;
	TStrongObjectPtr<UTarinoiRuntime> Runtime;
	TStrongObjectPtr<UTarinoiFakeDocumentStore> Store;
	TStrongObjectPtr<UTarinoiRuntimeRecorder> Events;

	FTarinoiRuntimeHarness()
	{
		ProjectId = TEXT("__test__rt_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		Settings.Reset(NewObject<UTarinoiSettings>());
		Settings->ApiPath = FString::Printf(TEXT("https://example.com/api/v1/group/%s/documents"), *ProjectId);
		Settings->bPollEnabled = false;
		Settings->bOfflineMode = false;
		Settings->bCommittedOnly = false;
		Settings->LogLevel = ETarinoiLogLevel::Log;

		Runtime.Reset(NewObject<UTarinoiRuntime>());
		Store.Reset(NewObject<UTarinoiFakeDocumentStore>());
		Events.Reset(NewObject<UTarinoiRuntimeRecorder>());
		Runtime->SetDocumentStore(Store.Get());
		Events->Listen(Runtime.Get());
	}

	~FTarinoiRuntimeHarness()
	{
		Runtime->Shutdown();
		const FString Path = FTarinoiDatabase::PathForProject(ProjectId);
		if (const TSharedPtr<FTarinoiDatabase> Open = FTarinoiDatabase::AcquireIfOpen(Path))
		{
			Open->Close();
		}
		IFileManager::Get().Delete(*Path, false, true, true);
	}

	FTarinoiRuntimeHarness& Configure()
	{
		check(Runtime->ConfigureWith(*Settings));
		return *this;
	}

	/** Writes an entity into the database. Seed before Configure: the caches load then. */
	FTarinoiRuntimeHarness& SeedEntity(const TCHAR* Identifier, bool bPlayer, const TCHAR* Label = nullptr)
	{
		return Write(TEXT("entity"), Identifier, Identifier, FString::Printf(TEXT("{\"is_player_character\":%s,\"label\":\"%s\"}"),
			bPlayer ? TEXT("true") : TEXT("false"), Label ? Label : Identifier));
	}

	/** A collection manifest with its name in the payload too, as older content has it. */
	FTarinoiRuntimeHarness& SeedCollection(const TCHAR* CollectionId, const TCHAR* Label, const TCHAR* Type = TEXT("card-collection"))
	{
		return Write(TEXT("collection-manifest"), CollectionId, CollectionId,
			FString::Printf(TEXT("{\"label\":\"%s\",\"collection_type\":\"%s\",\"collection_name\":\"%s\"}"), Label, Type, CollectionId));
	}

	/**
	 * A collection manifest as the live sync produces it: the Ls.* name lives only in the
	 * document's identifier column, never in the payload.
	 */
	FTarinoiRuntimeHarness& SeedCollectionByIdentifier(const TCHAR* CollectionId, const TCHAR* Identifier, const TCHAR* Label)
	{
		return Write(TEXT("collection-manifest"), CollectionId, Identifier,
			FString::Printf(TEXT("{\"label\":\"%s\",\"collection_type\":\"list-collection\"}"), Label));
	}

	FTarinoiRuntimeHarness& SeedListSpec(const TCHAR* CollectionId, const TCHAR* ListId, std::initializer_list<TPair<const TCHAR*, double>> Options)
	{
		TArray<FString> Items;
		for (const TPair<const TCHAR*, double>& Option : Options)
		{
			Items.Add(FString::Printf(TEXT("{\"key\":\"%s\",\"value\":%s}"), Option.Key, *TarinoiJson::NumberToString(Option.Value)));
		}
		return Write(TEXT("list-spec"), ListId, ListId, FString::Printf(TEXT("{\"label\":\"%s\",\"list_options\":[%s]}"), ListId, *FString::Join(Items, TEXT(","))),
			CollectionId);
	}

	FTarinoiRuntimeHarness& Write(const TCHAR* DocumentType, const TCHAR* DocumentId, const TCHAR* Identifier, const FString& Payload,
		const TCHAR* CollectionId = TEXT("col1"))
	{
		const TSharedPtr<FTarinoiDatabase> Db = FTarinoiDatabase::Acquire(ProjectId);
		Db->Execute(TEXT(
			"INSERT OR REPLACE INTO documents (document_id, collection_id, document_type, layer_id, namespace, identifier,"
			" update_key, is_tombstone, is_archived, is_moved, payload) VALUES (?, ?, ?, ?, 'document', ?, 1, 0, 0, 0, ?)"),
			{DocumentId, CollectionId, DocumentType, TarinoiLayerFilter::MainLayer, Identifier, Payload});
		return *this;
	}

	UTarinoiSpyFunctions* BindSpy()
	{
		UTarinoiSpyFunctions* Spy = NewObject<UTarinoiSpyFunctions>();
		Runtime->GetBindings()->BindFunctions(TEXT("g"), Spy);
		return Spy;
	}

	void Start(const TCHAR* CardId, const TCHAR* CollectionId = TEXT("col1")) { Runtime->StartDialogue(CollectionId, CardId); }
	void Advance() { Runtime->Advance(); }
	void Select(int32 Index) { Runtime->SelectChoice(Index); }
	void SelectPin(const TCHAR* Pin) { Runtime->SelectPin(Pin); }

	const TArray<FTarinoiDialogueChoice>& Choices() const
	{
		static const TArray<FTarinoiDialogueChoice> None;
		return Events->ChoiceSets.Num() > 0 ? Events->ChoiceSets.Last() : None;
	}

	TArray<FString> ChoiceLines() const
	{
		TArray<FString> Out;
		for (const FTarinoiDialogueChoice& Choice : Choices())
		{
			Out.Add(Choice.Line);
		}
		return Out;
	}

	FTarinoiDialogueLine LastLine() const { return Events->Lines.Num() > 0 ? Events->Lines.Last() : FTarinoiDialogueLine(); }
	ETarinoiDialogueState State() const { return Runtime->GetDialogueState(); }
};
