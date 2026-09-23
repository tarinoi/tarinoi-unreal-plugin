// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Sync/TarinoiSnapshot.h"

#include "Data/TarinoiDatabase.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Tarinoi.h"

namespace TarinoiSnapshot
{
	const TCHAR* ContentFolder = TEXT("Tarinoi");

	FString SourcePath(const FString& ProjectId)
	{
		return FPaths::Combine(FPaths::ProjectContentDir(), ContentFolder, ProjectId + TEXT(".db"));
	}

	bool Seed(const FString& ProjectId, bool bOverwriteExisting)
	{
		return SeedFrom(SourcePath(ProjectId), ProjectId, bOverwriteExisting);
	}

	bool SeedFrom(const FString& SourceFile, const FString& ProjectId, bool bOverwriteExisting)
	{
		if (ProjectId.IsEmpty())
		{
			UE_LOG(LogTarinoi, Error, TEXT("Snapshot: cannot seed without a project id"));
			return false;
		}

		const FString Target = FTarinoiDatabase::PathForProject(ProjectId);
		IFileManager& Files = IFileManager::Get();

		if (!bOverwriteExisting && Files.FileExists(*Target))
		{
			return true;
		}

		if (!Files.FileExists(*SourceFile))
		{
			UE_LOG(LogTarinoi, Error,
				TEXT("Snapshot: offline mode is on, but there is no snapshot at '%s'. Run Tools > Tarinoi > Export Snapshot before packaging."),
				*SourceFile);
			return false;
		}

		// Whoever holds the old file has to let go of it before it is replaced.
		if (const TSharedPtr<FTarinoiDatabase> Open = FTarinoiDatabase::AcquireIfOpen(Target))
		{
			Open->Close();
		}

		// A stale journal would be applied to the new file.
		for (const TCHAR* Suffix : {TEXT("-journal"), TEXT("-wal"), TEXT("-shm")})
		{
			Files.Delete(*(Target + Suffix), false, true, true);
		}

		if (Files.Copy(*Target, *SourceFile, true, true) != COPY_OK)
		{
			UE_LOG(LogTarinoi, Error, TEXT("Snapshot: could not copy '%s' to '%s'"), *SourceFile, *Target);
			return false;
		}

		UE_LOG(LogTarinoi, Log, TEXT("Snapshot: seeded '%s' from the bundled snapshot (%lld KB)"),
			*ProjectId, Files.FileSize(*Target) / 1024);
		return true;
	}
}
