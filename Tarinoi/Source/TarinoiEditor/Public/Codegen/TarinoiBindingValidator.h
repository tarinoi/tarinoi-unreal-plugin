// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Codegen/TarinoiCodegen.h"
#include "CoreMinimal.h"

/** One difference between the synced content and the compiled bindings. */
struct TARINOIEDITOR_API FTarinoiBindingIssue
{
	/** True when regenerating would break code that compiles now: a member removed or changed. Additions are not breaking. */
	bool bBreaking = false;
	FString Message;
};

/**
 * Compares the synced declarations with the generated classes compiled into the project, and
 * reports what regenerating would change.
 *
 * It reads the compiled classes through UE reflection rather than scanning the generated text,
 * and it never blocks regeneration: the compiler already reports anything that breaks, and
 * refusing to write would only leave the developer with stale bindings.
 */
namespace TarinoiBindingValidator
{
	/**
	 * ScaffoldHeader is where the core functions scaffold is expected; its overrides are read from
	 * the source text, since a C++ override of a BlueprintNativeEvent is invisible to reflection.
	 */
	TARINOIEDITOR_API TArray<FTarinoiBindingIssue> Validate(const FTarinoiCodegenModel& Model, const FString& ScaffoldHeader,
		const FString& ClassPrefix = TEXT("Tarinoi"));
}
