// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"

namespace TarinoiLayerFilterTests
{
	using namespace TarinoiLayerFilter;

	struct FRowSpec
	{
		FString LayerId;
		FString DocumentId = TEXT("d1");
		FString CollectionId = TEXT("c1");
		bool bTombstone = false;
		bool bArchived = false;
		bool bMoved = false;
	};

	FTarinoiDocumentRow Row(const FRowSpec& Spec)
	{
		FTarinoiDocumentRow Out;
		Out.DocumentId = Spec.DocumentId;
		Out.CollectionId = Spec.CollectionId;
		Out.LayerId = Spec.LayerId;
		Out.DocumentType = TEXT("card");
		Out.UpdateKey = 1;
		Out.bTombstone = Spec.bTombstone;
		Out.bArchived = Spec.bArchived;
		Out.bMoved = Spec.bMoved;
		Out.Payload = Spec.LayerId;
		return Out;
	}

	TArray<FString> Ids(const TArray<FTarinoiDocumentRow>& Rows)
	{
		TArray<FString> Out;
		for (const FTarinoiDocumentRow& R : Rows)
		{
			Out.Add(R.DocumentId);
		}
		return Out;
	}
}

BEGIN_DEFINE_SPEC(FTarinoiLayerFilterSpec, "Tarinoi.Data.LayerFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiLayerFilterSpec)

void FTarinoiLayerFilterSpec::Define()
{
	using namespace TarinoiLayerFilterTests;

	Describe("Merge", [this]()
	{
		It("shows an active main row with no buffer", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({Row({MainLayer})}, false);
			TestEqual("count", Merged.Num(), 1);
		});

		It("hides an inactive main row, for each kind of inactivity", [this]()
		{
			TestEqual("archived", Merge({Row({.LayerId = MainLayer, .bArchived = true})}, false).Num(), 0);
			TestEqual("tombstoned", Merge({Row({.LayerId = MainLayer, .bTombstone = true})}, false).Num(), 0);
			TestEqual("moved", Merge({Row({.LayerId = MainLayer, .bMoved = true})}, false).Num(), 0);
		});

		It("lets an active buffer row override the main row", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({Row({MainLayer}), Row({BufferLayer})}, false);
			TestEqual("count", Merged.Num(), 1);
			TestEqual("buffer wins", Merged[0].LayerId, FString(BufferLayer));
		});

		It("lets an inactive buffer row suppress the main row entirely", [this]()
		{
			// The subtle one: the buffer row is a deletion marker, so main must not reappear.
			TestEqual("archived buffer", Merge({Row({MainLayer}), Row({.LayerId = BufferLayer, .bArchived = true})}, false).Num(), 0);
			TestEqual("tombstoned buffer", Merge({Row({MainLayer}), Row({.LayerId = BufferLayer, .bTombstone = true})}, false).Num(), 0);
		});

		It("shows a buffer row with no main row only when active", [this]()
		{
			TestEqual("active", Merge({Row({BufferLayer})}, false).Num(), 1);
			TestEqual("moved", Merge({Row({.LayerId = BufferLayer, .bMoved = true})}, false).Num(), 0);
		});

		It("ignores the buffer layer completely when committed-only", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({Row({MainLayer}), Row({BufferLayer})}, true);
			TestEqual("count", Merged.Num(), 1);
			TestEqual("main shown", Merged[0].LayerId, FString(MainLayer));
			TestEqual("uncommitted deletion ignored",
				Merge({Row({MainLayer}), Row({.LayerId = BufferLayer, .bArchived = true})}, true).Num(), 1);
		});

		It("merges documents independently", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({
				Row({.LayerId = MainLayer, .DocumentId = TEXT("d1")}),
				Row({.LayerId = BufferLayer, .DocumentId = TEXT("d1"), .bArchived = true}),
				Row({.LayerId = MainLayer, .DocumentId = TEXT("d2")}),
				Row({.LayerId = BufferLayer, .DocumentId = TEXT("d3")}),
			}, false);
			TarinoiTest::Strings(*this, TEXT("ids"), Ids(Merged), {TEXT("d2"), TEXT("d3")});
		});

		It("keeps the same id in different collections apart", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({
				Row({.LayerId = MainLayer, .DocumentId = TEXT("shared"), .CollectionId = TEXT("c1")}),
				Row({.LayerId = BufferLayer, .DocumentId = TEXT("shared"), .CollectionId = TEXT("c2"), .bArchived = true}),
			}, false);
			TestEqual("count", Merged.Num(), 1);
			TestEqual("c1 survives", Merged[0].CollectionId, FString(TEXT("c1")));
		});

		It("preserves input order", [this]()
		{
			const TArray<FTarinoiDocumentRow> Merged = Merge({
				Row({.LayerId = MainLayer, .DocumentId = TEXT("b")}),
				Row({.LayerId = MainLayer, .DocumentId = TEXT("a")}),
				Row({.LayerId = MainLayer, .DocumentId = TEXT("c")}),
			}, false);
			TarinoiTest::Strings(*this, TEXT("order"), Ids(Merged), {TEXT("b"), TEXT("a"), TEXT("c")});
		});

		It("passes an unknown active layer through", [this]()
		{
			TestEqual("future layer", Merge({Row({TEXT("tarinoi:some-future-layer")})}, false).Num(), 1);
		});

		It("returns nothing for nothing", [this]()
		{
			TestEqual("empty", Merge({}, false).Num(), 0);
		});
	});

	Describe("SQL and in-memory forms", [this]()
	{
		struct FCase
		{
			const TCHAR* Name;
			bool bHasMain, bMainInactive, bHasBuffer, bBufferInactive, bCommittedOnly;
		};

		static const FCase Cases[] = {
			{TEXT("main active only"), true, false, false, false, false},
			{TEXT("main inactive only"), true, true, false, false, false},
			{TEXT("buffer active only"), false, false, true, false, false},
			{TEXT("buffer inactive only"), false, false, true, true, false},
			{TEXT("both active"), true, false, true, false, false},
			{TEXT("active main, inactive buffer"), true, false, true, true, false},
			{TEXT("inactive main, active buffer"), true, true, true, false, false},
			{TEXT("both inactive"), true, true, true, true, false},
			{TEXT("committed-only ignores buffer"), true, false, true, true, true},
			{TEXT("committed-only, inactive main"), true, true, true, false, true},
		};

		for (const FCase& Case : Cases)
		{
			It(FString::Printf(TEXT("agree: %s"), Case.Name), [this, Case]()
			{
				FTarinoiTestDb Fixture(Case.bCommittedOnly);
				TArray<FTarinoiDocumentRow> Rows;

				if (Case.bHasMain)
				{
					Rows.Add(Row({.LayerId = MainLayer, .bArchived = Case.bMainInactive}));
					Fixture.Insert({.DocumentId = TEXT("d1"), .CollectionId = TEXT("c1"), .LayerId = MainLayer, .Payload = MainLayer, .bArchived = Case.bMainInactive});
				}
				if (Case.bHasBuffer)
				{
					Rows.Add(Row({.LayerId = BufferLayer, .bArchived = Case.bBufferInactive}));
					Fixture.Insert({.DocumentId = TEXT("d1"), .CollectionId = TEXT("c1"), .LayerId = BufferLayer, .Payload = BufferLayer, .bArchived = Case.bBufferInactive});
				}

				TArray<FString> FromMemory;
				for (const FTarinoiDocumentRow& R : Merge(Rows, Case.bCommittedOnly))
				{
					FromMemory.Add(R.Payload);
				}

				TarinoiTest::Strings(*this, TEXT("SQL filter and in-memory merge select the same documents"), Fixture.VisiblePayloads(), FromMemory);
			});
		}

		It("filters each inactivity flag in SQL", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("active")});
			Fixture.Insert({.DocumentId = TEXT("tombstoned"), .bTombstone = true});
			Fixture.Insert({.DocumentId = TEXT("archived"), .bArchived = true});
			Fixture.Insert({.DocumentId = TEXT("moved"), .bMoved = true});
			TarinoiTest::Strings(*this, TEXT("visible"), Fixture.VisibleDocumentIds(), {TEXT("active")});
		});
	});
}

#endif
