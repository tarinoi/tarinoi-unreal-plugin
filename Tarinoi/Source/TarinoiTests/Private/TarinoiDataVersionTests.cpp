// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/TarinoiDataVersion.h"

BEGIN_DEFINE_SPEC(FTarinoiDataVersionSpec, "Tarinoi.Data.DataVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiDataVersionSpec)

void FTarinoiDataVersionSpec::Define()
{
	Describe("TryParse", [this]()
	{
		It("parses three numeric components", [this]()
		{
			int32 Major, Minor, Patch;
			TestTrue("parsed", FTarinoiDataVersion::TryParse(TEXT("12.3.45"), Major, Minor, Patch));
			TestEqual("major", Major, 12);
			TestEqual("minor", Minor, 3);
			TestEqual("patch", Patch, 45);
		});

		It("rejects the wrong number of components", [this]()
		{
			int32 Major, Minor, Patch;
			TestFalse("two", FTarinoiDataVersion::TryParse(TEXT("1.0"), Major, Minor, Patch));
			TestFalse("four", FTarinoiDataVersion::TryParse(TEXT("1.0.0.0"), Major, Minor, Patch));
			TestFalse("empty", FTarinoiDataVersion::TryParse(TEXT(""), Major, Minor, Patch));
			TestFalse("empty middle", FTarinoiDataVersion::TryParse(TEXT("1..0"), Major, Minor, Patch));
		});

		It("rejects signs, whitespace and letters", [this]()
		{
			int32 Major, Minor, Patch;
			TestFalse("sign", FTarinoiDataVersion::TryParse(TEXT("+1.0.0"), Major, Minor, Patch));
			TestFalse("negative", FTarinoiDataVersion::TryParse(TEXT("1.-1.0"), Major, Minor, Patch));
			TestFalse("space", FTarinoiDataVersion::TryParse(TEXT("1. 0.0"), Major, Minor, Patch));
			TestFalse("letters", FTarinoiDataVersion::TryParse(TEXT("1.0.0a"), Major, Minor, Patch));
		});
	});

	Describe("Check", [this]()
	{
		It("accepts the supported version", [this]()
		{
			FTarinoiDataVersion Check;
			TestEqual("exact", Check.Check(FTarinoiDataVersion::SupportedVersion), FString());
		});

		It("does not check an unversioned document", [this]()
		{
			FTarinoiDataVersion Check;
			TestEqual("empty", Check.Check(FString()), FString());
		});

		It("treats an unparseable version as compatible, with a warning", [this]()
		{
			FTarinoiDataVersion Check;
			AddExpectedMessage(TEXT("unparseable data_version"), ELogVerbosity::Warning);
			TestEqual("garbage", Check.Check(TEXT("banana")), FString());
		});

		It("passes a minor or patch difference", [this]()
		{
			FTarinoiDataVersion Check;
			AddExpectedMessage(TEXT("minor data format mismatch"), ELogVerbosity::Warning);
			TestEqual("minor", Check.Check(TEXT("2.7.0")), FString());
			TestEqual("patch", Check.Check(TEXT("2.0.9")), FString());
		});

		It("fails a major difference, every time it is seen", [this]()
		{
			FTarinoiDataVersion Check;
			AddExpectedError(TEXT("MAJOR data format mismatch"), EAutomationExpectedErrorFlags::Contains, 1);
			const FString First = Check.Check(TEXT("1.0.0"));
			const FString Second = Check.Check(TEXT("1.0.0"));
			TestTrue("first is an error", First.Contains(TEXT("MAJOR")));
			TestEqual("repeat keeps failing", Second, First);
		});
	});
}

#endif
