// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * The content snapshot a game ships with, for offline mode.
 *
 * Offline mode plays content bundled at build time and never contacts the API. The snapshot is
 * Content/Tarinoi/<project>.db, staged into the build as a non-asset file (the editor's export
 * adds Content/Tarinoi to the packaging settings). SQLite needs an ordinary writable file, and
 * a packaged file may sit inside a pak, so the snapshot is copied out to Saved/Tarinoi before it
 * is opened.
 *
 * The copy is unconditional by default: in offline mode the snapshot is the source of truth, and
 * the local database only ever a cache of it.
 */
namespace TarinoiSnapshot
{
	/** The folder under Content/ that snapshots live in, and that packaging must stage. */
	TARINOI_API extern const TCHAR* ContentFolder;

	/** Where a project's snapshot lives inside the project (and the packaged build). */
	TARINOI_API FString SourcePath(const FString& ProjectId);

	/**
	 * Copies the snapshot into place, closing any open connection to the target first. Returns
	 * false when no snapshot exists, which offline mode must treat as a configuration error.
	 */
	TARINOI_API bool Seed(const FString& ProjectId, bool bOverwriteExisting = true);

	/** Seeds from an explicit source file. For tests. */
	TARINOI_API bool SeedFrom(const FString& SourceFile, const FString& ProjectId, bool bOverwriteExisting = true);
}
