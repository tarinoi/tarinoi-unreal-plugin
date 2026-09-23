// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "TarinoiDialogueTypes.h"

#include "TarinoiQuickstartWidget.generated.h"

class UTarinoiDialogueStripWidget;
class UTarinoiRuntime;
class UTarinoiStartPickerWidget;

/**
 * The quickstart's whole interface: the entry-point picker while no dialogue runs, the dialogue
 * strip while one does.
 *
 * The strip listens to the runtime itself, and was bound before this widget, so when a line
 * arrives the strip has already added it by the time the view switches. The transcript is
 * therefore cleared on the way back to the picker, when it is stale, and never on the way in,
 * which would wipe the line just shown.
 */
UCLASS()
class TARINOI_API UTarinoiQuickstartWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void BindRuntime(UTarinoiRuntime* InRuntime);

	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void ShowPicker();

	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void ShowDialogue();

	bool IsShowingDialogue() const;

	UTarinoiStartPickerWidget* GetPicker() const { return Picker; }
	UTarinoiDialogueStripWidget* GetStrip() const { return Strip; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleStartSelected(const FString& CollectionId, const FString& CardId);

	UFUNCTION()
	void HandleLine(const FTarinoiDialogueLine& Line);

	UFUNCTION()
	void HandleChoices(const TArray<FTarinoiDialogueChoice>& Choices);

	UFUNCTION()
	void HandlePins(const TArray<FString>& Pins);

	UFUNCTION()
	void HandleError(const FString& Message);

	UFUNCTION()
	void HandleEnded();

	void Unbind();

	UPROPERTY()
	TObjectPtr<UTarinoiRuntime> Runtime;

	UPROPERTY()
	TObjectPtr<UTarinoiStartPickerWidget> Picker;

	UPROPERTY()
	TObjectPtr<UTarinoiDialogueStripWidget> Strip;
};
