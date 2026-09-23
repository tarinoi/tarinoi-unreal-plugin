// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include "TarinoiDialogueTypes.generated.h"

/** What the dialogue is waiting for. */
UENUM(BlueprintType)
enum class ETarinoiDialogueState : uint8
{
	/** No dialogue in progress, or between cards. */
	Idle,
	/** A line is showing; the player advances past it. */
	NpcLine,
	/** The player is choosing between lines. */
	PcChoice,
	/**
	 * Waiting for a named pin to be picked by hand. Only reached when a card has named pins but no
	 * usable output selector: a development fallback, not a player-facing state.
	 */
	AwaitingPin,
};

/** A line of dialogue to display. */
USTRUCT(BlueprintType)
struct TARINOI_API FTarinoiDialogueLine
{
	GENERATED_BODY()

	/** The card id of a system line, which has no card of its own. */
	static const TCHAR* SystemCardId;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CardId;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CollectionId;

	/** The speaking entity's identifier, or "system" for a system line. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString EntityRef;

	/** The speaker's display name, falling back to EntityRef. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString EntityLabel;

	/** "pc", "npc", "inherit" or "system". */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString LineMode;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString Line;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString BaseRef;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString TemplateRef;

	/**
	 * The card's authored properties, for whatever its template defines. Text values as written;
	 * numbers and booleans formatted; nested objects and arrays as JSON.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	TMap<FString, FString> Data;

	/** Whether this is an interstitial system line (see PostSystemLine) rather than authored dialogue. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	bool bIsSystem = false;

	/** The card's data as parsed JSON, for C++. */
	TSharedPtr<FJsonObject> DataJson;
};

/** One option offered to the player. */
USTRUCT(BlueprintType)
struct TARINOI_API FTarinoiDialogueChoice
{
	GENERATED_BODY()

	/** Position in the choice list; pass it to SelectChoice. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CardId;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CollectionId;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString EntityRef;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString LineMode;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString Line;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	TMap<FString, FString> Data;

	/**
	 * Whether the player has seen this card before. False unless a history store is set, or the
	 * card was seen earlier in the dialogue that is running.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	bool bVisited = false;

	/** The full card, for C++. */
	TSharedPtr<FJsonObject> Card;
};

/** A dialogue entry point, for a "where would you like to start?" list. */
USTRUCT(BlueprintType)
struct TARINOI_API FTarinoiStartCard
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CardId;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CollectionId;

	/** The display name of the collection the entry point lives in. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString CollectionLabel;

	/** A display label, including the card id to tell repeated labels apart. */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	FString Label;
};
