// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "STarinoiTokenDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Sync/TarinoiCredentials.h"
#include "TarinoiEditorActions.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TarinoiTokenDialog"

void STarinoiTokenDialog::Construct(const FArguments& InArgs)
{
	const bool bSaved = FTarinoiCredentials::Has(FTarinoiCredentials::ApiKeyName);

	ChildSlot
	[
		SNew(SBox)
		.Padding(16.0f)
		.WidthOverride(480.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("Explain", "Paste an API token from your Tarinoi project (app menu > Integrate, or Integrations > API Keys). It is stored in your user folder, outside this project, so it is never committed or packaged."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.Text(bSaved ? LOCTEXT("Saved", "A token is saved. Paste a new one to replace it.") : LOCTEXT("NotSet", "No token is saved yet."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(TokenBox, SEditableTextBox)
				.IsPassword(true)
				.HintText(LOCTEXT("Hint", "API token"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 16.0f, 0.0f, 0.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Clear", "Clear Saved Token"))
					.IsEnabled(bSaved)
					.OnClicked(this, &STarinoiTokenDialog::OnClear)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(this, &STarinoiTokenDialog::OnCancel)
				]
				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.Text(LOCTEXT("Save", "Save"))
					.OnClicked(this, &STarinoiTokenDialog::OnSave)
				]
			]
		]
	];
}

void STarinoiTokenDialog::Open()
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "Tarinoi API Token"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(STarinoiTokenDialog)
		];

	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
}

FReply STarinoiTokenDialog::OnSave()
{
	const FString Token = TokenBox->GetText().ToString().TrimStartAndEnd();
	if (Token.IsEmpty())
	{
		TarinoiEditorActions::Notify(TEXT("Tarinoi: the token was empty, so nothing was saved."), false);
		return FReply::Handled();
	}

	if (FTarinoiCredentials::Write(FTarinoiCredentials::ApiKeyName, Token))
	{
		TarinoiEditorActions::Notify(TEXT("Tarinoi: API token saved."), true);
	}
	CloseWindow();
	return FReply::Handled();
}

FReply STarinoiTokenDialog::OnClear()
{
	FTarinoiCredentials::Clear(FTarinoiCredentials::ApiKeyName);
	TarinoiEditorActions::Notify(TEXT("Tarinoi: API token cleared."), true);
	CloseWindow();
	return FReply::Handled();
}

FReply STarinoiTokenDialog::OnCancel()
{
	CloseWindow();
	return FReply::Handled();
}

void STarinoiTokenDialog::CloseWindow()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
