// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiSettingsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "STarinoiTokenDialog.h"
#include "Sync/TarinoiCredentials.h"
#include "TarinoiSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TarinoiSettings"

void FTarinoiSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& Api = DetailBuilder.EditCategory(TEXT("API"));

	Api.AddCustomRow(LOCTEXT("TokenSearch", "API Token"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("TokenLabel", "API Token"))
		.ToolTipText(LOCTEXT("TokenTip", "Stored in your user folder, outside the project, so it is never committed or packaged."))
	]
	.ValueContent()
	.MinDesiredWidth(250.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([]()
			{
				return FTarinoiCredentials::Has(FTarinoiCredentials::ApiKeyName) ? LOCTEXT("Saved", "Saved") : LOCTEXT("NotSet", "Not set");
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Set", "Set..."))
			.OnClicked_Lambda([]()
			{
				STarinoiTokenDialog::Open();
				return FReply::Handled();
			})
		]
	];

	Api.AddCustomRow(LOCTEXT("ProjectSearch", "Project"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("ProjectLabel", "Project"))
	]
	.ValueContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text_Lambda([]()
		{
			const FString ProjectId = UTarinoiSettings::Get()->GetProjectId();
			return ProjectId.IsEmpty() ? LOCTEXT("NoProject", "Set the API path to choose a project") : FText::FromString(ProjectId);
		})
	];
}

#undef LOCTEXT_NAMESPACE
