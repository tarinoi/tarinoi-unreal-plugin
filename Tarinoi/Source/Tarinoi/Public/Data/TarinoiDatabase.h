// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FSQLiteDatabase;
class FSQLitePreparedStatement;

/** A value bound to a SQL parameter. Converts implicitly from the types queries use. */
struct TARINOI_API FTarinoiSqlValue
{
	enum class EType : uint8 { Null, Integer, Float, Text };

	EType Type = EType::Null;
	int64 Integer = 0;
	double Float = 0.0;
	FString Text;

	FTarinoiSqlValue() = default;
	FTarinoiSqlValue(int32 InValue) : Type(EType::Integer), Integer(InValue) {}
	FTarinoiSqlValue(int64 InValue) : Type(EType::Integer), Integer(InValue) {}
	FTarinoiSqlValue(bool InValue) : Type(EType::Integer), Integer(InValue ? 1 : 0) {}
	FTarinoiSqlValue(double InValue) : Type(EType::Float), Float(InValue) {}
	FTarinoiSqlValue(const TCHAR* InValue) : Type(EType::Text), Text(InValue) {}
	FTarinoiSqlValue(const FString& InValue) : Type(EType::Text), Text(InValue) {}

	static FTarinoiSqlValue Null() { return FTarinoiSqlValue(); }

	/** Text, or SQL NULL when the text is empty. For optional columns such as identifier. */
	static FTarinoiSqlValue TextOrNull(const FString& InValue)
	{
		return InValue.IsEmpty() ? FTarinoiSqlValue() : FTarinoiSqlValue(InValue);
	}
};

using FTarinoiSqlArgs = TArray<FTarinoiSqlValue>;

/** Read access to the current row of a query, by column name. NULL reads as empty or zero. */
class TARINOI_API FTarinoiSqlRow
{
public:
	explicit FTarinoiSqlRow(const FSQLitePreparedStatement& InStatement) : Statement(InStatement) {}

	FString GetString(const TCHAR* Column) const;
	int64 GetInt64(const TCHAR* Column) const;
	double GetDouble(const TCHAR* Column) const;
	bool IsNull(const TCHAR* Column) const;

	/** By position, for single-column queries. */
	FString GetString(int32 Index) const;
	int64 GetInt64(int32 Index) const;
	bool IsNull(int32 Index) const;

private:
	const FSQLitePreparedStatement& Statement;
};

/**
 * The local SQLite store for synced Tarinoi content: schema, metadata, and query helpers that
 * log rather than throw.
 *
 * One database file per Tarinoi project, under Saved/Tarinoi/.
 *
 * Threading and sharing: the engine's SQLite build (SQLiteCore under the default
 * bCompileCustomSQLitePlatform) allows only one open connection per file and gives that
 * connection no internal locking. So there is exactly one FTarinoiDatabase per file, handed out
 * by Acquire() and shared by everyone who needs it (the runtime, an editor sync, codegen), and
 * it is only ever used from the game thread. The sync importer keeps its network I/O
 * asynchronous and applies each fetched page here, in one transaction, on the game thread.
 */
class TARINOI_API FTarinoiDatabase : public TSharedFromThis<FTarinoiDatabase>
{
public:
	/**
	 * Bumped when the schema changes incompatibly. On an older stored version the content tables
	 * are dropped and recreated, and the next sync repopulates them. Migration is deliberately
	 * destructive: the database is a cache of server state, never a source of truth.
	 */
	static constexpr int32 SchemaVersion = 3;

	static const TCHAR* SchemaVersionKey;
	static const TCHAR* ProjectIdKey;
	static const TCHAR* ApiPathKey;
	static const TCHAR* ApiSyncCursorKey;

	/** Saved/Tarinoi, as an absolute path. */
	static FString BaseDirectory();
	static FString PathForProject(const FString& ProjectId);

	/**
	 * The shared database for a project, opened (and created) on first use. Returns null and logs
	 * when the file cannot be opened; a broken database degrades to "no content".
	 */
	static TSharedPtr<FTarinoiDatabase> Acquire(const FString& ProjectId);

	/** The shared database for a file outside the usual location, such as a snapshot copy. */
	static TSharedPtr<FTarinoiDatabase> AcquireAtPath(const FString& Path);

	~FTarinoiDatabase();

	bool IsOpen() const;
	const FString& GetPath() const { return Path; }

	/**
	 * Closes the connection now, even if others still hold this object; their queries then log
	 * and return nothing. Used before deleting or replacing the file.
	 */
	void Close();

	/**
	 * When true, buffer-layer (uncommitted) content is hidden: the project shows only what a
	 * player would. The runtime sets this from the project settings.
	 */
	bool bCommittedOnly = false;

	/** The WHERE fragment selecting visible documents. Queries using it alias documents as d. */
	FString ActiveFilter() const;

	/** Runs a statement. Returns the number of rows changed, or -1 on error. */
	int32 Execute(const TCHAR* Sql, const FTarinoiSqlArgs& Args = {});

	/** Runs a query, calling OnRow for each result row. Returns false on error. */
	bool Query(const TCHAR* Sql, const FTarinoiSqlArgs& Args, TFunctionRef<void(const FTarinoiSqlRow&)> OnRow);

	/** The first column of every result row, as text. Empty on error. */
	TArray<FString> QueryStrings(const TCHAR* Sql, const FTarinoiSqlArgs& Args = {});

	/**
	 * Runs Body inside a transaction. Body returns false (or a statement fails) to roll back.
	 * The importer wraps each page in one of these; otherwise every insert pays its own sync.
	 */
	bool RunInTransaction(TFunctionRef<bool()> Body);

	/** A metadata value, or "" when the key is absent. */
	FString ReadMeta(const FString& Key);
	void WriteMeta(const FString& Key, const FString& Value);
	void DeleteMeta(const TArray<FString>& Keys);

private:
	FTarinoiDatabase() = default;

	bool OpenAt(const FString& InPath);
	void InitSchema();
	bool Prepare(const TCHAR* Sql, const FTarinoiSqlArgs& Args, FSQLitePreparedStatement& OutStatement);
	bool EnsureUsable(const TCHAR* What) const;

	TUniquePtr<FSQLiteDatabase> Connection;
	FString Path;
	int32 ErrorsInTransaction = 0;
	bool bInTransaction = false;
};
