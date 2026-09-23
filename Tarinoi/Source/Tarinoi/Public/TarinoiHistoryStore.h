// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "TarinoiTypes.h"
#include "UObject/Interface.h"

#include "TarinoiHistoryStore.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UTarinoiHistoryStore : public UInterface
{
	GENERATED_BODY()
};

/**
 * Remembers which cards a player has seen, so seen options can be shown differently, or, with
 * the shown_once card flag, not at all.
 *
 * Implement it over your save system (in C++ or Blueprint) to persist across sessions. The
 * runtime asks for a dialogue's seen cards when it starts and hands the updated set back when it
 * ends, so it keeps nothing itself. With no history store set, choices never report bVisited and
 * shown_once only lasts for the dialogue that is running.
 */
class TARINOI_API ITarinoiHistoryStore
{
	GENERATED_BODY()

public:
	/** Card ids already seen in the dialogue that starts at StartCardId, across earlier visits. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tarinoi")
	TArray<FString> GetVisited(const FString& StartCardId);

	/**
	 * Stores the cumulative seen set for an entry point: every NPC line displayed and every PC line
	 * chosen. Called when a dialogue ends or is aborted.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tarinoi")
	void SaveVisited(const FString& StartCardId, const TArray<FString>& VisitedIds);
};

/**
 * Keeps seen cards for as long as this object lives. Enough to stop a player re-reading an option
 * within a play session; implement ITarinoiHistoryStore yourself to survive a restart.
 */
UCLASS(BlueprintType)
class TARINOI_API UTarinoiInMemoryHistoryStore : public UObject, public ITarinoiHistoryStore
{
	GENERATED_BODY()

public:
	virtual TArray<FString> GetVisited_Implementation(const FString& StartCardId) override
	{
		const TArray<FString>* Visited = Store.Find(StartCardId);
		return Visited ? *Visited : TArray<FString>();
	}

	virtual void SaveVisited_Implementation(const FString& StartCardId, const TArray<FString>& VisitedIds) override
	{
		Store.Add(StartCardId, VisitedIds);
	}

private:
	TTarinoiMap<TArray<FString>> Store;
};
