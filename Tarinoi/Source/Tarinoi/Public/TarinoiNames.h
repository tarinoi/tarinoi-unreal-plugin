// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

namespace TarinoiNames
{
	/**
	 * Converts an authored identifier (snake_case, and otherwise unconstrained) to PascalCase:
	 * separators (_ - space .) start a new word, other characters that C++ will not accept are
	 * dropped, a leading digit gets an underscore, and input with nothing usable becomes
	 * "Unnamed". Codegen names members this way, and the variable-collection default looks
	 * properties up the same way, so the two always agree.
	 */
	TARINOI_API FString ToPascal(const FString& Authored);
}
