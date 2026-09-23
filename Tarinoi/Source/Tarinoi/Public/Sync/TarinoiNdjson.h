// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** One page of the documents feed: the documents, plus the cursor for the next page. */
struct TARINOI_API FTarinoiNdjsonPage
{
	TArray<TSharedPtr<FJsonObject>> Documents;

	/** The pagination cursor. Unset when the server signalled the last page, which ends the sync. */
	TOptional<FString> Cursor;
};

/**
 * Parses the newline-delimited JSON the documents endpoint returns.
 *
 * Every line is a JSON object. A line that is an object with exactly one key, "cursor", is the
 * pagination sentinel rather than a document; the exactly-one-key rule keeps a document that
 * happens to carry a cursor field from being swallowed.
 *
 * Unparseable lines are skipped rather than failing the page: one malformed record should not
 * cost the user an entire sync.
 */
namespace TarinoiNdjson
{
	TARINOI_API FTarinoiNdjsonPage Parse(const FString& Body);
}
