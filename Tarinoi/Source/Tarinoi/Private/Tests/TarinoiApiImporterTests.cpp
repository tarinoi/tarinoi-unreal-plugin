// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Sync/TarinoiApiImporter.h"
#include "TarinoiJson.h"
#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"

namespace TarinoiApiImporterTests
{
	const TCHAR* ApiPath = TEXT("https://app.tarinoi.com/api/v1/group1/proj1/documents");
	const TCHAR* ApiKey = TEXT("test-token");

	/** A scripted server: answers each request with the next queued response, synchronously. */
	struct FFakeTransport : ITarinoiHttpTransport
	{
		TArray<FTarinoiHttpResponse> Responses;
		TArray<FString> RequestedUrls;
		TArray<TMap<FString, FString>> RequestedHeaders;

		FFakeTransport& Respond(const FString& Body, int32 Status = 200)
		{
			FTarinoiHttpResponse& Response = Responses.AddDefaulted_GetRef();
			Response.Status = Status;
			Response.Body = Body;
			return *this;
		}

		virtual void Get(const FString& Url, const TMap<FString, FString>& Headers,
			TFunction<void(const FTarinoiHttpResponse&)> OnComplete) override
		{
			RequestedUrls.Add(Url);
			RequestedHeaders.Add(Headers);

			FTarinoiHttpResponse Response;
			Response.Status = 200;
			if (Responses.Num() > 0)
			{
				Response = Responses[0];
				Responses.RemoveAt(0);
			}
			OnComplete(Response);
		}
	};

	/** One feed line. Defaults describe an active card on the main layer at the current format. */
	struct FDoc
	{
		FString DocumentId;
		FString CollectionId = TEXT("col1");
		FString LayerId = TarinoiLayerFilter::MainLayer;
		FString DocumentType = TEXT("card");
		int64 UpdateKey = 1;
		bool bTombstone = false;
		bool bArchived = false;
		bool bMoved = false;
		FString DataVersion = TEXT("2.0.0");
		FString Identifier;
		FString Payload = TEXT("{\"a\":1}");
	};

	FString Doc(const FDoc& D)
	{
		return FString::Printf(
			TEXT("{\"document_id\":\"%s\",\"collection_id\":\"%s\",\"layer_id\":\"%s\",\"document_type\":\"%s\",")
			TEXT("\"update_key\":%lld,\"is_tombstone\":%s,\"is_archived\":%s,\"is_moved\":%s,\"data_version\":\"%s\",")
			TEXT("\"identifier\":%s,\"payload\":%s}"),
			*D.DocumentId, *D.CollectionId, *D.LayerId, *D.DocumentType, D.UpdateKey,
			D.bTombstone ? TEXT("true") : TEXT("false"), D.bArchived ? TEXT("true") : TEXT("false"),
			D.bMoved ? TEXT("true") : TEXT("false"), *D.DataVersion,
			D.Identifier.IsEmpty() ? TEXT("null") : *FString::Printf(TEXT("\"%s\""), *D.Identifier),
			*D.Payload);
	}

	FString Lines(std::initializer_list<FString> Items)
	{
		return FString::Join(TArray<FString>(Items), TEXT("\n"));
	}

	struct FRun
	{
		FTarinoiSyncResult Result;
		bool bCompleted = false;
		TArray<float> Progress;
	};

	FRun Sync(const TSharedRef<FFakeTransport>& Transport, const FTarinoiTestDb& Fixture,
		const FString& Path = ApiPath, const FString& Key = ApiKey)
	{
		FRun Run;
		const TSharedRef<FTarinoiApiImporter> Importer = MakeShared<FTarinoiApiImporter>(Transport);
		Importer->Start(Path, Key, Fixture.Db,
			FTarinoiApiImporter::FOnProgress::CreateLambda([&Run](const FString&, float Fraction) { Run.Progress.Add(Fraction); }),
			FTarinoiApiImporter::FOnComplete::CreateLambda([&Run](const FTarinoiSyncResult& Result)
			{
				Run.Result = Result;
				Run.bCompleted = true;
			}));
		return Run;
	}

	int64 Count(const FTarinoiTestDb& Fixture, const TCHAR* Sql)
	{
		const TArray<FString> Rows = Fixture.Db->QueryStrings(Sql);
		return Rows.Num() > 0 ? FCString::Atoi64(*Rows[0]) : -1;
	}
}

BEGIN_DEFINE_SPEC(FTarinoiApiImporterSpec, "Tarinoi.Sync.ApiImporter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TUniquePtr<FTarinoiTestDb> Fixture;
	TSharedPtr<TarinoiApiImporterTests::FFakeTransport> Server;
END_DEFINE_SPEC(FTarinoiApiImporterSpec)

void FTarinoiApiImporterSpec::Define()
{
	using namespace TarinoiApiImporterTests;

	BeforeEach([this]()
	{
		Fixture = MakeUnique<FTarinoiTestDb>();
		Server = MakeShared<FFakeTransport>();
	});

	AfterEach([this]()
	{
		Server.Reset();
		Fixture.Reset();
	});

	Describe("preconditions", [this]()
	{
		It("fails without an API key, saying where to set one", [this]()
		{
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture, ApiPath, TEXT(""));
			TestTrue("completed", Run.bCompleted);
			TestFalse("failed", Run.Result.bSuccess);
			TestTrue("actionable", Run.Result.Error.Contains(TEXT("Set API Token")));
			TestEqual("no request", Server->RequestedUrls.Num(), 0);
		});

		It("fails without an API path", [this]()
		{
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture, TEXT(""));
			TestTrue("mentions settings", Run.Result.Error.Contains(TEXT("Project Settings")));
		});

		It("fails on a path that is not a URL, or has no project id", [this]()
		{
			TestTrue("not a url", Sync(Server.ToSharedRef(), *Fixture, TEXT("not a url")).Result.Error.Contains(TEXT("URL")));
			TestTrue("no project", Sync(Server.ToSharedRef(), *Fixture, TEXT("https://app.tarinoi.com")).Result.Error.Contains(TEXT("project id")));
		});

		It("fails on a closed database", [this]()
		{
			Fixture->Db->Close();
			TestTrue("database", Sync(Server.ToSharedRef(), *Fixture).Result.Error.Contains(TEXT("database")));
		});
	});

	Describe("pagination", [this]()
	{
		It("stores a single page and records where it came from", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1")}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("success", Run.Result.bSuccess);
			TestEqual("upserted", Run.Result.Stats.DocumentsUpserted, 1);
			TarinoiTest::Strings(*this, TEXT("stored"), Fixture->VisibleDocumentIds(), {TEXT("d1")});
			TestEqualSensitive("project id", Fixture->Db->ReadMeta(FTarinoiDatabase::ProjectIdKey), FString(TEXT("proj1")));
			TestEqualSensitive("api path", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiPathKey), FString(ApiPath));
		});

		It("sends the bearer token and asks for NDJSON", [this]()
		{
			Server->Respond(FString());
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqualSensitive("auth", Server->RequestedHeaders[0].FindRef(TEXT("Authorization")), FString(TEXT("Bearer test-token")));
			TestEqualSensitive("accept", Server->RequestedHeaders[0].FindRef(TEXT("Accept")), FString(TEXT("application/x-ndjson")));
		});

		It("follows the cursor until it stops", [this]()
		{
			Server->Respond(Lines({Doc({.DocumentId = TEXT("d1")}), TEXT("{\"cursor\":\"10\"}")}))
				.Respond(Lines({Doc({.DocumentId = TEXT("d2")}), TEXT("{\"cursor\":\"20\"}")}))
				.Respond(Doc({.DocumentId = TEXT("d3")}));

			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("success", Run.Result.bSuccess);
			if (TestEqual("requests", Server->RequestedUrls.Num(), 3))
			{
				TestFalse("first has no cursor", Server->RequestedUrls[0].Contains(TEXT("cursor")));
				TestTrue("second", Server->RequestedUrls[1].EndsWith(TEXT("?cursor=10")));
				TestTrue("third", Server->RequestedUrls[2].EndsWith(TEXT("?cursor=20")));
			}
			TarinoiTest::Strings(*this, TEXT("stored"), Fixture->VisibleDocumentIds(), {TEXT("d1"), TEXT("d2"), TEXT("d3")});
		});

		It("persists the cursor after each page, so an interrupted sync resumes", [this]()
		{
			Server->Respond(Lines({Doc({.DocumentId = TEXT("d1")}), TEXT("{\"cursor\":\"10\"}")}))
				.Respond(FString(), 500);
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestFalse("failed", Run.Result.bSuccess);
			TestEqualSensitive("cursor kept", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey), FString(TEXT("10")));
		});

		It("sends a stored cursor on the very first request", [this]()
		{
			Fixture->Db->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, TEXT("500"));
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .UpdateKey = 501}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("incremental", Server->RequestedUrls[0].EndsWith(TEXT("?cursor=500")));
		});

		It("uses the highest update key as the cursor when the server sends none", [this]()
		{
			Server->Respond(Lines({Doc({.DocumentId = TEXT("d1"), .UpdateKey = 7}), Doc({.DocumentId = TEXT("d2"), .UpdateKey = 42})}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqualSensitive("cursor", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey), FString(TEXT("42")));
		});

		It("leaves content and cursor alone on an empty incremental response", [this]()
		{
			Fixture->Insert({.DocumentId = TEXT("existing"), .UpdateKey = 5});
			Fixture->Db->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, TEXT("5"));
			Server->Respond(FString());
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("success", Run.Result.bSuccess);
			TestEqual("nothing upserted", Run.Result.Stats.DocumentsUpserted, 0);
			TarinoiTest::Strings(*this, TEXT("content"), Fixture->VisibleDocumentIds(), {TEXT("existing")});
			TestEqualSensitive("cursor", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey), FString(TEXT("5")));
		});

		It("reports progress, ending at 1", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1")}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("some reports", Run.Progress.Num() >= 2);
			TestEqual("ends complete", Run.Progress.Num() > 0 ? Run.Progress.Last() : 0.0f, 1.0f);
		});
	});

	Describe("HTTP failures", [this]()
	{
		struct FCase { int32 Status; const TCHAR* Expected; };
		static const FCase Cases[] = {
			{401, TEXT("credentials rejected")},
			{403, TEXT("credentials rejected")},
			{404, TEXT("project not found")},
			{500, TEXT("server error")},
			{400, TEXT("unexpected response")},
		};

		for (const FCase& Case : Cases)
		{
			It(FString::Printf(TEXT("turns HTTP %d into '%s'"), Case.Status, Case.Expected), [this, Case]()
			{
				Server->Respond(FString(), Case.Status);
				const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
				TestFalse("failed", Run.Result.bSuccess);
				TestTrue(Run.Result.Error, Run.Result.Error.Contains(Case.Expected));
				TestTrue("names the status", Run.Result.Error.Contains(FString::FromInt(Case.Status)));
			});
		}

		It("reports a transport failure", [this]()
		{
			FTarinoiHttpResponse& Response = Server->Responses.AddDefaulted_GetRef();
			Response.Status = 0;
			Response.TransportError = TEXT("the server could not be reached");
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("reached", Run.Result.Error.Contains(TEXT("could not be reached")));
		});

		It("leaves the cursor unchanged when a page fails", [this]()
		{
			Fixture->Db->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, TEXT("3"));
			Server->Respond(FString(), 503);
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqualSensitive("cursor", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey), FString(TEXT("3")));
		});
	});

	Describe("layer-aware upsert", [this]()
	{
		It("keeps active rows on both layers, and shows one", [this]()
		{
			Server->Respond(Lines({Doc({.DocumentId = TEXT("d1")}), Doc({.DocumentId = TEXT("d1"), .LayerId = TarinoiLayerFilter::BufferLayer})}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("stored", Count(*Fixture, TEXT("SELECT COUNT(*) FROM documents")), (int64)2);
			TestEqual("visible", Fixture->VisibleDocumentIds().Num(), 1);
		});

		It("deletes tombstoned documents", [this]()
		{
			Fixture->Insert({.DocumentId = TEXT("d1")});
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .bTombstone = true}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("counted", Run.Result.Stats.DocumentsDeleted, 1);
			TestEqual("gone", Fixture->VisibleDocumentIds().Num(), 0);
		});

		It("evicts a tombstoned manifest from collections", [this]()
		{
			Fixture->Db->Execute(TEXT("INSERT INTO collections VALUES ('col-doc', 'Name', 'card-collection', '{}')"));
			Server->Respond(Doc({.DocumentId = TEXT("col-doc"), .DocumentType = TEXT("collection-manifest"), .bTombstone = true}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("evicted", Count(*Fixture, TEXT("SELECT COUNT(*) FROM collections")), (int64)0);
		});

		It("removes only the tombstoned layer", [this]()
		{
			Fixture->Insert({.DocumentId = TEXT("d1")});
			Fixture->Insert({.DocumentId = TEXT("d1"), .LayerId = TarinoiLayerFilter::BufferLayer});
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .LayerId = TarinoiLayerFilter::BufferLayer, .bTombstone = true}));
			Sync(Server.ToSharedRef(), *Fixture);
			TarinoiTest::Strings(*this, TEXT("layers"), Fixture->Db->QueryStrings(TEXT("SELECT layer_id FROM documents")), {TarinoiLayerFilter::MainLayer});
		});

		It("stores archived buffer rows, because they suppress the committed version", [this]()
		{
			// The subtlest rule in the importer: dropping these would resurrect deleted content.
			Server->Respond(Lines({Doc({.DocumentId = TEXT("d1")}), Doc({.DocumentId = TEXT("d1"), .LayerId = TarinoiLayerFilter::BufferLayer, .bArchived = true})}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("stored", Count(*Fixture, TEXT("SELECT COUNT(*) FROM documents")), (int64)2);
			TestEqual("hidden", Fixture->VisibleDocumentIds().Num(), 0);
			TestEqual("upserted", Run.Result.Stats.DocumentsUpserted, 1);
			TestEqual("deleted", Run.Result.Stats.DocumentsDeleted, 1);
		});

		It("stores moved documents with their flag", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .bMoved = true}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("flag", Count(*Fixture, TEXT("SELECT is_moved FROM documents")), (int64)1);
			TestEqual("hidden", Fixture->VisibleDocumentIds().Num(), 0);
		});

		It("replaces a re-synced document in place", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .UpdateKey = 1, .Payload = TEXT("{\"v\":1}")}));
			Sync(Server.ToSharedRef(), *Fixture);
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .UpdateKey = 2, .Payload = TEXT("{\"v\":2}")}));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("one row", Count(*Fixture, TEXT("SELECT COUNT(*) FROM documents")), (int64)1);
			TestTrue("new payload", Fixture->VisiblePayloads()[0].Contains(TEXT("\"v\":2")));
		});

		It("stores the document's fields", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .DocumentType = TEXT("entity"), .UpdateKey = 77, .Identifier = TEXT("narrator")}));
			Sync(Server.ToSharedRef(), *Fixture);
			Fixture->Db->Query(TEXT("SELECT * FROM documents"), {}, [this](const FTarinoiSqlRow& Row)
			{
				const FTarinoiDocumentRow Doc = FTarinoiDocumentRow::FromSql(Row);
				TestEqualSensitive("type", Doc.DocumentType, FString(TEXT("entity")));
				TestEqualSensitive("identifier", Doc.Identifier, FString(TEXT("narrator")));
				TestEqual("update key", Doc.UpdateKey, (int64)77);
				TestEqualSensitive("namespace", Doc.Namespace, FString(TEXT("document")));
			});
		});

		It("skips documents with no identity, with a warning", [this]()
		{
			Server->Respond(TEXT("{\"document_id\":\"\",\"collection_id\":\"c1\"}"));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestTrue("success", Run.Result.bSuccess);
			TestEqual("nothing", Fixture->VisibleDocumentIds().Num(), 0);
			TestEqual("warned", Run.Result.Stats.Warnings.Num(), 1);
		});

		It("stores a missing payload as an empty object", [this]()
		{
			Server->Respond(FString::Printf(TEXT("{\"document_id\":\"d1\",\"collection_id\":\"c1\",\"layer_id\":\"%s\",\"update_key\":1}"), TarinoiLayerFilter::MainLayer));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqualSensitive("payload", Fixture->VisiblePayloads()[0], FString(TEXT("{}")));
		});

		It("understands flags sent as numbers or strings", [this]()
		{
			Server->Respond(FString::Printf(TEXT("{\"document_id\":\"d1\",\"collection_id\":\"c1\",\"layer_id\":\"%s\",\"update_key\":1,\"is_archived\":1,\"is_moved\":\"true\"}"), TarinoiLayerFilter::MainLayer));
			Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("archived", Count(*Fixture, TEXT("SELECT is_archived FROM documents")), (int64)1);
			TestEqual("moved", Count(*Fixture, TEXT("SELECT is_moved FROM documents")), (int64)1);
		});
	});

	Describe("collections and the version gate", [this]()
	{
		It("rebuilds the collections table from manifests", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("col1"), .DocumentType = TEXT("collection-manifest"),
				.Payload = TEXT("{\"label\":\"Global\",\"collection_type\":\"list-collection\"}")}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("updated", Run.Result.Stats.CollectionsUpdated, 1);
			TarinoiTest::Strings(*this, TEXT("row"),
				Fixture->Db->QueryStrings(TEXT("SELECT collection_id || '|' || collection_name || '|' || collection_type FROM collections")),
				{TEXT("col1|Global|list-collection")});
		});

		It("falls back to collection_name when there is no label", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("col1"), .DocumentType = TEXT("collection-manifest"),
				.Payload = TEXT("{\"collection_name\":\"fallback\",\"collection_type\":\"card-collection\"}")}));
			Sync(Server.ToSharedRef(), *Fixture);
			TarinoiTest::Strings(*this, TEXT("name"), Fixture->Db->QueryStrings(TEXT("SELECT collection_name FROM collections")), {TEXT("fallback")});
		});

		It("does not rebuild archived manifests", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("col1"), .DocumentType = TEXT("collection-manifest"), .bArchived = true, .Payload = TEXT("{\"label\":\"Gone\"}")}));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestEqual("none", Run.Result.Stats.CollectionsUpdated, 0);
		});

		It("aborts on a MAJOR data version mismatch, writing nothing from that page", [this]()
		{
			Fixture->Db->WriteMeta(FTarinoiDatabase::ApiSyncCursorKey, TEXT("9"));
			Server->Respond(Lines({Doc({.DocumentId = TEXT("ok")}), Doc({.DocumentId = TEXT("future"), .DataVersion = TEXT("3.0.0")}), TEXT("{\"cursor\":\"99\"}")}));
			AddExpectedError(TEXT("MAJOR data format mismatch"));
			const FRun Run = Sync(Server.ToSharedRef(), *Fixture);
			TestFalse("failed", Run.Result.bSuccess);
			TestTrue("says why", Run.Result.Error.Contains(TEXT("Update the plugin")));
			TestEqual("nothing written", Fixture->VisibleDocumentIds().Num(), 0);
			TestEqualSensitive("cursor untouched", Fixture->Db->ReadMeta(FTarinoiDatabase::ApiSyncCursorKey), FString(TEXT("9")));
		});

		It("syncs a compatible minor version, with a warning", [this]()
		{
			Server->Respond(Doc({.DocumentId = TEXT("d1"), .DataVersion = TEXT("2.3.0")}));
			AddExpectedMessage(TEXT("minor data format mismatch"), ELogVerbosity::Warning);
			TestTrue("success", Sync(Server.ToSharedRef(), *Fixture).Result.bSuccess);
		});
	});

	Describe("AppendCursor", [this]()
	{
		It("adds a query parameter, keeping any that exist", [this]()
		{
			TestEqualSensitive("plain", FTarinoiApiImporter::AppendCursor(TEXT("https://h/p/documents"), TEXT("5")), FString(TEXT("https://h/p/documents?cursor=5")));
			TestEqualSensitive("existing", FTarinoiApiImporter::AppendCursor(TEXT("https://h/p/documents?x=1"), TEXT("5")), FString(TEXT("https://h/p/documents?x=1&cursor=5")));
		});

		It("escapes the value", [this]()
		{
			TestEqualSensitive("escaped", FTarinoiApiImporter::AppendCursor(TEXT("https://h/d"), TEXT("a b&c")), FString(TEXT("https://h/d?cursor=a%20b%26c")));
		});
	});
}

#endif
