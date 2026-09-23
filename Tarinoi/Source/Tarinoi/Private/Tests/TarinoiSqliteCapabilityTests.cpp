// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

// These pin down what the engine's SQLiteCore build can do, because the data layer depends
// on features that are compile-time options. An engine upgrade that changes one of them
// fails here rather than somewhere confusing.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"

namespace TarinoiSqliteCapability
{
	FString TempDatabasePath()
	{
		return FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("Tarinoi"),
			FString::Printf(TEXT("capability-%s.db"), *FGuid::NewGuid().ToString()));
	}

	FString QueryString(FSQLiteDatabase& Db, const TCHAR* Sql)
	{
		FString Value;
		FSQLitePreparedStatement Statement(Db, Sql);
		if (Statement.IsValid() && Statement.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			Statement.GetColumnValueByIndex(0, Value);
		}
		return Value;
	}

	int64 QueryInt(FSQLiteDatabase& Db, const TCHAR* Sql)
	{
		int64 Value = -1;
		FSQLitePreparedStatement Statement(Db, Sql);
		if (Statement.IsValid() && Statement.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			Statement.GetColumnValueByIndex(0, Value);
		}
		return Value;
	}
}

BEGIN_DEFINE_SPEC(FTarinoiSqliteCapabilitySpec, "Tarinoi.Data.SqliteCapability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FString Path;
	FSQLiteDatabase Db;
END_DEFINE_SPEC(FTarinoiSqliteCapabilitySpec)

void FTarinoiSqliteCapabilitySpec::Define()
{
	using namespace TarinoiSqliteCapability;

	BeforeEach([this]()
	{
		Path = TempDatabasePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		TestTrue("opened", Db.Open(*Path, ESQLiteDatabaseOpenMode::ReadWriteCreate));
	});

	AfterEach([this]()
	{
		Db.Close();
		IFileManager::Get().Delete(*Path);
	});

	It("loads the native library", [this]()
	{
		const FString Version = QueryString(Db, TEXT("SELECT sqlite_version()"));
		AddInfo(FString::Printf(TEXT("SQLite %s"), *Version));
		TestFalse("has a version", Version.IsEmpty());
	});

	It("has json_extract", [this]()
	{
		TestEqual("extract", QueryString(Db, TEXT("SELECT json_extract('{\"data\":{\"label\":\"Hi\"}}', '$.data.label')")), FString(TEXT("Hi")));
		TestEqual("bool as int", QueryInt(Db, TEXT("SELECT json_extract('{\"dialog_capable\":true}', '$.dialog_capable')")), (int64)1);
	});

	It("reports which journal mode WAL requests get", [this]()
	{
		// Recorded, not required: the data layer keeps one connection and does not need WAL.
		const FString Mode = QueryString(Db, TEXT("PRAGMA journal_mode=WAL"));
		AddInfo(FString::Printf(TEXT("journal_mode=WAL returned '%s'"), *Mode));
		TestFalse("answered", Mode.IsEmpty());
	});

	It("keeps one row per layer under a composite key, and replaces in place", [this]()
	{
		TestTrue("create", Db.Execute(TEXT(
			"CREATE TABLE documents (document_id TEXT NOT NULL, collection_id TEXT NOT NULL, layer_id TEXT NOT NULL,"
			" payload TEXT NOT NULL, PRIMARY KEY (document_id, collection_id, layer_id))")));
		TestTrue("main", Db.Execute(TEXT("INSERT OR REPLACE INTO documents VALUES ('d','c','main','a')")));
		TestTrue("buffer", Db.Execute(TEXT("INSERT OR REPLACE INTO documents VALUES ('d','c','buffer','b')")));
		TestTrue("replace main", Db.Execute(TEXT("INSERT OR REPLACE INTO documents VALUES ('d','c','main','c')")));

		TestEqual("two rows", QueryInt(Db, TEXT("SELECT COUNT(*) FROM documents")), (int64)2);
		TestEqual("buffer untouched", QueryString(Db, TEXT("SELECT payload FROM documents WHERE layer_id='buffer'")), FString(TEXT("b")));
		TestEqual("main replaced", QueryString(Db, TEXT("SELECT payload FROM documents WHERE layer_id='main'")), FString(TEXT("c")));
	});

	It("binds parameters as data, not SQL", [this]()
	{
		TestTrue("create", Db.Execute(TEXT("CREATE TABLE t (v TEXT)")));
		const FString Hostile = TEXT("'); DROP TABLE t; --ä☃");
		{
			FSQLitePreparedStatement Insert(Db, TEXT("INSERT INTO t VALUES (?)"));
			TestTrue("bind", Insert.SetBindingValueByIndex(1, Hostile));
			TestTrue("exec", Insert.Execute());
		}
		TestEqual("round trip", QueryString(Db, TEXT("SELECT v FROM t")), Hostile);
	});

	It("refuses a second connection to the same file", [this]()
	{
		// Under the engine's default bCompileCustomSQLitePlatform, SQLite runs on the Unreal
		// file layer with no shared memory or granular locks, so only one connection can hold
		// a database file. The data layer is built around that: one shared connection per
		// database, used from the game thread. If this starts passing, that constraint has
		// lifted and a second, writer connection becomes possible again.
		TestTrue("create", Db.Execute(TEXT("CREATE TABLE t (v INTEGER)")));

		AddExpectedMessage(TEXT("Failed to open database"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		FSQLiteDatabase Second;
		TestFalse("second connection opens", Second.Open(*Path, ESQLiteDatabaseOpenMode::ReadWrite));
		Second.Close();
	});
}

#endif
