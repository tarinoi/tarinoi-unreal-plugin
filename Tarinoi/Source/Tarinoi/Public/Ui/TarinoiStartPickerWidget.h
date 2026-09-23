// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Sync/TarinoiSyncTypes.h"

#include "TarinoiStartPickerWidget.generated.h"

class UScrollBox;
class UTextBlock;
class UTarinoiButtonAction;
class UTarinoiRuntime;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTarinoiOnStartSelected, const FString&, CollectionId, const FString&, CardId);

/**
 * Lists every dialogue entry point, grouped by collection, and raises OnStartSelected when one is
 * picked. Refreshes itself after each sync, and says when a sync is running or failed.
 *
 * A debug view: a game decides for itself when dialogue starts (see UTarinoiDialogueTrigger).
 */
UCLASS()
class TARINOI_API UTarinoiStartPickerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi")
	FTarinoiOnStartSelected OnStartSelected;

	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void BindRuntime(UTarinoiRuntime* InRuntime);

	/** Lists the entry points again. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void Refresh();

	/** Shows a status line above the list. */
	void SetStatus(const FString& Text, bool bProblem = false);

	/** How many entry points are listed. For tests. */
	int32 GetEntryCount() const { return EntryActions.Num(); }

	/** Picks the entry point at a position in the list. For tests. */
	void Pick(int32 Index);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleSyncStarted();

	UFUNCTION()
	void HandleSyncCompleted(const FTarinoiSyncStats& Stats);

	UFUNCTION()
	void HandleSyncFailed(const FString& Error);

	void BuildTree();
	void Unbind();

	UPROPERTY()
	TObjectPtr<UTarinoiRuntime> Runtime;

	UPROPERTY()
	TObjectPtr<UScrollBox> List;

	UPROPERTY()
	TObjectPtr<UTextBlock> Status;

	UPROPERTY()
	TArray<TObjectPtr<UTarinoiButtonAction>> EntryActions;
};
