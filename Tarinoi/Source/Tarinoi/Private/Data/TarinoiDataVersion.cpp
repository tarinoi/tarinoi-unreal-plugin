// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Data/TarinoiDataVersion.h"

#include "Tarinoi.h"

const TCHAR* FTarinoiDataVersion::SupportedVersion = TEXT("2.0.0");

namespace
{
	bool ParseComponent(const FString& Text, int32& OutValue)
	{
		OutValue = 0;
		if (Text.IsEmpty() || Text.Len() > 9)
		{
			return false;
		}

		for (const TCHAR Char : Text)
		{
			if (Char < TEXT('0') || Char > TEXT('9'))
			{
				return false;
			}
		}

		OutValue = FCString::Atoi(*Text);
		return true;
	}
}

bool FTarinoiDataVersion::TryParse(const FString& Version, int32& OutMajor, int32& OutMinor, int32& OutPatch)
{
	OutMajor = OutMinor = OutPatch = 0;

	TArray<FString> Parts;
	// CullEmpty = false, so "1..2" is three parts with an empty middle and fails to parse.
	Version.ParseIntoArray(Parts, TEXT("."), false);

	return Parts.Num() == 3
		&& ParseComponent(Parts[0], OutMajor)
		&& ParseComponent(Parts[1], OutMinor)
		&& ParseComponent(Parts[2], OutPatch);
}

FString FTarinoiDataVersion::Check(const FString& DataVersion)
{
	if (DataVersion.IsEmpty())
	{
		return FString();
	}

	if (const FString* Cached = Seen.Find(DataVersion))
	{
		return *Cached;
	}

	int32 Major, Minor, Patch;
	if (!TryParse(DataVersion, Major, Minor, Patch))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("DataVersion: unparseable data_version '%s', skipping the check"), *DataVersion);
		Seen.Add(DataVersion, FString());
		return FString();
	}

	int32 SupportedMajor, SupportedMinor, SupportedPatch;
	TryParse(SupportedVersion, SupportedMajor, SupportedMinor, SupportedPatch);

	FString Result;
	if (Major != SupportedMajor)
	{
		Result = FString::Printf(
			TEXT("MAJOR data format mismatch: this plugin reads %s, the synced data is %s. Update the plugin."),
			SupportedVersion, *DataVersion);
		UE_LOG(LogTarinoi, Error, TEXT("DataVersion: %s"), *Result);
	}
	else if (Minor != SupportedMinor)
	{
		UE_LOG(LogTarinoi, Warning, TEXT("DataVersion: minor data format mismatch: plugin reads %s, data is %s"),
			SupportedVersion, *DataVersion);
	}
	else if (Patch != SupportedPatch)
	{
		UE_LOG(LogTarinoi, Verbose, TEXT("DataVersion: patch data format mismatch: plugin reads %s, data is %s"),
			SupportedVersion, *DataVersion);
	}

	Seen.Add(DataVersion, Result);
	return Result;
}
