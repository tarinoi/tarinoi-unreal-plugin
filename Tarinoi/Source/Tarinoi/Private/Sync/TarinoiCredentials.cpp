// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Sync/TarinoiCredentials.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tarinoi.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

const TCHAR* FTarinoiCredentials::ApiKeyName = TEXT("api_key");

FString& FTarinoiCredentials::PathOverride()
{
	static FString Override;
	return Override;
}

void FTarinoiCredentials::SetFilePathOverride(const FString& Path)
{
	PathOverride() = Path;
}

FString FTarinoiCredentials::FilePath()
{
	if (!PathOverride().IsEmpty())
	{
		return PathOverride();
	}

	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Tarinoi"), FApp::GetProjectName(), TEXT("credentials"));
}

namespace
{
	bool MatchesKey(const FString& Line, const FString& Prefix)
	{
		return Line.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}

	TArray<FString> ReadLines(const FString& Path)
	{
		TArray<FString> Lines;
		if (FPaths::FileExists(Path))
		{
			FFileHelper::LoadFileToStringArray(Lines, *Path);
		}
		return Lines;
	}

	bool WriteLines(const FString& Path, const TArray<FString>& Lines)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		if (!FFileHelper::SaveStringArrayToFile(Lines, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogTarinoi, Error, TEXT("Credentials: could not write '%s'"), *Path);
			return false;
		}

#if PLATFORM_MAC || PLATFORM_LINUX
		// Readable by the owner only. The file is outside the project, but a token is a secret.
		chmod(TCHAR_TO_UTF8(*Path), S_IRUSR | S_IWUSR);
#endif
		return true;
	}
}

FString FTarinoiCredentials::Read(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return FString();
	}

	const FString Prefix = Key + TEXT("=");
	for (const FString& Raw : ReadLines(FilePath()))
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (MatchesKey(Line, Prefix))
		{
			return Line.Mid(Prefix.Len()).TrimStartAndEnd();
		}
	}
	return FString();
}

bool FTarinoiCredentials::Write(const FString& Key, const FString& Value)
{
	if (Key.IsEmpty())
	{
		return false;
	}

	const FString Prefix = Key.ToLower() + TEXT("=");
	TArray<FString> Out;
	bool bReplaced = false;

	for (const FString& Raw : ReadLines(FilePath()))
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			continue;
		}

		if (MatchesKey(Line, Prefix))
		{
			Out.Add(Prefix + Value.TrimStartAndEnd());
			bReplaced = true;
		}
		else
		{
			Out.Add(Line);
		}
	}

	if (!bReplaced)
	{
		Out.Add(Prefix + Value.TrimStartAndEnd());
	}

	return WriteLines(FilePath(), Out);
}

bool FTarinoiCredentials::Clear(const FString& Key)
{
	if (Key.IsEmpty() || !FPaths::FileExists(FilePath()))
	{
		return false;
	}

	const FString Prefix = Key + TEXT("=");
	TArray<FString> Kept;
	bool bRemoved = false;

	for (const FString& Raw : ReadLines(FilePath()))
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty())
		{
			continue;
		}

		if (MatchesKey(Line, Prefix))
		{
			bRemoved = true;
		}
		else
		{
			Kept.Add(Line);
		}
	}

	WriteLines(FilePath(), Kept);
	return bRemoved;
}
