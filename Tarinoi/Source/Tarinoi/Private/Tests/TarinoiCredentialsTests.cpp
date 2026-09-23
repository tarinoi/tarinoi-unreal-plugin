// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sync/TarinoiCredentials.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

BEGIN_DEFINE_SPEC(FTarinoiCredentialsSpec, "Tarinoi.Sync.Credentials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FString TempPath;
END_DEFINE_SPEC(FTarinoiCredentialsSpec)

void FTarinoiCredentialsSpec::Define()
{
	It("keeps the real file outside the project directory", [this]()
	{
		const FString Real = FPaths::ConvertRelativePathToFull(FTarinoiCredentials::FilePath());
		const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		TestFalse(FString::Printf(TEXT("%s is outside %s"), *Real, *Project), Real.StartsWith(Project));
	});

	Describe("with a temporary file", [this]()
	{
		BeforeEach([this]()
		{
			TempPath = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("Tarinoi"), FGuid::NewGuid().ToString(), TEXT("credentials"));
			FTarinoiCredentials::SetFilePathOverride(TempPath);
		});

		AfterEach([this]()
		{
			FTarinoiCredentials::SetFilePathOverride(FString());
			IFileManager::Get().Delete(*TempPath);
		});

		It("reads nothing when there is no file", [this]()
		{
			TestEqual("empty", FTarinoiCredentials::Read(FTarinoiCredentials::ApiKeyName), FString());
			TestFalse("has", FTarinoiCredentials::Has(FTarinoiCredentials::ApiKeyName));
		});

		It("round-trips a value", [this]()
		{
			TestTrue("wrote", FTarinoiCredentials::Write(TEXT("api_key"), TEXT("secret-123")));
			TestEqualSensitive("read", FTarinoiCredentials::Read(TEXT("api_key")), FString(TEXT("secret-123")));
			TestTrue("has", FTarinoiCredentials::Has(TEXT("api_key")));
		});

		It("replaces rather than duplicates, and keeps other keys", [this]()
		{
			FTarinoiCredentials::Write(TEXT("other"), TEXT("keep"));
			FTarinoiCredentials::Write(TEXT("api_key"), TEXT("one"));
			FTarinoiCredentials::Write(TEXT("api_key"), TEXT("two"));

			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *TempPath);
			TestEqual("two lines", Lines.Num(), 2);
			TestEqualSensitive("replaced", FTarinoiCredentials::Read(TEXT("api_key")), FString(TEXT("two")));
			TestEqualSensitive("kept", FTarinoiCredentials::Read(TEXT("other")), FString(TEXT("keep")));
		});

		It("matches keys case-insensitively and trims whitespace", [this]()
		{
			FFileHelper::SaveStringToFile(TEXT("  API_KEY =  padded  \n"), *TempPath);
			// "API_KEY =" has a space before the '=', so it is a different key.
			TestEqual("spaced key", FTarinoiCredentials::Read(TEXT("api_key")), FString());
			FFileHelper::SaveStringToFile(TEXT("  API_KEY=  padded  \n"), *TempPath);
			TestEqualSensitive("case and padding", FTarinoiCredentials::Read(TEXT("api_key")), FString(TEXT("padded")));
		});

		It("keeps values containing '=' intact", [this]()
		{
			FTarinoiCredentials::Write(TEXT("api_key"), TEXT("abc=def=="));
			TestEqualSensitive("value", FTarinoiCredentials::Read(TEXT("api_key")), FString(TEXT("abc=def==")));
		});

		It("clears only the named key", [this]()
		{
			FTarinoiCredentials::Write(TEXT("api_key"), TEXT("x"));
			FTarinoiCredentials::Write(TEXT("other"), TEXT("y"));
			TestTrue("removed", FTarinoiCredentials::Clear(TEXT("api_key")));
			TestFalse("gone", FTarinoiCredentials::Has(TEXT("api_key")));
			TestEqualSensitive("kept", FTarinoiCredentials::Read(TEXT("other")), FString(TEXT("y")));
			TestFalse("already absent", FTarinoiCredentials::Clear(TEXT("api_key")));
		});

#if PLATFORM_MAC || PLATFORM_LINUX
		It("is readable by its owner only", [this]()
		{
			FTarinoiCredentials::Write(TEXT("api_key"), TEXT("x"));
			struct stat Info;
			TestEqual("stat", stat(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(TempPath)), &Info), 0);
			TestEqual("mode 600", static_cast<int32>(Info.st_mode & 0777), 0600);
		});
#endif
	});
}

#endif
