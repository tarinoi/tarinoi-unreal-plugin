// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * Stores the Tarinoi API token outside the project directory.
 *
 * The file lives in the per-user application settings folder (on macOS
 * ~/Library/Application Support/Tarinoi/<Project>/credentials), never under the project, so a
 * token cannot be committed to version control or packaged into a build. That placement is the
 * whole point; the format is deliberately boring: one key=value per line, and unknown keys are
 * preserved on write so a hand-added entry survives.
 */
class TARINOI_API FTarinoiCredentials
{
public:
	static const TCHAR* ApiKeyName;

	/** Where the credentials file lives for this project. */
	static FString FilePath();

	/** A stored credential, or "" when the file or key is absent. Keys match case-insensitively. */
	static FString Read(const FString& Key);

	/** Stores a credential, leaving other keys untouched. */
	static bool Write(const FString& Key, const FString& Value);

	/** Removes a credential. Returns true if one was present. */
	static bool Clear(const FString& Key);

	static bool Has(const FString& Key) { return !Read(Key).IsEmpty(); }

	/** Redirects the file, for tests. Pass an empty string to restore the default. */
	static void SetFilePathOverride(const FString& Path);

private:
	static FString& PathOverride();
};
