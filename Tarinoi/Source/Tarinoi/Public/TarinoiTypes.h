// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * Case-sensitive string keys.
 *
 * FString hashing and equality ignore case, so a plain TMap<FString, ...> treats "Global" and
 * "global" as one key. Authored identifiers, document ids (mixed-case nanoids) and expression
 * source text are all case-sensitive, so every map or set keyed on them uses these instead.
 */
template <typename ValueType>
struct TTarinoiCaseSensitiveMapFuncs : BaseKeyFuncs<TPair<FString, ValueType>, FString, false>
{
	static const FString& GetSetKey(const TPair<FString, ValueType>& Element) { return Element.Key; }
	static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

struct FTarinoiCaseSensitiveSetFuncs : BaseKeyFuncs<FString, FString, false>
{
	static const FString& GetSetKey(const FString& Element) { return Element; }
	static bool Matches(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(const FString& Key) { return FCrc::StrCrc32(*Key); }
};

template <typename ValueType>
using TTarinoiMap = TMap<FString, ValueType, FDefaultSetAllocator, TTarinoiCaseSensitiveMapFuncs<ValueType>>;

using FTarinoiStringSet = TSet<FString, FTarinoiCaseSensitiveSetFuncs>;
