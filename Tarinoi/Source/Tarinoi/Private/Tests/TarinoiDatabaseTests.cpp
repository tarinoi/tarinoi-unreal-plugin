// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/Paths.h"
#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"

BEGIN_DEFINE_SPEC(FTarinoiDatabaseSpec, "Tarinoi.Data.Database",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiDatabaseSpec)

void FTarinoiDatabaseSpec::Define()
{
	It("creates the file, tables and indexes on open", [this]()
	{
		FTarinoiTestDb Fixture;
		TestTrue("file exists", FPaths::FileExists(FTarinoiDatabase::PathForProject(Fixture.ProjectId)));

		const TArray<FString> Tables = Fixture.Db->QueryStrings(TEXT("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"));
		TarinoiTest::Strings(*this, TEXT("tables"), Tables, {TEXT("collections"), TEXT("documents"), TEXT("metadata")});

		const TArray<FString> Indexes = Fixture.Db->QueryStrings(TEXT("SELECT name FROM sqlite_master WHERE type = 'index' AND name LIKE 'idx_%' ORDER BY name"));
		TarinoiTest::Strings(*this, TEXT("indexes"), Indexes,
			{TEXT("idx_documents_collection"), TEXT("idx_documents_identifier"), TEXT("idx_documents_update_key")});
	});

	It("records the current schema version", [this]()
	{
		FTarinoiTestDb Fixture;
		TestEqual("version", Fixture.Db->ReadMeta(FTarinoiDatabase::SchemaVersionKey), FString::FromInt(FTarinoiDatabase::SchemaVersion));
	});

	It("refuses to open without a project id", [this]()
	{
		AddExpectedError(TEXT("without a project id"));
		TestFalse("null", FTarinoiDatabase::Acquire(FString()).IsValid());
	});

	It("hands out one shared connection per file", [this]()
	{
		FTarinoiTestDb Fixture;
		const TSharedPtr<FTarinoiDatabase> Again = FTarinoiDatabase::Acquire(Fixture.ProjectId);
		TestTrue("same object", Again == Fixture.Db);
	});

	It("round-trips metadata and deletes only the named keys", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Db->WriteMeta(TEXT("a"), TEXT("1"));
		Fixture.Db->WriteMeta(TEXT("b"), TEXT("2"));
		Fixture.Db->WriteMeta(TEXT("a"), TEXT("3"));
		TestEqualSensitive("overwritten", Fixture.Db->ReadMeta(TEXT("a")), FString(TEXT("3")));
		TestEqual("absent", Fixture.Db->ReadMeta(TEXT("nope")), FString());

		Fixture.Db->DeleteMeta({TEXT("a")});
		TestEqual("deleted", Fixture.Db->ReadMeta(TEXT("a")), FString());
		TestEqual("kept", Fixture.Db->ReadMeta(TEXT("b")), FString(TEXT("2")));
	});

	It("keeps content across a close and reopen", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Insert({.DocumentId = TEXT("d1")});
		Fixture.Db->Close();

		Fixture.Db = FTarinoiDatabase::Acquire(Fixture.ProjectId);
		TarinoiTest::Strings(*this, TEXT("still there"), Fixture.VisibleDocumentIds(), {TEXT("d1")});
	});

	It("drops content but keeps metadata when the stored schema is older", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Insert({.DocumentId = TEXT("d1")});
		Fixture.Db->WriteMeta(TEXT("keep"), TEXT("yes"));
		Fixture.Db->WriteMeta(FTarinoiDatabase::SchemaVersionKey, TEXT("2"));
		Fixture.Db->Close();

		Fixture.Db = FTarinoiDatabase::Acquire(Fixture.ProjectId);
		TestEqual("content gone", Fixture.VisibleDocumentIds().Num(), 0);
		TestEqual("metadata kept", Fixture.Db->ReadMeta(TEXT("keep")), FString(TEXT("yes")));
		TestEqual("version bumped", Fixture.Db->ReadMeta(FTarinoiDatabase::SchemaVersionKey), FString::FromInt(FTarinoiDatabase::SchemaVersion));
	});

	It("logs and returns nothing on a closed database", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Db->Close();
		AddExpectedError(TEXT("closed database"), EAutomationExpectedErrorFlags::Contains, 0);
		TestEqual("query", Fixture.Db->QueryStrings(TEXT("SELECT 1")).Num(), 0);
		TestEqual("execute", Fixture.Db->Execute(TEXT("DELETE FROM documents")), -1);
		TestEqual("meta", Fixture.Db->ReadMeta(TEXT("x")), FString());
	});

	It("logs malformed SQL rather than failing hard", [this]()
	{
		FTarinoiTestDb Fixture;
		AddExpectedError(TEXT("syntax error"), EAutomationExpectedErrorFlags::Contains, 0);
		TestEqual("query", Fixture.Db->QueryStrings(TEXT("SELEKT nonsense")).Num(), 0);
		TestEqual("execute", Fixture.Db->Execute(TEXT("DELEET FROM documents")), -1);
	});

	It("reports how many rows a statement changed", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Insert({.DocumentId = TEXT("d1")});
		Fixture.Insert({.DocumentId = TEXT("d2")});
		TestEqual("deleted two", Fixture.Db->Execute(TEXT("DELETE FROM documents")), 2);
	});

	It("rolls a transaction back when a statement fails", [this]()
	{
		FTarinoiTestDb Fixture;
		AddExpectedError(TEXT("no such table|rolled back"), EAutomationExpectedErrorFlags::Contains, 0);
		const bool bCommitted = Fixture.Db->RunInTransaction([&Fixture]()
		{
			Fixture.Insert({.DocumentId = TEXT("d1")});
			Fixture.Db->Execute(TEXT("INSERT INTO missing_table VALUES (1)"));
			return true;
		});
		TestFalse("not committed", bCommitted);
		TestEqual("nothing written", Fixture.VisibleDocumentIds().Num(), 0);
	});

	It("rolls a transaction back when the body says so", [this]()
	{
		FTarinoiTestDb Fixture;
		AddExpectedError(TEXT("rolled back"));
		TestFalse("not committed", Fixture.Db->RunInTransaction([&Fixture]()
		{
			Fixture.Insert({.DocumentId = TEXT("d1")});
			return false;
		}));
		TestEqual("nothing written", Fixture.VisibleDocumentIds().Num(), 0);
	});

	It("commits a successful transaction", [this]()
	{
		FTarinoiTestDb Fixture;
		TestTrue("committed", Fixture.Db->RunInTransaction([&Fixture]()
		{
			Fixture.Insert({.DocumentId = TEXT("d1")});
			Fixture.Insert({.DocumentId = TEXT("d2")});
			return true;
		}));
		TarinoiTest::Strings(*this, TEXT("both written"), Fixture.VisibleDocumentIds(), {TEXT("d1"), TEXT("d2")});
	});

	It("follows bCommittedOnly in its active filter", [this]()
	{
		FTarinoiTestDb Fixture;
		Fixture.Insert({.DocumentId = TEXT("d1"), .LayerId = TarinoiLayerFilter::BufferLayer});
		TestEqual("buffer visible", Fixture.VisibleDocumentIds().Num(), 1);
		Fixture.Db->bCommittedOnly = true;
		TestEqual("buffer hidden", Fixture.VisibleDocumentIds().Num(), 0);
	});
}

#endif
