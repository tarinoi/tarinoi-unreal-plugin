// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Ui/TarinoiQuickstartWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "TarinoiRuntime.h"
#include "Ui/TarinoiDialogueStripWidget.h"
#include "Ui/TarinoiStartPickerWidget.h"
#include "Ui/TarinoiUiStyle.h"

TSharedRef<SWidget> UTarinoiQuickstartWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
		WidgetTree->RootWidget = Root;

		Picker = WidgetTree->ConstructWidget<UTarinoiStartPickerWidget>();
		Strip = WidgetTree->ConstructWidget<UTarinoiDialogueStripWidget>();
		for (UWidget* Child : {static_cast<UWidget*>(Picker), static_cast<UWidget*>(Strip)})
		{
			// A readable column, whatever the window width.
			USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>();
			Column->SetWidthOverride(TarinoiUiStyle::ColumnWidth);
			Column->AddChild(Child);
			if (UOverlaySlot* LayoutSlot = Root->AddChildToOverlay(Column))
			{
				LayoutSlot->SetHorizontalAlignment(HAlign_Center);
				LayoutSlot->SetVerticalAlignment(VAlign_Fill);
				LayoutSlot->SetPadding(FMargin(0.0f, 48.0f));
			}
		}
		Strip->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void UTarinoiQuickstartWidget::BindRuntime(UTarinoiRuntime* InRuntime)
{
	TakeWidget();
	Unbind();
	Runtime = InRuntime;

	// The strip binds first, so it has drawn a line before the view switch below sees it.
	Strip->BindRuntime(Runtime);
	Picker->BindRuntime(Runtime);
	Picker->OnStartSelected.AddDynamic(this, &UTarinoiQuickstartWidget::HandleStartSelected);

	if (Runtime)
	{
		Runtime->OnLineReady.AddDynamic(this, &UTarinoiQuickstartWidget::HandleLine);
		Runtime->OnChoicesReady.AddDynamic(this, &UTarinoiQuickstartWidget::HandleChoices);
		Runtime->OnPinChoiceNeeded.AddDynamic(this, &UTarinoiQuickstartWidget::HandlePins);
		Runtime->OnDialogueError.AddDynamic(this, &UTarinoiQuickstartWidget::HandleError);
		Runtime->OnDialogueEnded.AddDynamic(this, &UTarinoiQuickstartWidget::HandleEnded);
	}
}

void UTarinoiQuickstartWidget::Unbind()
{
	if (Runtime)
	{
		Runtime->OnLineReady.RemoveAll(this);
		Runtime->OnChoicesReady.RemoveAll(this);
		Runtime->OnPinChoiceNeeded.RemoveAll(this);
		Runtime->OnDialogueError.RemoveAll(this);
		Runtime->OnDialogueEnded.RemoveAll(this);
	}
	if (Picker)
	{
		Picker->OnStartSelected.RemoveAll(this);
	}
	Runtime = nullptr;
}

void UTarinoiQuickstartWidget::ShowPicker()
{
	TakeWidget();
	Strip->SetVisibility(ESlateVisibility::Collapsed);
	Strip->Clear();
	Picker->SetVisibility(ESlateVisibility::Visible);
	Picker->SetKeyboardFocus();
}

void UTarinoiQuickstartWidget::ShowDialogue()
{
	TakeWidget();
	if (IsShowingDialogue())
	{
		return;
	}
	Picker->SetVisibility(ESlateVisibility::Collapsed);
	Strip->SetVisibility(ESlateVisibility::Visible);
	Strip->SetKeyboardFocus();
}

bool UTarinoiQuickstartWidget::IsShowingDialogue() const
{
	return Strip && Strip->GetVisibility() == ESlateVisibility::Visible;
}

void UTarinoiQuickstartWidget::HandleStartSelected(const FString& CollectionId, const FString& CardId)
{
	if (Runtime)
	{
		Runtime->StartDialogue(CollectionId, CardId);
	}
}

void UTarinoiQuickstartWidget::HandleLine(const FTarinoiDialogueLine& Line) { ShowDialogue(); }
void UTarinoiQuickstartWidget::HandleChoices(const TArray<FTarinoiDialogueChoice>& Choices) { ShowDialogue(); }
void UTarinoiQuickstartWidget::HandlePins(const TArray<FString>& Pins) { ShowDialogue(); }
void UTarinoiQuickstartWidget::HandleError(const FString& Message) { ShowDialogue(); }
void UTarinoiQuickstartWidget::HandleEnded() { ShowPicker(); }
