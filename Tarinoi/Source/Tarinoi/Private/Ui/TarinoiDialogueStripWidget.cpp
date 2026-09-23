// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Ui/TarinoiDialogueStripWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "TarinoiRuntime.h"
#include "Ui/TarinoiUiStyle.h"

TSharedRef<SWidget> UTarinoiDialogueStripWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UTarinoiDialogueStripWidget::BuildTree()
{
	SetIsFocusable(true);

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>();
	Root->SetBrushColor(TarinoiUiStyle::Background);
	Root->SetPadding(FMargin(24.0f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Root->SetContent(Column);

	Feed = WidgetTree->ConstructWidget<UScrollBox>();
	if (UVerticalBoxSlot* LayoutSlot = Column->AddChildToVerticalBox(Feed))
	{
		LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	Hint = TarinoiUiStyle::Text(WidgetTree, FString(), 13, TarinoiUiStyle::Dimmed);
	Hint->SetJustification(ETextJustify::Right);
	if (UVerticalBoxSlot* LayoutSlot = Column->AddChildToVerticalBox(Hint))
	{
		LayoutSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}
}

void UTarinoiDialogueStripWidget::BindRuntime(UTarinoiRuntime* InRuntime)
{
	// Build the tree first: the first Slate build runs the widget's construct/destruct cycle.
	TakeWidget();
	Unbind();
	Runtime = InRuntime;
	if (!Runtime)
	{
		return;
	}

	Runtime->OnLineReady.AddDynamic(this, &UTarinoiDialogueStripWidget::HandleLine);
	Runtime->OnChoicesReady.AddDynamic(this, &UTarinoiDialogueStripWidget::HandleChoices);
	Runtime->OnPinChoiceNeeded.AddDynamic(this, &UTarinoiDialogueStripWidget::HandlePins);
	Runtime->OnDialogueError.AddDynamic(this, &UTarinoiDialogueStripWidget::HandleError);
}

void UTarinoiDialogueStripWidget::Unbind()
{
	if (Runtime)
	{
		Runtime->OnLineReady.RemoveAll(this);
		Runtime->OnChoicesReady.RemoveAll(this);
		Runtime->OnPinChoiceNeeded.RemoveAll(this);
		Runtime->OnDialogueError.RemoveAll(this);
	}
	Runtime = nullptr;
}

void UTarinoiDialogueStripWidget::Clear()
{
	if (Feed)
	{
		Feed->ClearChildren();
	}
	LiveEntries.Reset();
	LiveActions.Reset();
	AllActions.Reset();
	SetHint(FString());
}

int32 UTarinoiDialogueStripWidget::GetEntryCount() const
{
	return Feed ? Feed->GetChildrenCount() : 0;
}

TArray<FString> UTarinoiDialogueStripWidget::GetTranscript() const
{
	TArray<FString> Out;
	if (!Feed)
	{
		return Out;
	}

	for (int32 Index = 0; Index < Feed->GetChildrenCount(); ++Index)
	{
		TArray<FString> Parts;
		if (const UVerticalBox* Entry = Cast<UVerticalBox>(Feed->GetChildAt(Index)))
		{
			for (int32 Child = 0; Child < Entry->GetChildrenCount(); ++Child)
			{
				if (const UTextBlock* Text = Cast<UTextBlock>(Entry->GetChildAt(Child)))
				{
					Parts.Add(Text->GetText().ToString());
				}
			}
		}
		Out.Add(FString::Join(Parts, TEXT(" / ")));
	}
	return Out;
}

void UTarinoiDialogueStripWidget::InvokeLiveAction(int32 Index)
{
	if (LiveActions.IsValidIndex(Index))
	{
		// Held locally: invoking may freeze and replace the live actions.
		UTarinoiButtonAction* Action = LiveActions[Index];
		Action->Invoke();
	}
}

void UTarinoiDialogueStripWidget::HandleLine(const FTarinoiDialogueLine& Line)
{
	FreezePrevious();
	UVerticalBox* Entry = NewEntry();

	if (Line.bIsSystem)
	{
		AddText(Entry, Line.Line, 15, TarinoiUiStyle::SystemLine, false, true);
	}
	else
	{
		if (!Line.EntityLabel.IsEmpty())
		{
			AddText(Entry, Line.EntityLabel, 14, TarinoiUiStyle::Speaker, true);
		}
		AddText(Entry, Line.Line, 17, TarinoiUiStyle::Body);
	}

	AddAction(Entry, TEXT("Continue"), [this]()
	{
		if (Runtime)
		{
			Runtime->Advance();
		}
	});
	SetHint(TEXT("Space to continue, Esc to stop"));
}

void UTarinoiDialogueStripWidget::HandleChoices(const TArray<FTarinoiDialogueChoice>& Choices)
{
	FreezePrevious();
	UVerticalBox* Entry = NewEntry();

	for (const FTarinoiDialogueChoice& Choice : Choices)
	{
		const int32 Index = Choice.Index;
		// Seen options are dimmed, not hidden: the player may want to hear them again.
		AddAction(Entry, FString::Printf(TEXT("%d. %s"), Index + 1, *Choice.Line), [this, Index]()
		{
			if (Runtime)
			{
				Runtime->SelectChoice(Index);
			}
		}, Choice.bVisited);
	}

	SetHint(Choices.Num() == 1 ? FString(TEXT("One way forward")) : FString::Printf(TEXT("%d ways forward: press a number"), Choices.Num()));
}

void UTarinoiDialogueStripWidget::HandlePins(const TArray<FString>& Pins)
{
	// Development tooling: a card whose output selector is missing or unbound. Players never see this.
	FreezePrevious();
	UVerticalBox* Entry = NewEntry();
	AddText(Entry, TEXT("This card needs a pin picked by hand: its output selector is missing or not bound."), 13, TarinoiUiStyle::SystemLine);
	for (const FString& Pin : Pins)
	{
		AddAction(Entry, TEXT("Pin: ") + Pin, [this, Pin]()
		{
			if (Runtime)
			{
				Runtime->SelectPin(Pin);
			}
		});
	}
	SetHint(TEXT("Waiting for a pin"));
}

void UTarinoiDialogueStripWidget::HandleError(const FString& Message)
{
	FreezePrevious();
	UVerticalBox* Entry = NewEntry();
	AddText(Entry, TEXT("The dialogue stopped: ") + Message, 14, TarinoiUiStyle::SystemLine, false, true);
	AddAction(Entry, TEXT("Back to the start"), [this]()
	{
		if (Runtime)
		{
			Runtime->AbortDialogue();
		}
	});
	SetHint(TEXT("See the Output Log for details"));
}

UVerticalBox* UTarinoiDialogueStripWidget::NewEntry()
{
	TakeWidget();
	UVerticalBox* Entry = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UScrollBoxSlot* LayoutSlot = Cast<UScrollBoxSlot>(Feed->AddChild(Entry)))
	{
		LayoutSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}
	LiveEntries.Add(Entry);
	Feed->ScrollToEnd();
	return Entry;
}

void UTarinoiDialogueStripWidget::AddText(UVerticalBox* Entry, const FString& Text, int32 Size, const FLinearColor& Color, bool bBold, bool bItalic)
{
	if (UVerticalBoxSlot* LayoutSlot = Entry->AddChildToVerticalBox(TarinoiUiStyle::Text(WidgetTree, Text, Size, Color, bBold, bItalic)))
	{
		LayoutSlot->SetPadding(FMargin(0.0f, 2.0f));
	}
}

void UTarinoiDialogueStripWidget::AddAction(UVerticalBox* Entry, const FString& Label, TFunction<void()> OnClick, bool bDimmed)
{
	UTextBlock* Text = nullptr;
	UButton* Button = TarinoiUiStyle::Button(WidgetTree, Label, &Text);
	if (bDimmed)
	{
		Text->SetColorAndOpacity(FSlateColor(TarinoiUiStyle::Dimmed));
	}

	UTarinoiButtonAction* Action = NewObject<UTarinoiButtonAction>(this);
	Action->Callback = MoveTemp(OnClick);
	Button->OnClicked.AddDynamic(Action, &UTarinoiButtonAction::Invoke);
	LiveActions.Add(Action);
	AllActions.Add(Action);

	if (UVerticalBoxSlot* LayoutSlot = Entry->AddChildToVerticalBox(Button))
	{
		LayoutSlot->SetPadding(FMargin(0.0f, 2.0f));
		LayoutSlot->SetHorizontalAlignment(HAlign_Left);
	}
}

void UTarinoiDialogueStripWidget::FreezePrevious()
{
	for (UVerticalBox* Entry : LiveEntries)
	{
		for (int32 Index = 0; Index < Entry->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Entry->GetChildAt(Index);
			if (UButton* Button = Cast<UButton>(Child))
			{
				Button->OnClicked.Clear();
				Button->SetIsEnabled(false);
				Button->SetBackgroundColor(FLinearColor::Transparent);
				if (UTextBlock* Label = Cast<UTextBlock>(Button->GetContent()))
				{
					Label->SetColorAndOpacity(FSlateColor(TarinoiUiStyle::Dimmed));
				}
			}
			else if (UTextBlock* Text = Cast<UTextBlock>(Child))
			{
				Text->SetColorAndOpacity(FSlateColor(TarinoiUiStyle::Dimmed));
			}
		}
	}
	LiveEntries.Reset();
	LiveActions.Reset();
}

void UTarinoiDialogueStripWidget::SetHint(const FString& Text)
{
	if (Hint)
	{
		Hint->SetText(FText::FromString(Text));
	}
}

FReply UTarinoiDialogueStripWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();

	if (Key == EKeys::Escape && Runtime && Runtime->GetDialogueState() != ETarinoiDialogueState::Idle)
	{
		Runtime->AbortDialogue();
		return FReply::Handled();
	}

	if ((Key == EKeys::SpaceBar || Key == EKeys::Enter) && Runtime && Runtime->GetDialogueState() == ETarinoiDialogueState::NpcLine)
	{
		InvokeLiveAction(0);
		return FReply::Handled();
	}

	// 1 to 9, then 0 for the tenth.
	static const FKey Digits[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Digits); ++Index)
	{
		if (Key == Digits[Index] && Runtime && Runtime->GetDialogueState() == ETarinoiDialogueState::PcChoice)
		{
			InvokeLiveAction(Index);
			return FReply::Handled();
		}
	}

	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}
