// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Data/TarinoiDataVersion.h"
#include "Dom/JsonObject.h"
#include "Sync/TarinoiHttpTransport.h"
#include "Sync/TarinoiSyncTypes.h"

class FTarinoiDatabase;

/**
 * Pulls authored content from the Tarinoi documents API into the local database.
 *
 * The endpoint returns NDJSON pages plus a cursor. The cursor is an update_key watermark: the
 * server sends documents newer than it, so storing it after each page makes the next sync
 * incremental, and an interrupted sync resumes rather than restarting.
 *
 * Network I/O is asynchronous. Each page is applied to the database in one transaction on the
 * game thread when it arrives; see FTarinoiDatabase for why there is no worker connection.
 *
 * Usage: create one per sync, call Start, and keep it alive until OnComplete fires. Start holds
 * a reference to itself while a request is in flight.
 */
class TARINOI_API FTarinoiApiImporter : public TSharedFromThis<FTarinoiApiImporter>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnProgress, const FString& /*Message*/, float /*Fraction*/);
	DECLARE_DELEGATE_OneParam(FOnComplete, const FTarinoiSyncResult&);

	/** Guards against a malformed server response paginating forever. */
	static constexpr int32 MaxPages = 10000;

	explicit FTarinoiApiImporter(TSharedRef<ITarinoiHttpTransport> InTransport = ITarinoiHttpTransport::CreateDefault());

	/**
	 * Runs a full or incremental sync. Never fails hard: every failure arrives through OnComplete
	 * as an error message the user can act on.
	 */
	void Start(const FString& ApiPath, const FString& ApiKey, const TSharedPtr<FTarinoiDatabase>& Database,
		FOnProgress InOnProgress, FOnComplete InOnComplete);

	/** Stops after the current page. OnComplete fires with a cancellation error. */
	void Cancel();

	/**
	 * Applies one document from the feed. Tombstoned: the row for that layer is deleted, and a
	 * collection manifest leaves the collections table too. Archived or moved: stored with the
	 * flags intact, because on the buffer layer such a row suppresses the committed version.
	 * Otherwise: a plain upsert.
	 */
	static void UpsertDocument(const TSharedPtr<FJsonObject>& Document, FTarinoiDatabase& Database, FTarinoiSyncStats& Stats);

	/**
	 * Rebuilds the collections table from every active collection manifest. Run after each sync
	 * rather than maintained incrementally, so manifests stored by an earlier run count too.
	 */
	static void RebuildCollections(FTarinoiDatabase& Database, FTarinoiSyncStats& Stats);

	/** Appends ?cursor=... (or &cursor=...) to a URL, escaping the value. */
	static FString AppendCursor(const FString& Url, const FString& Cursor);

	/** Turns a failing status into something the user can act on. */
	static FString HttpErrorHint(int32 Status);

private:
	void FetchPage();
	void OnPage(const FTarinoiHttpResponse& Response, const FString& RequestUrl);
	void Finish(const FTarinoiSyncResult& Result);
	void Report(const FString& Message, float Fraction) const;

	TSharedRef<ITarinoiHttpTransport> Transport;
	TSharedPtr<FTarinoiDatabase> Database;
	FOnProgress OnProgress;
	FOnComplete OnComplete;
	FTarinoiDataVersion VersionCheck;
	FTarinoiSyncStats Stats;

	FString BaseUrl;
	FString ApiKey;
	FString Cursor;
	int32 Page = 0;
	bool bCursorAdvanced = false;
	bool bCancelled = false;
	bool bFinished = false;

	/** Keeps this importer alive while a request is outstanding. */
	TSharedPtr<FTarinoiApiImporter> SelfWhileRunning;
};
