// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Data/TarinoiDocumentRow.h"

/**
 * The single source of truth for Tarinoi's two-layer document merge.
 *
 * Authored content arrives on two layers. The main layer holds committed content; the buffer
 * layer holds edits that are not committed yet. The rules:
 *  - An active buffer row overrides the main row.
 *  - An inactive buffer row (tombstoned, archived or moved) suppresses the main row entirely:
 *    it is a deletion marker, not a reason to fall back.
 *  - With no buffer row at all, the main row shows if it is active.
 *
 * Both forms live here, SQL for queries and an in-memory merge for callers that read across
 * layers, sharing one definition of "active". An earlier implementation kept them apart and
 * they drifted; the tests cross-check the two over every combination.
 */
namespace TarinoiLayerFilter
{
	TARINOI_API extern const TCHAR* MainLayer;
	TARINOI_API extern const TCHAR* BufferLayer;

	/**
	 * A SQL WHERE fragment (no leading AND) selecting visible documents. The documents table
	 * must be aliased as d. bCommittedOnly ignores the buffer layer entirely: "what a player
	 * would see".
	 */
	TARINOI_API FString ActiveFilterSql(bool bCommittedOnly);

	/**
	 * The same rules, in memory. Input order is preserved: each surviving document appears at
	 * the position of its first row. Rows on an unrecognised layer pass through when active, so a
	 * future third layer degrades to visible rather than silently vanishing.
	 */
	TARINOI_API TArray<FTarinoiDocumentRow> Merge(const TArray<FTarinoiDocumentRow>& Rows, bool bCommittedOnly);
}
