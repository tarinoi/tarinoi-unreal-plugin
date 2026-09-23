// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/TarinoiDocumentStore.h"
#include "TarinoiJson.h"
#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

namespace TarinoiDocumentStoreTests
{
	struct FFakeEntities : ITarinoiEntityCache
	{
		TMap<FString, TSharedPtr<FJsonObject>> Payloads;

		virtual TSharedPtr<FJsonObject> GetEntityPayload(const FString& Identifier) const override
		{
			return Payloads.FindRef(Identifier);
		}
	};

	FString StartCard(const TCHAR* Label)
	{
		return FString::Printf(TEXT("{\"base_ref\":\"start\",\"data\":{\"label\":\"%s\"}}"), Label);
	}
}

BEGIN_DEFINE_SPEC(FTarinoiDocumentStoreSpec, "Tarinoi.Data.DocumentStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TUniquePtr<FTarinoiTestDb> Fixture;
	TStrongObjectPtr<UTarinoiSqliteDocumentStore> Store;
	TarinoiDocumentStoreTests::FFakeEntities Entities;
END_DEFINE_SPEC(FTarinoiDocumentStoreSpec)

void FTarinoiDocumentStoreSpec::Define()
{
	using namespace TarinoiDocumentStoreTests;

	BeforeEach([this]()
	{
		Fixture = MakeUnique<FTarinoiTestDb>();
		Store.Reset(NewObject<UTarinoiSqliteDocumentStore>());
		Entities.Payloads.Reset();
		Store->Setup(Fixture->Db, &Entities);
	});

	AfterEach([this]()
	{
		Store.Reset();
		Fixture.Reset();
	});

	It("loads a card's parsed payload", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("card1"), .Payload = TEXT("{\"data\":{\"line\":\"Hello\"}}")});
		const TSharedPtr<FJsonObject> Card = Store->LoadCard(TEXT("col1"), TEXT("card1"));
		TestTrue("found", Card.IsValid());
		TestEqualSensitive("line", TarinoiJson::Str(TarinoiJson::Obj(Card, TEXT("data")), TEXT("line")), FString(TEXT("Hello")));
	});

	It("returns null for a missing card", [this]()
	{
		TestFalse("missing", Store->LoadCard(TEXT("col1"), TEXT("nope")).IsValid());
	});

	It("respects the active filter", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("card1"), .bArchived = true});
		TestFalse("archived", Store->LoadCard(TEXT("col1"), TEXT("card1")).IsValid());
	});

	It("prefers the buffer layer", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("card1"), .Payload = TEXT("{\"v\":\"main\"}")});
		Fixture->Insert({.DocumentId = TEXT("card1"), .LayerId = TarinoiLayerFilter::BufferLayer, .Payload = TEXT("{\"v\":\"buffer\"}")});
		TestEqualSensitive("buffer", TarinoiJson::Str(Store->LoadCard(TEXT("col1"), TEXT("card1")), TEXT("v")), FString(TEXT("buffer")));
	});

	It("finds a document without a collection, and disambiguates with one", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("doc"), .CollectionId = TEXT("a"), .Payload = TEXT("{\"v\":\"a\"}")});
		Fixture->Insert({.DocumentId = TEXT("doc"), .CollectionId = TEXT("b"), .Payload = TEXT("{\"v\":\"b\"}")});
		TestTrue("any", Store->GetDocument(TEXT("doc")).IsValid());
		TestEqualSensitive("b", TarinoiJson::Str(Store->GetDocument(TEXT("doc"), TEXT("b")), TEXT("v")), FString(TEXT("b")));
	});

	It("returns null for blank input", [this]()
	{
		TestFalse("blank", Store->GetDocument(FString()).IsValid());
	});

	It("logs a malformed payload and treats it as missing", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("bad"), .Payload = TEXT("{not json")});
		AddExpectedError(TEXT("unreadable payload"));
		TestFalse("missing", Store->LoadCard(TEXT("col1"), TEXT("bad")).IsValid());
	});

	It("locates a card by id alone, reporting its collection", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("target"), .CollectionId = TEXT("other-board"), .Payload = TEXT("{\"v\":1}")});
		Fixture->Insert({.DocumentId = TEXT("target"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("template")});

		FString CollectionId;
		TSharedPtr<FJsonObject> Card;
		TestTrue("found", Store->LocateCard(TEXT("target"), CollectionId, Card));
		TestEqualSensitive("collection", CollectionId, FString(TEXT("other-board")));
		TestTrue("payload", Card.IsValid());
		TestFalse("missing", Store->LocateCard(TEXT("nope"), CollectionId, Card));
	});

	It("reads entities from the cache, not the database", [this]()
	{
		TSharedPtr<FJsonObject> Hero = MakeShared<FJsonObject>();
		Hero->SetStringField(TEXT("label"), TEXT("Hero"));
		Entities.Payloads.Add(TEXT("hero"), Hero);
		TestTrue("cached", Store->GetEntity(TEXT("hero")) == Hero);
		TestFalse("unknown", Store->GetEntity(TEXT("villain")).IsValid());
	});

	It("lists only start cards, with their labels, and skips the start template", [this]()
	{
		Fixture->Insert({.DocumentId = TEXT("s1"), .Payload = StartCard(TEXT("Harbour"))});
		Fixture->Insert({.DocumentId = TEXT("line1"), .Payload = TEXT("{\"base_ref\":\"line\"}")});
		Fixture->Insert({.DocumentId = TEXT("tpl"), .DocumentType = TEXT("card-template"), .Payload = StartCard(TEXT("Template"))});
		Fixture->Insert({.DocumentId = TEXT("s2"), .Payload = StartCard(TEXT("Gone")), .bArchived = true});

		const TArray<FTarinoiStartCardRow> Rows = Store->QueryStartCards();
		if (TestEqual("count", Rows.Num(), 1))
		{
			TestEqualSensitive("id", Rows[0].DocumentId, FString(TEXT("s1")));
			TestEqualSensitive("label", Rows[0].Label, FString(TEXT("Harbour")));
			TestEqualSensitive("collection", Rows[0].CollectionId, FString(TEXT("col1")));
		}
	});

	It("answers empty rather than failing when the database is closed", [this]()
	{
		Fixture->Db->Close();
		TestFalse("card", Store->LoadCard(TEXT("col1"), TEXT("x")).IsValid());
		TestEqual("starts", Store->QueryStartCards().Num(), 0);
	});
}

#endif
