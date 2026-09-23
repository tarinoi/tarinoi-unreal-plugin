// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TarinoiSettings.h"

BEGIN_DEFINE_SPEC(FTarinoiSettingsSpec, "Tarinoi.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiSettingsSpec)

void FTarinoiSettingsSpec::Define()
{
	Describe("ProjectIdFromApiPath", [this]()
	{
		It("takes the segment before /documents", [this]()
		{
			const TCHAR* Paths[] = {
				TEXT("https://app.tarinoi.com/api/projects/proj123/documents"),
				TEXT("https://app.tarinoi.com/api/projects/proj123/documents/"),
				TEXT("https://app.tarinoi.com/api/projects/proj123"),
				TEXT("https://app.tarinoi.com/api/projects/proj123/"),
				TEXT("  https://app.tarinoi.com/api/projects/proj123/documents  "),
				TEXT("https://app.tarinoi.com/api/projects/proj123/DOCUMENTS"),
			};
			for (const TCHAR* Path : Paths)
			{
				TestEqualSensitive(Path, UTarinoiSettings::ProjectIdFromApiPath(Path), FString(TEXT("proj123")));
			}
		});

		It("gives nothing when the path cannot yield an id", [this]()
		{
			const TCHAR* Paths[] = {TEXT(""), TEXT("   "), TEXT("https://app.tarinoi.com"), TEXT("https://app.tarinoi.com/documents")};
			for (const TCHAR* Path : Paths)
			{
				TestEqual(Path, UTarinoiSettings::ProjectIdFromApiPath(Path), FString());
			}
		});
	});

	It("has defaults that are safe for a fresh project", [this]()
	{
		const UTarinoiSettings* Defaults = GetDefault<UTarinoiSettings>();
		TestFalse("not offline", NewObject<UTarinoiSettings>()->bOfflineMode);
		TestFalse("not polling", NewObject<UTarinoiSettings>()->bPollEnabled);
		TestNotNull("CDO", Defaults);
	});
}

#endif
