// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#include "TarinoiSyncTypes.generated.h"

/** What a sync changed. */
USTRUCT(BlueprintType)
struct TARINOI_API FTarinoiSyncStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	int32 DocumentsUpserted = 0;

	/**
	 * Documents removed from view: server-side deletions (tombstones) and documents stored as
	 * archived or moved, since all three disappear from the player's perspective.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	int32 DocumentsDeleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	int32 CollectionsUpdated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	TArray<FString> Warnings;

	FString ToString() const
	{
		FString Text = FString::Printf(TEXT("%d upserted, %d removed, %d collections"), DocumentsUpserted, DocumentsDeleted, CollectionsUpdated);
		if (Warnings.Num() > 0)
		{
			Text += FString::Printf(TEXT(", %d warnings"), Warnings.Num());
		}
		return Text;
	}
};

/** The outcome of a sync: stats, or an error written to be acted on. */
struct TARINOI_API FTarinoiSyncResult
{
	bool bSuccess = false;
	FString Error;
	FTarinoiSyncStats Stats;

	static FTarinoiSyncResult Ok(const FTarinoiSyncStats& InStats)
	{
		FTarinoiSyncResult Result;
		Result.bSuccess = true;
		Result.Stats = InStats;
		return Result;
	}

	static FTarinoiSyncResult Fail(const FString& InError)
	{
		FTarinoiSyncResult Result;
		Result.Error = InError;
		return Result;
	}
};
