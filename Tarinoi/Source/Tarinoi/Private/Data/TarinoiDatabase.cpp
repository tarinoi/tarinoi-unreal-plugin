// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Data/TarinoiDatabase.h"

#include "Data/TarinoiLayerFilter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "Tarinoi.h"

const TCHAR* FTarinoiDatabase::SchemaVersionKey = TEXT("schema_version");
const TCHAR* FTarinoiDatabase::ProjectIdKey = TEXT("project_id");
const TCHAR* FTarinoiDatabase::ApiPathKey = TEXT("api_path");
const TCHAR* FTarinoiDatabase::ApiSyncCursorKey = TEXT("api_sync_cursor");

namespace
{
	/** Every open database, by absolute path. Weak, so the last holder closes the file. */
	TMap<FString, TWeakPtr<FTarinoiDatabase>>& OpenDatabases()
	{
		static TMap<FString, TWeakPtr<FTarinoiDatabase>> Map;
		return Map;
	}

	FString Normalize(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Full);
		return Full;
	}
}

// -----------------------------------------------------------------------------
// FTarinoiSqlRow
// -----------------------------------------------------------------------------

FString FTarinoiSqlRow::GetString(const TCHAR* Column) const
{
	FString Value;
	Statement.GetColumnValueByName(Column, Value);
	return Value;
}

int64 FTarinoiSqlRow::GetInt64(const TCHAR* Column) const
{
	int64 Value = 0;
	Statement.GetColumnValueByName(Column, Value);
	return Value;
}

double FTarinoiSqlRow::GetDouble(const TCHAR* Column) const
{
	double Value = 0.0;
	Statement.GetColumnValueByName(Column, Value);
	return Value;
}

bool FTarinoiSqlRow::IsNull(const TCHAR* Column) const
{
	ESQLiteColumnType Type = ESQLiteColumnType::Null;
	return !Statement.GetColumnTypeByName(Column, Type) || Type == ESQLiteColumnType::Null;
}

FString FTarinoiSqlRow::GetString(int32 Index) const
{
	FString Value;
	Statement.GetColumnValueByIndex(Index, Value);
	return Value;
}

int64 FTarinoiSqlRow::GetInt64(int32 Index) const
{
	int64 Value = 0;
	Statement.GetColumnValueByIndex(Index, Value);
	return Value;
}

bool FTarinoiSqlRow::IsNull(int32 Index) const
{
	ESQLiteColumnType Type = ESQLiteColumnType::Null;
	return !Statement.GetColumnTypeByIndex(Index, Type) || Type == ESQLiteColumnType::Null;
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

FString FTarinoiDatabase::BaseDirectory()
{
	return Normalize(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tarinoi")));
}

FString FTarinoiDatabase::PathForProject(const FString& ProjectId)
{
	return FPaths::Combine(BaseDirectory(), ProjectId + TEXT(".db"));
}

TSharedPtr<FTarinoiDatabase> FTarinoiDatabase::Acquire(const FString& ProjectId)
{
	if (ProjectId.IsEmpty())
	{
		UE_LOG(LogTarinoi, Error, TEXT("Database: cannot open a database without a project id"));
		return nullptr;
	}

	return AcquireAtPath(PathForProject(ProjectId));
}

TSharedPtr<FTarinoiDatabase> FTarinoiDatabase::AcquireAtPath(const FString& InPath)
{
	// SQLiteCore connections have no locking of their own; see the class comment.
	ensureMsgf(IsInGameThread(), TEXT("Tarinoi databases are game-thread only"));

	const FString Key = Normalize(InPath);
	if (TSharedPtr<FTarinoiDatabase> Existing = OpenDatabases().FindRef(Key).Pin())
	{
		if (Existing->IsOpen())
		{
			return Existing;
		}
	}

	TSharedPtr<FTarinoiDatabase> Database = MakeShareable(new FTarinoiDatabase());
	if (!Database->OpenAt(Key))
	{
		return nullptr;
	}

	OpenDatabases().Add(Key, Database);
	return Database;
}

FTarinoiDatabase::~FTarinoiDatabase()
{
	Close();
}

bool FTarinoiDatabase::OpenAt(const FString& InPath)
{
	Path = InPath;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);

	Connection = MakeUnique<FSQLiteDatabase>();
	if (!Connection->Open(*Path, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		UE_LOG(LogTarinoi, Error, TEXT("Database: failed to open '%s': %s"), *Path, *Connection->GetLastError());
		Connection.Reset();
		return false;
	}

	InitSchema();
	return true;
}

bool FTarinoiDatabase::IsOpen() const
{
	return Connection.IsValid() && Connection->IsValid();
}

void FTarinoiDatabase::Close()
{
	if (Connection.IsValid())
	{
		Connection->Close();
		Connection.Reset();
	}

	OpenDatabases().Remove(Path);
}

FString FTarinoiDatabase::ActiveFilter() const
{
	return TarinoiLayerFilter::ActiveFilterSql(bCommittedOnly);
}

bool FTarinoiDatabase::EnsureUsable(const TCHAR* What) const
{
	if (IsOpen())
	{
		return true;
	}

	UE_LOG(LogTarinoi, Error, TEXT("Database: %s attempted on a closed database"), What);
	return false;
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool FTarinoiDatabase::Prepare(const TCHAR* Sql, const FTarinoiSqlArgs& Args, FSQLitePreparedStatement& OutStatement)
{
	if (!OutStatement.Create(*Connection, Sql))
	{
		UE_LOG(LogTarinoi, Error, TEXT("Database: %s\n-> %s"), *Connection->GetLastError(), Sql);
		return false;
	}

	for (int32 Index = 0; Index < Args.Num(); ++Index)
	{
		const FTarinoiSqlValue& Arg = Args[Index];
		const int32 Binding = Index + 1;
		bool bBound = false;
		switch (Arg.Type)
		{
		case FTarinoiSqlValue::EType::Null:
			bBound = OutStatement.SetBindingValueByIndex(Binding);
			break;
		case FTarinoiSqlValue::EType::Integer:
			bBound = OutStatement.SetBindingValueByIndex(Binding, Arg.Integer);
			break;
		case FTarinoiSqlValue::EType::Float:
			bBound = OutStatement.SetBindingValueByIndex(Binding, Arg.Float);
			break;
		case FTarinoiSqlValue::EType::Text:
			bBound = OutStatement.SetBindingValueByIndex(Binding, Arg.Text);
			break;
		}

		if (!bBound)
		{
			UE_LOG(LogTarinoi, Error, TEXT("Database: could not bind parameter %d: %s\n-> %s"),
				Binding, *Connection->GetLastError(), Sql);
			return false;
		}
	}

	return true;
}

int32 FTarinoiDatabase::Execute(const TCHAR* Sql, const FTarinoiSqlArgs& Args)
{
	if (!EnsureUsable(TEXT("execute")))
	{
		return -1;
	}

	{
		FSQLitePreparedStatement Statement;
		if (!Prepare(Sql, Args, Statement) || !Statement.Execute())
		{
			if (Statement.IsValid())
			{
				UE_LOG(LogTarinoi, Error, TEXT("Database: %s\n-> %s"), *Connection->GetLastError(), Sql);
			}
			++ErrorsInTransaction;
			return -1;
		}
	}

	int64 Changes = 0;
	FSQLitePreparedStatement Count(*Connection, TEXT("SELECT changes()"));
	if (Count.IsValid() && Count.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		Count.GetColumnValueByIndex(0, Changes);
	}
	return static_cast<int32>(Changes);
}

bool FTarinoiDatabase::Query(const TCHAR* Sql, const FTarinoiSqlArgs& Args, TFunctionRef<void(const FTarinoiSqlRow&)> OnRow)
{
	if (!EnsureUsable(TEXT("query")))
	{
		return false;
	}

	FSQLitePreparedStatement Statement;
	if (!Prepare(Sql, Args, Statement))
	{
		return false;
	}

	const FTarinoiSqlRow Row(Statement);
	while (true)
	{
		const ESQLitePreparedStatementStepResult Result = Statement.Step();
		if (Result == ESQLitePreparedStatementStepResult::Row)
		{
			OnRow(Row);
			continue;
		}

		if (Result == ESQLitePreparedStatementStepResult::Done)
		{
			return true;
		}

		UE_LOG(LogTarinoi, Error, TEXT("Database: %s\n-> %s"), *Connection->GetLastError(), Sql);
		return false;
	}
}

TArray<FString> FTarinoiDatabase::QueryStrings(const TCHAR* Sql, const FTarinoiSqlArgs& Args)
{
	TArray<FString> Values;
	Query(Sql, Args, [&Values](const FTarinoiSqlRow& Row)
	{
		Values.Add(Row.GetString(0));
	});
	return Values;
}

bool FTarinoiDatabase::RunInTransaction(TFunctionRef<bool()> Body)
{
	if (!EnsureUsable(TEXT("transaction")))
	{
		return false;
	}

	if (bInTransaction)
	{
		// Not nested: the outer transaction owns commit and rollback.
		return Body();
	}

	if (!Connection->Execute(TEXT("BEGIN")))
	{
		UE_LOG(LogTarinoi, Error, TEXT("Database: could not begin a transaction: %s"), *Connection->GetLastError());
		return false;
	}

	bInTransaction = true;
	ErrorsInTransaction = 0;
	const bool bBodySucceeded = Body();
	bInTransaction = false;

	if (bBodySucceeded && ErrorsInTransaction == 0)
	{
		if (Connection->Execute(TEXT("COMMIT")))
		{
			return true;
		}
		UE_LOG(LogTarinoi, Error, TEXT("Database: commit failed: %s"), *Connection->GetLastError());
	}

	Connection->Execute(TEXT("ROLLBACK"));
	UE_LOG(LogTarinoi, Error, TEXT("Database: transaction rolled back"));
	return false;
}

// -----------------------------------------------------------------------------
// Metadata
// -----------------------------------------------------------------------------

FString FTarinoiDatabase::ReadMeta(const FString& Key)
{
	const TArray<FString> Rows = QueryStrings(TEXT("SELECT value FROM metadata WHERE key = ?"), {Key});
	return Rows.Num() > 0 ? Rows[0] : FString();
}

void FTarinoiDatabase::WriteMeta(const FString& Key, const FString& Value)
{
	Execute(TEXT("INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)"), {Key, Value});
}

void FTarinoiDatabase::DeleteMeta(const TArray<FString>& Keys)
{
	for (const FString& Key : Keys)
	{
		Execute(TEXT("DELETE FROM metadata WHERE key = ?"), {Key});
	}
}

// -----------------------------------------------------------------------------
// Schema
// -----------------------------------------------------------------------------

void FTarinoiDatabase::InitSchema()
{
	// metadata first: it stores the schema version that decides whether the content tables
	// below need wiping.
	Execute(TEXT("CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL)"));

	const int32 Stored = FCString::Atoi(*ReadMeta(SchemaVersionKey));
	if (Stored < SchemaVersion)
	{
		if (Stored > 0)
		{
			UE_LOG(LogTarinoi, Log, TEXT("Database: schema %d -> %d, clearing local content (the next sync refetches it)"),
				Stored, SchemaVersion);
		}

		Execute(TEXT("DROP TABLE IF EXISTS documents"));
		Execute(TEXT("DROP TABLE IF EXISTS collections"));
		WriteMeta(SchemaVersionKey, FString::FromInt(SchemaVersion));
	}

	Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS documents ("
		"  document_id   TEXT NOT NULL,"
		"  collection_id TEXT NOT NULL,"
		"  document_type TEXT NOT NULL,"
		"  layer_id      TEXT NOT NULL,"
		"  namespace     TEXT NOT NULL DEFAULT 'document',"
		"  identifier    TEXT,"
		"  update_key    INTEGER NOT NULL,"
		"  is_tombstone  INTEGER NOT NULL DEFAULT 0,"
		"  is_archived   INTEGER NOT NULL DEFAULT 0,"
		"  is_moved      INTEGER NOT NULL DEFAULT 0,"
		"  payload       TEXT NOT NULL,"
		"  PRIMARY KEY (document_id, collection_id, layer_id)"
		")"));

	Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_documents_collection ON documents (collection_id, document_type)"));
	Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_documents_update_key ON documents (update_key)"));
	Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_documents_identifier ON documents (identifier)"));

	Execute(TEXT(
		"CREATE TABLE IF NOT EXISTS collections ("
		"  collection_id   TEXT PRIMARY KEY,"
		"  collection_name TEXT,"
		"  collection_type TEXT NOT NULL,"
		"  payload         TEXT NOT NULL"
		")"));
}
