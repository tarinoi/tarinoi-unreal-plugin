// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Ui/TarinoiStartPickerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "TarinoiRuntime.h"
#include "Ui/TarinoiDialogueStripWidget.h"
#include "Ui/TarinoiUiStyle.h"

TSharedRef<SWidget> UTarinoiStartPickerWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UTarinoiStartPickerWidget::BuildTree()
{
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>();
	Root->SetBrushColor(TarinoiUiStyle::Background);
	Root->SetPadding(FMargin(24.0f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->SetContent(Column);

	Column->AddChildToVerticalBox(TarinoiUiStyle::Text(WidgetTree, TEXT("Where would you like to start?"), 22, TarinoiUiStyle::Heading, true));

	Status = TarinoiUiStyle::Text(WidgetTree, FString(), 13, TarinoiUiStyle::Dimmed);
	if (UVerticalBoxSlot* LayoutSlot = Column->AddChildToVerticalBox(Status))
	{
		LayoutSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 12.0f));
	}

	List = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* LayoutSlot = Column->AddChildToVerticalBox(List))
	{
		LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
}

void UTarinoiStartPickerWidget::BindRuntime(UTarinoiRuntime* InRuntime)
{
	// Build the tree first: the first Slate build runs the widget's construct/destruct cycle.
	TakeWidget();
	Unbind();
	Runtime = InRuntime;
	if (!Runtime)
	{
		return;
	}

	Runtime->OnSyncStarted.AddDynamic(this, &UTarinoiStartPickerWidget::HandleSyncStarted);
	Runtime->OnSyncCompleted.AddDynamic(this, &UTarinoiStartPickerWidget::HandleSyncCompleted);
	Runtime->OnSyncFailed.AddDynamic(this, &UTarinoiStartPickerWidget::HandleSyncFailed);
	Refresh();
}

void UTarinoiStartPickerWidget::Unbind()
{
	if (Runtime)
	{
		Runtime->OnSyncStarted.RemoveAll(this);
		Runtime->OnSyncCompleted.RemoveAll(this);
		Runtime->OnSyncFailed.RemoveAll(this);
	}
	Runtime = nullptr;
}

void UTarinoiStartPickerWidget::SetStatus(const FString& Text, bool bProblem)
{
	TakeWidget();
	Status->SetText(FText::FromString(Text));
	Status->SetColorAndOpacity(FSlateColor(bProblem ? TarinoiUiStyle::SystemLine : TarinoiUiStyle::Dimmed));
}

void UTarinoiStartPickerWidget::Refresh()
{
	TakeWidget();
	List->ClearChildren();
	EntryActions.Reset();

	if (!Runtime || !Runtime->IsConfigured())
	{
		SetStatus(TEXT("Tarinoi is not set up. Set the API path in Project Settings > Plugins > Tarinoi, sync, and press Play again."), true);
		return;
	}

	const TArray<FTarinoiStartCard> Cards = Runtime->GetStartCards();
	if (Cards.Num() == 0)
	{
		SetStatus(TEXT("No entry points yet. Sync (Tools > Tarinoi > Sync), or add a start card in Tarinoi."), true);
		return;
	}

	SetStatus(FString::Printf(TEXT("%d entry point(s)"), Cards.Num()));

	FString CurrentGroup;
	bool bFirstGroup = true;
	for (const FTarinoiStartCard& Card : Cards)
	{
		if (bFirstGroup || !Card.CollectionLabel.Equals(CurrentGroup, ESearchCase::CaseSensitive))
		{
			UTextBlock* Heading = TarinoiUiStyle::Text(WidgetTree, Card.CollectionLabel, 15, TarinoiUiStyle::Speaker, true);
			if (UScrollBoxSlot* LayoutSlot = Cast<UScrollBoxSlot>(List->AddChild(Heading)))
			{
				LayoutSlot->SetPadding(FMargin(0.0f, bFirstGroup ? 0.0f : 12.0f, 0.0f, 4.0f));
			}
			CurrentGroup = Card.CollectionLabel;
			bFirstGroup = false;
		}

		UButton* Button = TarinoiUiStyle::Button(WidgetTree, Card.Label);
		UTarinoiButtonAction* Action = NewObject<UTarinoiButtonAction>(this);
		const FString CollectionId = Card.CollectionId;
		const FString CardId = Card.CardId;
		Action->Callback = [this, CollectionId, CardId]()
		{
			OnStartSelected.Broadcast(CollectionId, CardId);
		};
		Button->OnClicked.AddDynamic(Action, &UTarinoiButtonAction::Invoke);
		EntryActions.Add(Action);

		if (UScrollBoxSlot* LayoutSlot = Cast<UScrollBoxSlot>(List->AddChild(Button)))
		{
			LayoutSlot->SetPadding(FMargin(0.0f, 2.0f));
			LayoutSlot->SetHorizontalAlignment(HAlign_Left);
		}
	}
}

void UTarinoiStartPickerWidget::Pick(int32 Index)
{
	if (EntryActions.IsValidIndex(Index))
	{
		EntryActions[Index]->Invoke();
	}
}

void UTarinoiStartPickerWidget::HandleSyncStarted()
{
	SetStatus(TEXT("Syncing..."));
}

void UTarinoiStartPickerWidget::HandleSyncCompleted(const FTarinoiSyncStats& Stats)
{
	Refresh();
}

void UTarinoiStartPickerWidget::HandleSyncFailed(const FString& Error)
{
	Refresh();
	SetStatus(TEXT("Sync failed, showing content already synced. ") + Error, true);
}
