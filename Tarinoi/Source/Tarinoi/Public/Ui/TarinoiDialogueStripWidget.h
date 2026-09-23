// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "TarinoiDialogueTypes.h"

#include "TarinoiDialogueStripWidget.generated.h"

class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UTarinoiRuntime;

/** Runs a callback when a button is clicked. UButton's click event carries no context, so each button gets one of these. */
UCLASS()
class TARINOI_API UTarinoiButtonAction : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Callback;

	UFUNCTION()
	void Invoke()
	{
		if (Callback)
		{
			Callback();
		}
	}
};

/**
 * Shows dialogue as a scrolling transcript: lines accumulate, past entries dim, and choices appear
 * as buttons under the latest line. Space continues, number keys pick a choice, Escape ends the
 * dialogue.
 *
 * A transcript rather than one replaced line because it shows what the runtime is doing while you
 * author: you can read the path taken. Once you move on, the old buttons turn to plain text, so
 * history cannot be clicked again.
 *
 * This is a debug view built on the runtime's events, not a UI to ship. It builds its own widget
 * tree, so it needs no Widget Blueprint; create it with CreateWidget and call BindRuntime.
 */
UCLASS()
class TARINOI_API UTarinoiDialogueStripWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Follows a runtime's dialogue events. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void BindRuntime(UTarinoiRuntime* InRuntime);

	/** Empties the transcript, ready for a new conversation. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void Clear();

	/** How many entries the transcript holds. */
	int32 GetEntryCount() const;

	/** The actions the latest entry offers, in order: Continue, or one per choice. For tests and key handling. */
	int32 GetLiveActionCount() const { return LiveActions.Num(); }
	void InvokeLiveAction(int32 Index);

	/** The text of every entry, oldest first. For tests. */
	TArray<FString> GetTranscript() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	UFUNCTION()
	void HandleLine(const FTarinoiDialogueLine& Line);

	UFUNCTION()
	void HandleChoices(const TArray<FTarinoiDialogueChoice>& Choices);

	UFUNCTION()
	void HandlePins(const TArray<FString>& Pins);

	UFUNCTION()
	void HandleError(const FString& Message);

	void BuildTree();
	UVerticalBox* NewEntry();
	void AddText(UVerticalBox* Entry, const FString& Text, int32 Size, const FLinearColor& Color, bool bBold = false, bool bItalic = false);
	void AddAction(UVerticalBox* Entry, const FString& Label, TFunction<void()> OnClick, bool bDimmed = false);
	void FreezePrevious();
	void SetHint(const FString& Text);
	void Unbind();

	UPROPERTY()
	TObjectPtr<UTarinoiRuntime> Runtime;

	UPROPERTY()
	TObjectPtr<UScrollBox> Feed;

	UPROPERTY()
	TObjectPtr<UTextBlock> Hint;

	UPROPERTY()
	TArray<TObjectPtr<UVerticalBox>> LiveEntries;

	UPROPERTY()
	TArray<TObjectPtr<UTarinoiButtonAction>> LiveActions;

	/** Every handler ever created, kept alive until Clear: a click may still be in flight. */
	UPROPERTY()
	TArray<TObjectPtr<UTarinoiButtonAction>> AllActions;
};
