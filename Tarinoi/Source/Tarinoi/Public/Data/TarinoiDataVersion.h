// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * Checks each synced document's data_version against the data format this plugin was built for.
 *
 * The Tarinoi data format promises a semver contract:
 *  - MAJOR: breaking; this plugin can no longer read the data. Fatal.
 *  - MINOR: additive and backward-compatible. Logged as a warning.
 *  - PATCH: cosmetic, no shape change. Logged at verbose level only.
 *
 * An empty data_version marks a pre-versioning legacy document and is not checked.
 *
 * One instance is meant to live for a single sync, so each distinct version is logged once.
 * A MAJOR mismatch keeps returning its error on every repeat rather than being swallowed.
 */
class TARINOI_API FTarinoiDataVersion
{
public:
	/** The data format this plugin reads. */
	static const TCHAR* SupportedVersion;

	/**
	 * Returns an empty string when the version is compatible, unversioned or unparseable, and
	 * the error message on a MAJOR mismatch, which callers must treat as fatal.
	 */
	FString Check(const FString& DataVersion);

	/** Parses "X.Y.Z" into its three unsigned components. Signs and whitespace are rejected. */
	static bool TryParse(const FString& Version, int32& OutMajor, int32& OutMinor, int32& OutPatch);

private:
	/** Version string -> "" (compatible) or the fatal error message. */
	TMap<FString, FString> Seen;
};
