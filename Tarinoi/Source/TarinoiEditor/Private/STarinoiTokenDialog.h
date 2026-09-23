// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

/**
 * Asks for the API token and stores it outside the project (see FTarinoiCredentials). The field is
 * masked, and the stored token is never shown back: the dialog only says whether one is saved.
 */
class STarinoiTokenDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STarinoiTokenDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Opens the dialog as a modal window. */
	static void Open();

private:
	FReply OnSave();
	FReply OnClear();
	FReply OnCancel();
	void CloseWindow();

	TSharedPtr<SEditableTextBox> TokenBox;
};
