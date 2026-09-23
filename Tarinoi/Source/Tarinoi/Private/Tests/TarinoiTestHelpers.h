// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace TarinoiTest
{
	/**
	 * Compares string arrays exactly: order and case both matter. (The framework has no array
	 * TestEqual, and its FString TestEqual ignores case.)
	 */
	inline bool Strings(FAutomationTestBase& Test, const TCHAR* What, const TArray<FString>& Actual, const TArray<FString>& Expected)
	{
		return Test.TestEqualSensitive(What, FString::Join(Actual, TEXT(" | ")), FString::Join(Expected, TEXT(" | ")));
	}
}

#endif
