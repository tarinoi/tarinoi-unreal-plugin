// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Small, forgiving readers for authored JSON.
 *
 * Card payloads are loosely shaped: optional fields are often present with an explicit null,
 * and numbers, strings and booleans turn up where another type was expected. Every reader here
 * treats null and the wrong type as absent rather than asserting, because a quirk in authored
 * content should degrade a dialogue, not crash a game.
 */
namespace TarinoiJson
{
	/** Parses a JSON object, or returns null (without logging) when the text is not one. */
	TARINOI_API TSharedPtr<FJsonObject> Parse(const FString& Text);

	/** Serialises an object or value compactly, on one line. */
	TARINOI_API FString Stringify(const TSharedPtr<FJsonObject>& Object);
	TARINOI_API FString Stringify(const TSharedPtr<FJsonValue>& Value);

	/** A field as text: strings as-is, numbers and booleans formatted, anything else "". */
	TARINOI_API FString Str(const TSharedPtr<FJsonObject>& Object, const FString& Key);
	TARINOI_API FString Str(const TSharedPtr<FJsonValue>& Value);

	/** A nested object, or null when the field is missing, null or not an object. */
	TARINOI_API TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Object, const FString& Key);

	/** A nested array, or null when the field is missing, null or not an array. */
	TARINOI_API const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& Object, const FString& Key);

	/** True only for an explicit JSON true. Missing, null and other types read as false. */
	TARINOI_API bool Flag(const TSharedPtr<FJsonObject>& Object, const FString& Key);

	/** A field that the service may send as a bool, a number, or a string ("true"/"1"). */
	TARINOI_API bool LooseBool(const TSharedPtr<FJsonObject>& Object, const FString& Key);

	/** Formats a number the way authored content expects: integers without a trailing ".0". */
	TARINOI_API FString NumberToString(double Value);
}
