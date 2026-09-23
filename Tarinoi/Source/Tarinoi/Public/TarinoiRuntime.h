// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Bindings/TarinoiDispatcher.h"
#include "Bindings/TarinoiValue.h"
#include "CoreMinimal.h"
#include "Data/TarinoiDocumentStore.h"
#include "Sync/TarinoiSyncTypes.h"
#include "TarinoiDialogueTypes.h"
#include "TarinoiHistoryStore.h"
#include "TarinoiTypes.h"
#include "UObject/Object.h"

#include "TarinoiRuntime.generated.h"

class FTarinoiApiImporter;
class FTarinoiDatabase;
class ITarinoiHttpTransport;
class UTarinoiBindings;
class UTarinoiSettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnLineReady, const FTarinoiDialogueLine&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnChoicesReady, const TArray<FTarinoiDialogueChoice>&, Choices);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTarinoiOnDialogueEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnDialogueError, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnChoiceMade, const FTarinoiDialogueLine&, Line);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnPinChoiceNeeded, const TArray<FString>&, Pins);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTarinoiOnSyncStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTarinoiOnSyncProgress, const FString&, Message, float, Fraction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnSyncCompleted, const FTarinoiSyncStats&, Stats);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnSyncFailed, const FString&, Error);

/**
 * Plays Tarinoi dialogue: walks the authored card graph, evaluates conditions against your
 * bindings, and raises events for the lines and choices to show.
 *
 * In a game you get the shared instance from UTarinoiSubsystem, which configures it on startup:
 *
 *   UTarinoiRuntime* Tarinoi = GetGameInstance()->GetSubsystem<UTarinoiSubsystem>()->GetRuntime();
 *   Tarinoi->GetBindings()->BindFunctions(TEXT("global"), NewObject<UMyGlobalFunctions>(this));
 *   Tarinoi->OnLineReady.AddDynamic(this, &UMyDialogueWidget::ShowLine);
 *   Tarinoi->OnChoicesReady.AddDynamic(this, &UMyDialogueWidget::ShowChoices);
 *   Tarinoi->StartDialogue(CollectionId, CardId);
 *
 * Everything runs on the game thread. Events fire synchronously from the call that caused them:
 * StartDialogue raises the first OnLineReady before it returns.
 *
 * Nothing here fails hard on an authoring mistake. A missing card, a broken condition or an
 * unbound function raises OnDialogueError and stops that traversal; the game keeps running.
 */
UCLASS(BlueprintType)
class TARINOI_API UTarinoiRuntime : public UObject, public ITarinoiEntityCache
{
	GENERATED_BODY()

public:
	UTarinoiRuntime();

	// ---------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------

	/** A line is ready to display. The player then calls Advance. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnLineReady OnLineReady;

	/** Choices are ready. The player picks one with SelectChoice. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnChoicesReady OnChoicesReady;

	/** The dialogue reached an end, was aborted, or could not continue. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnDialogueEnded OnDialogueEnded;

	/** An authoring or binding problem stopped the current traversal. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnDialogueError OnDialogueError;

	/** The player chose an option. Raised before the dialogue continues. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnChoiceMade OnChoiceMade;

	/** A card has named pins but no usable output selector, so a pin must be picked with SelectPin. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Dialogue")
	FTarinoiOnPinChoiceNeeded OnPinChoiceNeeded;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Sync")
	FTarinoiOnSyncStarted OnSyncStarted;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Sync")
	FTarinoiOnSyncProgress OnSyncProgress;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Sync")
	FTarinoiOnSyncCompleted OnSyncCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi|Sync")
	FTarinoiOnSyncFailed OnSyncFailed;

	// ---------------------------------------------------------------------------------------
	// Configuration
	// ---------------------------------------------------------------------------------------

	/**
	 * Opens the local content database from the project settings and prepares the runtime. The
	 * subsystem does this on startup. Safe to call again to reconfigure. Returns false (and logs
	 * why) when the project cannot be loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	bool Configure();

	/** Configure from explicit settings rather than the project's. */
	bool ConfigureWith(const UTarinoiSettings& Settings);

	/** Closes the database and clears cached content. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void Shutdown();

	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	bool IsConfigured() const;

	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	FString GetProjectId() const { return ProjectId; }

	/**
	 * Where game code registers what implements Fn.*, Var.* and Ent.*. Bindings can be registered
	 * before or after Configure, as long as they are in place before dialogue starts.
	 */
	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	UTarinoiBindings* GetBindings() const { return Bindings; }

	/** Optional seen-card tracking. See ITarinoiHistoryStore. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void SetHistoryStore(TScriptInterface<ITarinoiHistoryStore> InHistoryStore) { HistoryStore = InHistoryStore; }

	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	TScriptInterface<ITarinoiHistoryStore> GetHistoryStore() const { return HistoryStore; }

	/**
	 * Replaces where content is read from. Set it before Configure; otherwise the store class in
	 * the project settings is used, and failing that the built-in SQLite store.
	 */
	void SetDocumentStore(UTarinoiDocumentStore* InDocumentStore) { DocumentStore = InDocumentStore; }
	UTarinoiDocumentStore* GetDocumentStore() const { return DocumentStore; }

	/** Replaces the network transport sync uses. For tests. */
	void SetHttpTransport(const TSharedPtr<ITarinoiHttpTransport>& InTransport) { HttpTransport = InTransport; }

	// ---------------------------------------------------------------------------------------
	// Sync
	// ---------------------------------------------------------------------------------------

	/**
	 * Fetches new content from the Tarinoi API and refreshes the caches, raising the sync events.
	 * In offline mode it completes at once without contacting anything. A sync already running
	 * makes this a no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Sync")
	void Sync();

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Sync")
	bool IsSyncing() const { return ActiveImport.IsValid(); }

	// ---------------------------------------------------------------------------------------
	// Dialogue
	// ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void StartDialogue(const FString& CollectionId, const FString& CardId);

	/** Moves past the line on screen. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void Advance();

	/** Picks one of the choices most recently offered, by its Index. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void SelectChoice(int32 Index);

	/** Picks a named pin by hand. See OnPinChoiceNeeded. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void SelectPin(const FString& PinName);

	/** Ends the dialogue now, saving what was seen. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void AbortDialogue();

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Dialogue")
	ETarinoiDialogueState GetDialogueState() const { return State; }

	/** Every dialogue entry point, grouped by collection label. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	TArray<FTarinoiStartCard> GetStartCards();

	/** Evaluates any Tarinoi expression. Useful inside a binding, for example to read a list option. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	FTarinoiValue EvalExpression(const FString& Expression);

	/**
	 * Queues a line to show before the dialogue moves on. Meant for bindings called from an output
	 * selector, such as a skill check telling the player what happened before routing them: the
	 * line appears as an ordinary line the player advances past. Only the first queued line shows.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Dialogue")
	void PostSystemLine(const FString& Text) { PendingSystemLines.Add(Text); }

	// ITarinoiEntityCache
	virtual TSharedPtr<FJsonObject> GetEntityPayload(const FString& Identifier) const override;

private:
	// Traversal
	void LoadAndProcessCard(const FString& CollectionId, const FString& CardId);
	void ProcessCard(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId);
	void ProcessLine(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId);
	void FollowConnections(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId);
	void FollowNamedPins(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId,
		const TArray<TPair<FString, FString>>& NamedPins);
	void FollowJump(const TSharedPtr<FJsonObject>& Card, const FString& CardId);
	void BuildChoicesFromTargets(const TArray<FString>& TargetIds, const FString& CollectionId, const FString& SourceCardId);
	TArray<FTarinoiDialogueChoice> SelectByKind(TArray<FTarinoiDialogueChoice> Sorted, const FString& SourceCardId) const;
	void ShowSystemLine(const FString& CollectionId, const FString& NavTarget);
	void FinishDialogue();
	bool CheckLoop(const FString& CardId);
	void RaiseError(const FString& Message);

	// Card functions and conditions
	void EvalCardFunctions(const TSharedPtr<FJsonObject>& Card, const FString& CardId);
	bool EvalGuarded(const FString& Condition, const FString& Where);

	// Caches
	void LoadGlobalCache();
	void LoadCollections();
	void LoadEntities();
	void LoadLists();
	FString CollectionLabel(const FString& CollectionId) const;

	// Helpers
	bool IsPcCard(const TSharedPtr<FJsonObject>& Card) const;
	bool IsSpentShownOnce(const TSharedPtr<FJsonObject>& Card, const FString& CardId) const;
	FTarinoiDialogueChoice MakeChoice(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId, int32 Index) const;
	FTarinoiDialogueLine MakeLine(const TSharedPtr<FJsonObject>& Card, const FString& CardId, const FString& CollectionId) const;
	bool EnsureConfigured() const;
	UTarinoiDocumentStore* CreateConfiguredStore(const UTarinoiSettings& Settings);

	UPROPERTY()
	TObjectPtr<UTarinoiBindings> Bindings;

	UPROPERTY()
	TObjectPtr<UTarinoiDocumentStore> DocumentStore;

	UPROPERTY()
	TScriptInterface<ITarinoiHistoryStore> HistoryStore;

	TSharedPtr<FTarinoiDatabase> Database;
	TUniquePtr<FTarinoiDispatcher> Dispatcher;
	TSharedPtr<ITarinoiHttpTransport> HttpTransport;
	TSharedPtr<FTarinoiApiImporter> ActiveImport;

	// The settings Configure read, so a later Sync uses the same ones.
	FString ProjectId;
	FString ApiPath;
	bool bOfflineMode = false;

	ETarinoiDialogueState State = ETarinoiDialogueState::Idle;
	FString CurrentCollectionId;
	FString CurrentCardId;
	TSharedPtr<FJsonObject> CurrentCard;
	TArray<FTarinoiDialogueChoice> Choices;

	/** Cards passed through since the dialogue last stopped for the player: the loop guard. */
	FTarinoiStringSet Visited;

	FString SessionStartCardId;

	/**
	 * Every line the player has actually seen: NPC lines when displayed, PC lines when chosen.
	 * Seeded from the history store on start and handed back on end.
	 */
	FTarinoiStringSet SessionSeen;

	TArray<FString> PendingSystemLines;
	FString PendingNavTarget;

	// Layer-merged caches of everything that is not a card, rebuilt after each sync.
	TTarinoiMap<TSharedPtr<FJsonObject>> Collections;
	TTarinoiMap<FString> CollectionLabels;
	TTarinoiMap<FString> CollectionIdentifiers;
	TTarinoiMap<TSharedPtr<FJsonObject>> Entities;
	FTarinoiLists Lists;
};
