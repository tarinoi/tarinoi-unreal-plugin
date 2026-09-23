// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UTextBlock;
class UButton;
class UWidgetTree;
class UObject;

/**
 * The quickstart interface's few colours and helpers. Deliberately plain: this is a debug view for
 * authors, not something to ship, and your game's UI replaces it.
 */
namespace TarinoiUiStyle
{
	TARINOI_API extern const FLinearColor Background;
	TARINOI_API extern const FLinearColor Body;
	TARINOI_API extern const FLinearColor Speaker;
	TARINOI_API extern const FLinearColor Dimmed;
	TARINOI_API extern const FLinearColor SystemLine;
	TARINOI_API extern const FLinearColor Heading;

	/** The quickstart column's width, and the width text wraps at inside its padding. */
	TARINOI_API extern const float ColumnWidth;
	TARINOI_API extern const float ContentWidth;

	TARINOI_API FSlateFontInfo Font(int32 Size, bool bBold = false, bool bItalic = false);

	/** A wrapping text block. */
	TARINOI_API UTextBlock* Text(UWidgetTree* Tree, const FString& Content, int32 Size, const FLinearColor& Color, bool bBold = false, bool bItalic = false);

	/** A button with a text label; returns the label through OutLabel if asked. */
	TARINOI_API UButton* Button(UWidgetTree* Tree, const FString& Label, UTextBlock** OutLabel = nullptr);
}
