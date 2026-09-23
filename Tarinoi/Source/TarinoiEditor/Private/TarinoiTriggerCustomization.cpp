// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiTriggerCustomization.h"

#include "Components/TarinoiDialogueTrigger.h"
#include "Data/TarinoiDatabase.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "TarinoiSettings.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TarinoiTrigger"

TArray<TSharedPtr<FTarinoiStartOption>> FTarinoiTriggerCustomization::LoadStartOptions()
{
	TArray<TSharedPtr<FTarinoiStartOption>> Options;
	const FString ProjectId = UTarinoiSettings::Get()->GetProjectId();
	if (ProjectId.IsEmpty() || !FPaths::FileExists(FTarinoiDatabase::PathForProject(ProjectId)))
	{
		return Options;
	}

	const TSharedPtr<FTarinoiDatabase> Database = FTarinoiDatabase::Acquire(ProjectId);
	if (!Database.IsValid())
	{
		return Options;
	}

	// The collection's label comes from its manifest; the manifest's document id is the collection id.
	const FString Sql = FString::Printf(TEXT(
		"SELECT d.document_id, d.collection_id, json_extract(d.payload, '$.data.label') AS label,"
		" (SELECT json_extract(m.payload, '$.label') FROM documents m"
		"  WHERE m.document_id = d.collection_id AND m.document_type = 'collection-manifest' LIMIT 1) AS collection_label"
		" FROM documents d"
		" WHERE d.document_type = 'card' AND json_extract(d.payload, '$.base_ref') = 'start' AND %s"),
		*Database->ActiveFilter());

	Database->Query(*Sql, {}, [&Options](const FTarinoiSqlRow& Row)
	{
		TSharedPtr<FTarinoiStartOption> Option = MakeShared<FTarinoiStartOption>();
		Option->CollectionId = Row.GetString(TEXT("collection_id"));
		Option->CardId = Row.GetString(TEXT("document_id"));
		const FString Collection = Row.GetString(TEXT("collection_label"));
		const FString Label = Row.GetString(TEXT("label"));
		Option->Label = FString::Printf(TEXT("%s: %s"), Collection.IsEmpty() ? *Option->CollectionId : *Collection,
			Label.IsEmpty() ? *Option->CardId : *Label);
		Options.Add(Option);
	});

	Options.Sort([](const TSharedPtr<FTarinoiStartOption>& A, const TSharedPtr<FTarinoiStartOption>& B) { return A->Label < B->Label; });
	return Options;
}

void FTarinoiTriggerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TSharedRef<IPropertyHandle> Collection = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UTarinoiDialogueTrigger, CollectionId));
	const TSharedRef<IPropertyHandle> Card = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UTarinoiDialogueTrigger, CardId));

	TSharedRef<TArray<TSharedPtr<FTarinoiStartOption>>> Options = MakeShared<TArray<TSharedPtr<FTarinoiStartOption>>>(LoadStartOptions());

	auto CurrentLabel = [Collection, Card, Options]()
	{
		FString CollectionId, CardId;
		Collection->GetValue(CollectionId);
		Card->GetValue(CardId);
		if (CardId.IsEmpty())
		{
			return Options->Num() > 0 ? LOCTEXT("Pick", "Pick a start card") : LOCTEXT("NoneSynced", "Nothing synced yet: Tools > Tarinoi > Sync");
		}
		for (const TSharedPtr<FTarinoiStartOption>& Option : *Options)
		{
			if (Option->CollectionId == CollectionId && Option->CardId == CardId)
			{
				return FText::FromString(Option->Label);
			}
		}
		return FText::Format(LOCTEXT("Unknown", "{0} (not in the synced content)"), FText::FromString(CardId));
	};

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Tarinoi"));
	Category.AddCustomRow(LOCTEXT("StartCard", "Start Card"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("StartCard", "Start Card"))
		.ToolTipText(LOCTEXT("StartCardTip", "The synced entry points. Picking one sets Collection Id and Card Id."))
	]
	.ValueContent()
	.MinDesiredWidth(300.0f)
	[
		SNew(SComboBox<TSharedPtr<FTarinoiStartOption>>)
		.OptionsSource(&Options.Get())
		.OnGenerateWidget_Lambda([](TSharedPtr<FTarinoiStartOption> Option)
		{
			return SNew(STextBlock).Text(FText::FromString(Option->Label));
		})
		.OnSelectionChanged_Lambda([Collection, Card, Options](TSharedPtr<FTarinoiStartOption> Option, ESelectInfo::Type)
		{
			if (Option.IsValid())
			{
				Collection->SetValue(Option->CollectionId);
				Card->SetValue(Option->CardId);
			}
		})
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda(CurrentLabel)
		]
	];
}

#undef LOCTEXT_NAMESPACE
