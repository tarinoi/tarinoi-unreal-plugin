// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Sync/TarinoiNdjson.h"
#include "TarinoiJson.h"

BEGIN_DEFINE_SPEC(FTarinoiNdjsonSpec, "Tarinoi.Sync.Ndjson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiNdjsonSpec)

void FTarinoiNdjsonSpec::Define()
{
	It("yields nothing for an empty body", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(FString());
		TestEqual("documents", Page.Documents.Num(), 0);
		TestFalse("cursor", Page.Cursor.IsSet());
	});

	It("reads documents with no cursor as the last page", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"document_id\":\"a\"}\n{\"document_id\":\"b\"}"));
		TestEqual("documents", Page.Documents.Num(), 2);
		TestFalse("cursor", Page.Cursor.IsSet());
	});

	It("reads documents followed by a cursor", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"document_id\":\"a\"}\n{\"cursor\":\"abc\"}"));
		TestEqual("documents", Page.Documents.Num(), 1);
		TestEqualSensitive("cursor", Page.Cursor.Get(FString()), FString(TEXT("abc")));
	});

	It("reads a cursor-only page", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"cursor\":\"x\"}"));
		TestEqual("documents", Page.Documents.Num(), 0);
		TestTrue("cursor", Page.Cursor.IsSet());
	});

	It("reads a numeric cursor as text", [this]()
	{
		TestEqualSensitive("cursor", TarinoiNdjson::Parse(TEXT("{\"cursor\":12345}")).Cursor.Get(FString()), FString(TEXT("12345")));
	});

	It("ends pagination on a null cursor", [this]()
	{
		TestFalse("cursor", TarinoiNdjson::Parse(TEXT("{\"cursor\":null}")).Cursor.IsSet());
	});

	It("ignores blank lines and tolerates carriage returns", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("\r\n{\"document_id\":\"a\"}\r\n   \r\n\t\n{\"document_id\":\"b\"}\r\n"));
		TestEqual("documents", Page.Documents.Num(), 2);
	});

	It("skips a malformed line without losing the rest", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"document_id\":\"a\"}\n{broken\n{\"document_id\":\"b\"}"));
		TestEqual("documents", Page.Documents.Num(), 2);
	});

	It("keeps a document that happens to carry a cursor field", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"document_id\":\"a\",\"cursor\":\"not-a-sentinel\"}"));
		TestEqual("documents", Page.Documents.Num(), 1);
		TestFalse("cursor", Page.Cursor.IsSet());
	});

	It("lets the last cursor line win", [this]()
	{
		TestEqualSensitive("cursor", TarinoiNdjson::Parse(TEXT("{\"cursor\":\"1\"}\n{\"cursor\":\"2\"}")).Cursor.Get(FString()), FString(TEXT("2")));
	});

	It("keeps payloads intact, including non-ASCII text", [this]()
	{
		const FTarinoiNdjsonPage Page = TarinoiNdjson::Parse(TEXT("{\"payload\":{\"data\":{\"line\":\"Hän sanoi \\\"hei\\\"\"}}}"));
		const TSharedPtr<FJsonObject> Data = TarinoiJson::Obj(TarinoiJson::Obj(Page.Documents[0], TEXT("payload")), TEXT("data"));
		TestEqualSensitive("line", TarinoiJson::Str(Data, TEXT("line")), FString(TEXT("Hän sanoi \"hei\"")));
	});
}

#endif
