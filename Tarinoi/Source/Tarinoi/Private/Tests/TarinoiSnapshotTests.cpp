// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sync/TarinoiSnapshot.h"
#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"

BEGIN_DEFINE_SPEC(FTarinoiSnapshotSpec, "Tarinoi.Sync.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FString SourceFile;
	FString ProjectId;
END_DEFINE_SPEC(FTarinoiSnapshotSpec)

void FTarinoiSnapshotSpec::Define()
{
	BeforeEach([this]()
	{
		// Build a real snapshot: a database with one document, closed and moved aside.
		FTarinoiTestDb Source;
		Source.Insert({.DocumentId = TEXT("from-snapshot")});
		Source.Db->Close();
		SourceFile = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("Tarinoi"), Source.ProjectId + TEXT("-snapshot.db"));
		IFileManager::Get().Copy(*SourceFile, *FTarinoiDatabase::PathForProject(Source.ProjectId));

		ProjectId = TEXT("__test__seed") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	});

	AfterEach([this]()
	{
		if (const TSharedPtr<FTarinoiDatabase> Open = FTarinoiDatabase::AcquireIfOpen(FTarinoiDatabase::PathForProject(ProjectId)))
		{
			Open->Close();
		}
		IFileManager::Get().Delete(*SourceFile);
		IFileManager::Get().Delete(*FTarinoiDatabase::PathForProject(ProjectId));
	});

	It("copies the snapshot into place and opens as a working database", [this]()
	{
		TestTrue("seeded", TarinoiSnapshot::SeedFrom(SourceFile, ProjectId));
		const TSharedPtr<FTarinoiDatabase> Db = FTarinoiDatabase::Acquire(ProjectId);
		TarinoiTest::Strings(*this, TEXT("content"),
			Db->QueryStrings(*FString::Printf(TEXT("SELECT d.document_id FROM documents d WHERE %s"), *Db->ActiveFilter())),
			{TEXT("from-snapshot")});
	});

	It("fails with an actionable error when there is no snapshot", [this]()
	{
		AddExpectedError(TEXT("Export Snapshot"));
		TestFalse("seeded", TarinoiSnapshot::SeedFrom(SourceFile + TEXT(".missing"), ProjectId));
	});

	It("fails without a project id", [this]()
	{
		AddExpectedError(TEXT("without a project id"));
		TestFalse("seeded", TarinoiSnapshot::SeedFrom(SourceFile, FString()));
	});

	It("overwrites an existing database, even one that is open", [this]()
	{
		const TSharedPtr<FTarinoiDatabase> Existing = FTarinoiDatabase::Acquire(ProjectId);
		Existing->Execute(TEXT("INSERT INTO documents (document_id, collection_id, document_type, layer_id, update_key, payload) VALUES ('stale', 'c', 'card', 'tarinoi:main-project-layer', 1, '{}')"));

		TestTrue("seeded", TarinoiSnapshot::SeedFrom(SourceFile, ProjectId));
		TestFalse("old connection closed", Existing->IsOpen());

		const TSharedPtr<FTarinoiDatabase> Db = FTarinoiDatabase::Acquire(ProjectId);
		TarinoiTest::Strings(*this, TEXT("replaced"), Db->QueryStrings(TEXT("SELECT document_id FROM documents")), {TEXT("from-snapshot")});
	});

	It("can leave an existing database alone", [this]()
	{
		FTarinoiDatabase::Acquire(ProjectId)->Close();
		TestTrue("skipped", TarinoiSnapshot::SeedFrom(SourceFile, ProjectId, false));
		TestEqual("still empty", FTarinoiDatabase::Acquire(ProjectId)->QueryStrings(TEXT("SELECT document_id FROM documents")).Num(), 0);
	});

	It("removes a stale journal so it cannot be applied to the new file", [this]()
	{
		const FString Journal = FTarinoiDatabase::PathForProject(ProjectId) + TEXT("-journal");
		FFileHelper::SaveStringToFile(TEXT("garbage"), *Journal);
		TestTrue("seeded", TarinoiSnapshot::SeedFrom(SourceFile, ProjectId));
		TestFalse("journal gone", FPaths::FileExists(Journal));
	});
}

#endif
