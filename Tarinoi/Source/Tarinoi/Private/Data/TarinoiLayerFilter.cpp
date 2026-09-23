// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Data/TarinoiLayerFilter.h"

#include "Data/TarinoiDatabase.h"
#include "TarinoiTypes.h"

namespace TarinoiLayerFilter
{
	const TCHAR* MainLayer = TEXT("tarinoi:main-project-layer");
	const TCHAR* BufferLayer = TEXT("tarinoi:main-project-layer.buffer");

	FString ActiveFilterSql(bool bCommittedOnly)
	{
		static const TCHAR* Active = TEXT("d.is_tombstone = 0 AND d.is_archived = 0 AND d.is_moved = 0");

		if (bCommittedOnly)
		{
			return FString::Printf(TEXT("d.layer_id = '%s' AND %s"), MainLayer, Active);
		}

		return FString::Printf(TEXT(
			"((d.layer_id = '%s' AND %s)"
			" OR (d.layer_id = '%s' AND %s"
			" AND NOT EXISTS (SELECT 1 FROM documents b"
			" WHERE b.document_id = d.document_id AND b.collection_id = d.collection_id AND b.layer_id = '%s')))"),
			BufferLayer, Active, MainLayer, Active, BufferLayer);
	}

	TArray<FTarinoiDocumentRow> Merge(const TArray<FTarinoiDocumentRow>& Rows, bool bCommittedOnly)
	{
		// Ids are case-sensitive nanoids; see TarinoiTypes.h. A newline cannot appear in either id.
		using FKey = FString;

		TArray<FKey> Order;
		TTarinoiMap<const FTarinoiDocumentRow*> Main;
		TTarinoiMap<const FTarinoiDocumentRow*> Buffer;
		TArray<FTarinoiDocumentRow> Other;

		for (const FTarinoiDocumentRow& Row : Rows)
		{
			const FKey Key = Row.DocumentId + TEXT("\n") + Row.CollectionId;
			const bool bMain = Row.LayerId == MainLayer;
			const bool bBuffer = Row.LayerId == BufferLayer;

			if (!bMain && !bBuffer)
			{
				if (Row.IsActive())
				{
					Other.Add(Row);
				}
				continue;
			}

			if (!Main.Contains(Key) && !Buffer.Contains(Key))
			{
				Order.Add(Key);
			}
			(bMain ? Main : Buffer).Add(Key, &Row);
		}

		TArray<FTarinoiDocumentRow> Result;
		for (const FKey& Key : Order)
		{
			if (!bCommittedOnly)
			{
				if (const FTarinoiDocumentRow* const* BufferRow = Buffer.Find(Key))
				{
					// An inactive buffer row suppresses the main row rather than falling back to it.
					if ((*BufferRow)->IsActive())
					{
						Result.Add(**BufferRow);
					}
					continue;
				}
			}

			if (const FTarinoiDocumentRow* const* MainRow = Main.Find(Key))
			{
				if ((*MainRow)->IsActive())
				{
					Result.Add(**MainRow);
				}
			}
		}

		Result.Append(Other);
		return Result;
	}
}

FTarinoiDocumentRow FTarinoiDocumentRow::FromSql(const FTarinoiSqlRow& Row)
{
	FTarinoiDocumentRow Out;
	Out.DocumentId = Row.GetString(TEXT("document_id"));
	Out.CollectionId = Row.GetString(TEXT("collection_id"));
	Out.DocumentType = Row.GetString(TEXT("document_type"));
	Out.LayerId = Row.GetString(TEXT("layer_id"));
	Out.Namespace = Row.GetString(TEXT("namespace"));
	Out.Identifier = Row.GetString(TEXT("identifier"));
	Out.UpdateKey = Row.GetInt64(TEXT("update_key"));
	Out.bTombstone = Row.GetInt64(TEXT("is_tombstone")) != 0;
	Out.bArchived = Row.GetInt64(TEXT("is_archived")) != 0;
	Out.bMoved = Row.GetInt64(TEXT("is_moved")) != 0;
	Out.Payload = Row.GetString(TEXT("payload"));
	return Out;
}
