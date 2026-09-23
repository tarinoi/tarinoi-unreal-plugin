// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiSettings.h"

#include "Tarinoi.h"

UTarinoiSettings::UTarinoiSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Tarinoi");
}

FString UTarinoiSettings::ProjectIdFromApiPath(const FString& Path)
{
	FString Trimmed = Path.TrimStartAndEnd();
	while (Trimmed.EndsWith(TEXT("/")))
	{
		Trimmed.LeftChopInline(1);
	}

	if (Trimmed.EndsWith(TEXT("/documents"), ESearchCase::IgnoreCase))
	{
		Trimmed.LeftChopInline(10);
	}

	while (Trimmed.EndsWith(TEXT("/")))
	{
		Trimmed.LeftChopInline(1);
	}

	// Drop the scheme first, so the "//" of "https://" isn't read as path structure and a bare
	// host isn't mistaken for a project id.
	const int32 SchemeEnd = Trimmed.Find(TEXT("://"));
	if (SchemeEnd != INDEX_NONE)
	{
		Trimmed.RightChopInline(SchemeEnd + 3);
	}

	TArray<FString> Segments;
	Trimmed.ParseIntoArray(Segments, TEXT("/"), false);

	// A host alone is not a project path: there must be at least one segment beneath it.
	return Segments.Num() >= 2 ? Segments.Last() : FString();
}

void UTarinoiSettings::ApplyLogLevel() const
{
	ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
	switch (LogLevel)
	{
	case ETarinoiLogLevel::Verbose: Verbosity = ELogVerbosity::Verbose; break;
	case ETarinoiLogLevel::Log: Verbosity = ELogVerbosity::Log; break;
	case ETarinoiLogLevel::Warning: Verbosity = ELogVerbosity::Warning; break;
	case ETarinoiLogLevel::Error: Verbosity = ELogVerbosity::Error; break;
	case ETarinoiLogLevel::Off: Verbosity = ELogVerbosity::NoLogging; break;
	}
	LogTarinoi.SetVerbosity(Verbosity);
}

#if WITH_EDITOR
void UTarinoiSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	ApplyLogLevel();
}
#endif
