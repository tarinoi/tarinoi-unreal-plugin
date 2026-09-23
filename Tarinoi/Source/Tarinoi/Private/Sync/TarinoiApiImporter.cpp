// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Sync/TarinoiApiImporter.h"

#include "Data/TarinoiDatabase.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Sync/TarinoiNdjson.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"
#include "TarinoiSettings.h"

FTarinoiApiImporter::FTarinoiApiImporter(TSharedRef<ITarinoiHttpTransport> InTransport)
	: Transport(MoveTemp(InTransport))
{
}

void FTarinoiApiImporter::Start(const FString& ApiPath, const FString& InApiKey, const TSharedPtr<FTarinoiDatabase>& InDatabase,
	FOnProgress InOnProgress, FOnComplete InOnComplete)
{
	OnProgress = MoveTemp(InOnProgress);
	OnComplete = MoveTemp(InOnComplete);
	Database = InDatabase;
	ApiKey = InApiKey.TrimStartAndEnd();
	BaseUrl = ApiPath.TrimStartAndEnd();

	if (ApiKey.IsEmpty())
	{
		Finish(FTarinoiSyncResult::Fail(TEXT("No API token saved. Use Tools > Tarinoi > Set API Token.")));
		return;
	}

	if (BaseUrl.IsEmpty())
	{
		Finish(FTarinoiSyncResult::Fail(TEXT("No API path set. Check Project Settings > Plugins > Tarinoi.")));
		return;
	}

	if (!Database.IsValid() || !Database->IsOpen())
	{
		Finish(FTarinoiSyncResult::Fail(TEXT("Sync needs an open database.")));
		return;
	}

	if (!BaseUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) && !BaseUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		Finish(FTarinoiSyncResult::Fail(FString::Printf(TEXT("Cannot parse the API path as a URL: %s"), *ApiPath)));
		return;
	}

	const FString ProjectId = UTarinoiSettings::ProjectIdFromApiPath(BaseUrl);
	if (ProjectId.IsEmpty())
	{
		Finish(FTarinoiSyncResult::Fail(FString::Printf(TEXT("Cannot derive a project id from the API path: %s"), *ApiPath)));
		return;
	}

	Database->WriteMeta(FTarinoiDatabase::ProjectIdKey, ProjectId);
	Database->WriteMeta(FTarinoiDatabase::ApiPathKey, BaseUrl);

	Cursor = Database->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey);
	Report(Cursor.IsEmpty() ? TEXT("Starting full sync...") : FString::Printf(TEXT("Fetching changes since %s..."), *Cursor),
		Cursor.IsEmpty() ? 0.0f : 0.1f);

	SelfWhileRunning = AsShared();
	FetchPage();
}

void FTarinoiApiImporter::Cancel()
{
	if (bFinished)
	{
		return;
	}

	bCancelled = true;
	Transport->Cancel();
	Finish(FTarinoiSyncResult::Fail(TEXT("Sync cancelled.")));
}

void FTarinoiApiImporter::FetchPage()
{
	if (Page >= MaxPages)
	{
		Stats.Warnings.Add(FString::Printf(TEXT("Stopped after %d pages: the cursor never cleared."), MaxPages));
		Finish(FTarinoiSyncResult::Ok(Stats));
		return;
	}

	Report(FString::Printf(TEXT("Fetching page %d..."), Page), FMath::Clamp(0.1f + Page * 0.05f, 0.0f, 0.9f));

	const FString Url = Cursor.IsEmpty() ? BaseUrl : AppendCursor(BaseUrl, Cursor);
	const TMap<FString, FString> Headers = {
		{TEXT("Authorization"), TEXT("Bearer ") + ApiKey},
		{TEXT("Accept"), TEXT("application/x-ndjson")},
	};

	TWeakPtr<FTarinoiApiImporter> WeakThis = AsShared();
	Transport->Get(Url, Headers, [WeakThis, Url](const FTarinoiHttpResponse& Response)
	{
		if (const TSharedPtr<FTarinoiApiImporter> This = WeakThis.Pin())
		{
			This->OnPage(Response, Url);
		}
	});
}

void FTarinoiApiImporter::OnPage(const FTarinoiHttpResponse& Response, const FString& RequestUrl)
{
	if (bCancelled || bFinished)
	{
		return;
	}

	if (Response.Status == 0)
	{
		Finish(FTarinoiSyncResult::Fail(FString::Printf(TEXT("Sync failed: %s (%s)"),
			Response.TransportError.IsEmpty() ? TEXT("no response") : *Response.TransportError, *RequestUrl)));
		return;
	}

	if (Response.Status < 200 || Response.Status >= 300)
	{
		Finish(FTarinoiSyncResult::Fail(FString::Printf(TEXT("%s (HTTP %d for %s)"), *HttpErrorHint(Response.Status), Response.Status, *RequestUrl)));
		return;
	}

	// The database may have been closed while the request was in flight: PIE ending, say.
	if (!Database.IsValid() || !Database->IsOpen())
	{
		Finish(FTarinoiSyncResult::Fail(TEXT("The local database was closed during the sync.")));
		return;
	}

	const FTarinoiNdjsonPage Parsed = TarinoiNdjson::Parse(Response.Body);

	// Version-check the whole page before writing any of it, so an incompatible page leaves the
	// database untouched and the cursor where it was: an updated plugin resumes from the same point.
	for (const TSharedPtr<FJsonObject>& Document : Parsed.Documents)
	{
		const FString VersionError = VersionCheck.Check(TarinoiJson::Str(Document, TEXT("data_version")));
		if (!VersionError.IsEmpty())
		{
			Finish(FTarinoiSyncResult::Fail(VersionError));
			return;
		}
	}

	const bool bApplied = Database->RunInTransaction([this, &Parsed]()
	{
		for (const TSharedPtr<FJsonObject>& Document : Parsed.Documents)
		{
			UpsertDocument(Document, *Database, Stats);
		}
		return true;
	});

	if (!bApplied)
	{
		Finish(FTarinoiSyncResult::Fail(TEXT("Sync failed: could not write to the local database. See the log for details.")));
		return;
	}

	if (!Parsed.Cursor.IsSet())
	{
		// When the server never sent a cursor, derive one from the highest update_key held. That
		// covers a full sync and a single-page incremental response, and is what makes the next
		// sync incremental. (Keys are well inside the 2^53 a JSON double holds exactly.)
		if (!bCursorAdvanced)
		{
			const TArray<FString> Max = Database->QueryStrings(TEXT("SELECT MAX(update_key) FROM documents"));
			if (Max.Num() > 0 && !Max[0].IsEmpty())
			{
				Database->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, Max[0]);
			}
		}

		RebuildCollections(*Database, Stats);
		Report(TEXT("Sync complete."), 1.0f);
		Finish(FTarinoiSyncResult::Ok(Stats));
		return;
	}

	Cursor = Parsed.Cursor.GetValue();
	bCursorAdvanced = true;
	Database->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, Cursor);
	++Page;
	FetchPage();
}

void FTarinoiApiImporter::Finish(const FTarinoiSyncResult& Result)
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	OnComplete.ExecuteIfBound(Result);

	// Last: this may release the final reference to this importer.
	SelfWhileRunning.Reset();
}

void FTarinoiApiImporter::Report(const FString& Message, float Fraction) const
{
	OnProgress.ExecuteIfBound(Message, Fraction);
}

void FTarinoiApiImporter::UpsertDocument(const TSharedPtr<FJsonObject>& Document, FTarinoiDatabase& Database, FTarinoiSyncStats& Stats)
{
	const FString DocumentId = TarinoiJson::Str(Document, TEXT("document_id"));
	const FString CollectionId = TarinoiJson::Str(Document, TEXT("collection_id"));
	const FString LayerId = TarinoiJson::Str(Document, TEXT("layer_id"));

	if (DocumentId.IsEmpty() || CollectionId.IsEmpty())
	{
		Stats.Warnings.Add(TEXT("Skipped a document with no document_id or collection_id."));
		return;
	}

	if (TarinoiJson::LooseBool(Document, TEXT("is_tombstone")))
	{
		Database.Execute(TEXT("DELETE FROM documents WHERE document_id = ? AND collection_id = ? AND layer_id = ?"),
			{DocumentId, CollectionId, LayerId});
		Database.Execute(TEXT("DELETE FROM collections WHERE collection_id = ?"), {DocumentId});
		++Stats.DocumentsDeleted;
		return;
	}

	const bool bArchived = TarinoiJson::LooseBool(Document, TEXT("is_archived"));
	const bool bMoved = TarinoiJson::LooseBool(Document, TEXT("is_moved"));

	const TSharedPtr<FJsonValue> PayloadValue = Document->TryGetField(TEXT("payload"));
	const FString Payload = PayloadValue.IsValid() && PayloadValue->Type != EJson::Null
		? TarinoiJson::Stringify(PayloadValue)
		: FString(TEXT("{}"));

	const FString Namespace = TarinoiJson::Str(Document, TEXT("namespace"));
	double UpdateKey = 0.0;
	Document->TryGetNumberField(TEXT("update_key"), UpdateKey);

	Database.Execute(TEXT(
		"INSERT OR REPLACE INTO documents"
		" (document_id, collection_id, document_type, layer_id, namespace, identifier,"
		"  update_key, is_tombstone, is_archived, is_moved, payload)"
		" VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?)"),
		{DocumentId, CollectionId, TarinoiJson::Str(Document, TEXT("document_type")), LayerId,
		 Namespace.IsEmpty() ? FString(TEXT("document")) : Namespace,
		 FTarinoiSqlValue::TextOrNull(TarinoiJson::Str(Document, TEXT("identifier"))),
		 static_cast<int64>(UpdateKey), bArchived, bMoved, Payload});

	if (bArchived || bMoved)
	{
		++Stats.DocumentsDeleted;
	}
	else
	{
		++Stats.DocumentsUpserted;
	}
}

void FTarinoiApiImporter::RebuildCollections(FTarinoiDatabase& Database, FTarinoiSyncStats& Stats)
{
	struct FManifest
	{
		FString DocumentId;
		FString Payload;
	};

	TArray<FManifest> Manifests;
	Database.Query(TEXT(
		"SELECT document_id, payload FROM documents"
		" WHERE document_type = 'collection-manifest' AND is_tombstone = 0 AND is_archived = 0 AND is_moved = 0"),
		{}, [&Manifests](const FTarinoiSqlRow& Row)
		{
			Manifests.Add({Row.GetString(TEXT("document_id")), Row.GetString(TEXT("payload"))});
		});

	int32 Count = 0;
	for (const FManifest& Manifest : Manifests)
	{
		const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Manifest.Payload);
		if (!Payload.IsValid())
		{
			Stats.Warnings.Add(FString::Printf(TEXT("Collection manifest '%s' has an unreadable payload."), *Manifest.DocumentId));
			continue;
		}

		FString Name = TarinoiJson::Str(Payload, TEXT("label"));
		if (Name.IsEmpty())
		{
			Name = TarinoiJson::Str(Payload, TEXT("collection_name"));
		}

		Database.Execute(TEXT("INSERT OR REPLACE INTO collections (collection_id, collection_name, collection_type, payload) VALUES (?, ?, ?, ?)"),
			{Manifest.DocumentId, Name, TarinoiJson::Str(Payload, TEXT("collection_type")), Manifest.Payload});
		++Count;
	}

	Stats.CollectionsUpdated = Count;
}

FString FTarinoiApiImporter::AppendCursor(const FString& Url, const FString& InCursor)
{
	const FString Parameter = TEXT("cursor=") + FGenericPlatformHttp::UrlEncode(InCursor);

	FString Base = Url;
	FString Fragment;
	const int32 Hash = Url.Find(TEXT("#"));
	if (Hash != INDEX_NONE)
	{
		Base = Url.Left(Hash);
		Fragment = Url.Mid(Hash);
	}

	if (!Base.Contains(TEXT("?")))
	{
		return Base + TEXT("?") + Parameter + Fragment;
	}
	return Base + (Base.EndsWith(TEXT("?")) || Base.EndsWith(TEXT("&")) ? TEXT("") : TEXT("&")) + Parameter + Fragment;
}

FString FTarinoiApiImporter::HttpErrorHint(int32 Status)
{
	if (Status == 401 || Status == 403)
	{
		return TEXT("Sync failed: credentials rejected. Check your API token (Tools > Tarinoi > Set API Token).");
	}
	if (Status == 404)
	{
		return TEXT("Sync failed: project not found. Check the API path in Project Settings > Plugins > Tarinoi.");
	}
	if (Status >= 500)
	{
		return TEXT("Sync failed: server error. The Tarinoi API may be temporarily unavailable; try again shortly.");
	}
	return TEXT("Sync failed: unexpected response.");
}
