// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FTarinoiSqlRow;

/**
 * One row of the documents table, as stored: before any layer merge.
 *
 * The primary key is (DocumentId, CollectionId, LayerId), so a logical document exists once per
 * layer. Payload stays as the raw JSON the service sent; callers parse it, because document
 * shapes vary by DocumentType.
 */
struct TARINOI_API FTarinoiDocumentRow
{
	FString DocumentId;
	FString CollectionId;
	FString DocumentType;
	FString LayerId;
	FString Namespace;
	FString Identifier;
	int64 UpdateKey = 0;
	bool bTombstone = false;
	bool bArchived = false;
	bool bMoved = false;
	FString Payload;

	/**
	 * Whether this row is live content. The service distinguishes deletion, archival and
	 * relocation, but none of the three should ever reach a player.
	 */
	bool IsActive() const { return !bTombstone && !bArchived && !bMoved; }

	/** Reads whichever of the documents columns the query selected. */
	static FTarinoiDocumentRow FromSql(const FTarinoiSqlRow& Row);
};

/** A dialogue entry point, as listed by a start-card picker. */
struct TARINOI_API FTarinoiStartCardRow
{
	FString DocumentId;
	FString CollectionId;
	FString Label;
};
