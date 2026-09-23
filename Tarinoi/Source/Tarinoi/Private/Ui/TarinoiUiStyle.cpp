// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Ui/TarinoiUiStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

namespace TarinoiUiStyle
{
	const FLinearColor Background(0.02f, 0.02f, 0.03f, 0.92f);
	const FLinearColor Body(0.92f, 0.92f, 0.90f, 1.0f);
	const FLinearColor Speaker(0.45f, 0.70f, 1.0f, 1.0f);
	const FLinearColor Dimmed(0.45f, 0.45f, 0.45f, 1.0f);
	const FLinearColor SystemLine(0.95f, 0.78f, 0.30f, 1.0f);
	const FLinearColor Heading(1.0f, 1.0f, 1.0f, 1.0f);
	const float ColumnWidth = 760.0f;
	const float ContentWidth = ColumnWidth - 2.0f * 24.0f - 8.0f;

	FSlateFontInfo Font(int32 Size, bool bBold, bool bItalic)
	{
		const TCHAR* Face = bBold ? TEXT("Bold") : (bItalic ? TEXT("Italic") : TEXT("Regular"));
		return FCoreStyle::GetDefaultFontStyle(Face, Size);
	}

	UTextBlock* Text(UWidgetTree* Tree, const FString& Content, int32 Size, const FLinearColor& Color, bool bBold, bool bItalic)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Content));
		Block->SetFont(Font(Size, bBold, bItalic));
		Block->SetColorAndOpacity(FSlateColor(Color));
		// A fixed wrap width rather than auto-wrap: auto-wrapped text reports its height a frame
		// late, so the widget below it briefly overlaps. The quickstart column is a fixed width.
		Block->SetAutoWrapText(false);
		Block->SetWrapTextAt(ContentWidth);
		return Block;
	}

	UButton* Button(UWidgetTree* Tree, const FString& Label, UTextBlock** OutLabel)
	{
		UButton* Button = Tree->ConstructWidget<UButton>();
		UTextBlock* Text = TarinoiUiStyle::Text(Tree, Label, 16, FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
		Text->SetWrapTextAt(ContentWidth - 40.0f);
		if (UButtonSlot* Slot = Cast<UButtonSlot>(Button->AddChild(Text)))
		{
			Slot->SetHorizontalAlignment(HAlign_Left);
			Slot->SetPadding(FMargin(10.0f, 4.0f));
		}
		if (OutLabel)
		{
			*OutLabel = Text;
		}
		return Button;
	}
}
