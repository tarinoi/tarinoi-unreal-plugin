// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Data/TarinoiDocumentStore.h"

#include "Data/TarinoiDatabase.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"

void UTarinoiDocumentStore::Setup(const TSharedPtr<FTarinoiDatabase>& InDatabase, const ITarinoiEntityCache* InEntities)
{
	Database = InDatabase;
	Entities = InEntities;
}

TSharedPtr<FJsonObject> UTarinoiDocumentStore::GetEntity(const FString& Identifier)
{
	return Entities ? Entities->GetEntityPayload(Identifier) : nullptr;
}

bool UTarinoiSqliteDocumentStore::IsUsable() const
{
	return Database.IsValid() && Database->IsOpen();
}

TSharedPtr<FJsonObject> UTarinoiSqliteDocumentStore::ParsePayload(const FString& Payload, const FString& DocumentId)
{
	if (Payload.IsEmpty())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Parsed = TarinoiJson::Parse(Payload);
	if (!Parsed.IsValid())
	{
		UE_LOG(LogTarinoi, Error, TEXT("DocumentStore: document '%s' has an unreadable payload"), *DocumentId);
	}
	return Parsed;
}

TSharedPtr<FJsonObject> UTarinoiSqliteDocumentStore::GetDocument(const FString& DocumentId, const FString& CollectionId)
{
	if (!IsUsable() || DocumentId.IsEmpty())
	{
		return nullptr;
	}

	FString Payload;
	bool bFound = false;
	const auto Take = [&](const FTarinoiSqlRow& Row)
	{
		if (!bFound)
		{
			Payload = Row.GetString(TEXT("payload"));
			bFound = true;
		}
	};

	if (CollectionId.IsEmpty())
	{
		const FString Sql = FString::Printf(TEXT("SELECT d.payload FROM documents d WHERE d.document_id = ? AND %s"), *Database->ActiveFilter());
		Database->Query(*Sql, {DocumentId}, Take);
	}
	else
	{
		const FString Sql = FString::Printf(TEXT("SELECT d.payload FROM documents d WHERE d.document_id = ? AND d.collection_id = ? AND %s"), *Database->ActiveFilter());
		Database->Query(*Sql, {DocumentId, CollectionId}, Take);
	}

	return bFound ? ParsePayload(Payload, DocumentId) : nullptr;
}

TSharedPtr<FJsonObject> UTarinoiSqliteDocumentStore::LoadCard(const FString& CollectionId, const FString& CardId)
{
	if (CardId.IsEmpty())
	{
		return nullptr;
	}
	return GetDocument(CardId, CollectionId);
}

bool UTarinoiSqliteDocumentStore::LocateCard(const FString& CardId, FString& OutCollectionId, TSharedPtr<FJsonObject>& OutCard)
{
	if (!IsUsable() || CardId.IsEmpty())
	{
		return false;
	}

	FString Payload;
	bool bFound = false;
	const FString Sql = FString::Printf(
		TEXT("SELECT d.collection_id, d.payload FROM documents d WHERE d.document_id = ? AND d.document_type = 'card' AND %s"),
		*Database->ActiveFilter());
	Database->Query(*Sql, {CardId}, [&](const FTarinoiSqlRow& Row)
	{
		if (!bFound)
		{
			OutCollectionId = Row.GetString(TEXT("collection_id"));
			Payload = Row.GetString(TEXT("payload"));
			bFound = true;
		}
	});

	if (!bFound)
	{
		return false;
	}

	OutCard = ParsePayload(Payload, CardId);
	return OutCard.IsValid();
}

TArray<FTarinoiStartCardRow> UTarinoiSqliteDocumentStore::QueryStartCards()
{
	TArray<FTarinoiStartCardRow> Cards;
	if (!IsUsable())
	{
		return Cards;
	}

	// document_type = 'card' matters: the start card *template* also has base_ref 'start'.
	const FString Sql = FString::Printf(TEXT(
		"SELECT d.document_id, d.collection_id, json_extract(d.payload, '$.data.label') AS label"
		" FROM documents d"
		" WHERE d.document_type = 'card' AND json_extract(d.payload, '$.base_ref') = 'start' AND %s"),
		*Database->ActiveFilter());

	Database->Query(*Sql, {}, [&Cards](const FTarinoiSqlRow& Row)
	{
		FTarinoiStartCardRow& Card = Cards.AddDefaulted_GetRef();
		Card.DocumentId = Row.GetString(TEXT("document_id"));
		Card.CollectionId = Row.GetString(TEXT("collection_id"));
		Card.Label = Row.GetString(TEXT("label"));
	});
	return Cards;
}
