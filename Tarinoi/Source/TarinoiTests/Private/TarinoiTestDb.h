// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/TarinoiDatabase.h"
#include "Data/TarinoiLayerFilter.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"

/**
 * A throwaway database on a unique project id, deleted when the fixture goes away.
 *
 * Tests use real databases rather than mocks because the behaviour under test (the layer
 * merge, the composite-key upsert, json_extract) lives in SQL. A mock would only assert that we
 * wrote the SQL we wrote.
 */
struct FTarinoiTestDb
{
	/** A document to insert. Defaults describe an active card on the main layer. */
	struct FDoc
	{
		FString DocumentId;
		FString CollectionId = TEXT("col1");
		FString LayerId = TarinoiLayerFilter::MainLayer;
		FString DocumentType = TEXT("card");
		FString Payload;
		FString Identifier;
		int64 UpdateKey = 1;
		bool bTombstone = false;
		bool bArchived = false;
		bool bMoved = false;
	};

	FString ProjectId;
	TSharedPtr<FTarinoiDatabase> Db;

	explicit FTarinoiTestDb(bool bCommittedOnly = false)
	{
		ProjectId = TEXT("__test__") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		Db = FTarinoiDatabase::Acquire(ProjectId);
		check(Db.IsValid());
		Db->bCommittedOnly = bCommittedOnly;
	}

	~FTarinoiTestDb()
	{
		const FString Path = FTarinoiDatabase::PathForProject(ProjectId);
		if (Db.IsValid())
		{
			Db->Close();
		}
		Db.Reset();
		IFileManager::Get().Delete(*Path, false, true, true);
	}

	void Insert(const FDoc& Doc) const
	{
		const FString Payload = Doc.Payload.IsEmpty()
			? FString::Printf(TEXT("{\"id\":\"%s\",\"layer\":\"%s\"}"), *Doc.DocumentId, *Doc.LayerId)
			: Doc.Payload;

		Db->Execute(TEXT(
			"INSERT OR REPLACE INTO documents"
			" (document_id, collection_id, document_type, layer_id, namespace, identifier, update_key,"
			"  is_tombstone, is_archived, is_moved, payload)"
			" VALUES (?, ?, ?, ?, 'document', ?, ?, ?, ?, ?, ?)"),
			{Doc.DocumentId, Doc.CollectionId, Doc.DocumentType, Doc.LayerId,
			 FTarinoiSqlValue::TextOrNull(Doc.Identifier), Doc.UpdateKey,
			 Doc.bTombstone, Doc.bArchived, Doc.bMoved, Payload});
	}

	TArray<FString> VisibleDocumentIds() const
	{
		return Db->QueryStrings(*FString::Printf(
			TEXT("SELECT d.document_id FROM documents d WHERE %s ORDER BY d.document_id"), *Db->ActiveFilter()));
	}

	TArray<FString> VisiblePayloads() const
	{
		return Db->QueryStrings(*FString::Printf(
			TEXT("SELECT d.payload FROM documents d WHERE %s ORDER BY d.document_id"), *Db->ActiveFilter()));
	}
};

#endif
