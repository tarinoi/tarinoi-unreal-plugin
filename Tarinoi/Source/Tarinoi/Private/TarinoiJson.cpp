// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiJson.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace TarinoiJson
{
	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return nullptr;
		}
		return Object;
	}

	FString Stringify(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Out;
	}

	FString Stringify(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("null");
		}

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}

	FString NumberToString(double Value)
	{
		// Integral values print without a fraction, so a selector returning 2 names pin "2",
		// not "2.0". Beyond 2^53 a double stops representing integers exactly anyway.
		if (FMath::IsFinite(Value) && FMath::Abs(Value) < 9007199254740992.0 && Value == FMath::FloorToDouble(Value))
		{
			return FString::Printf(TEXT("%lld"), static_cast<int64>(Value));
		}
		return FString::SanitizeFloat(Value);
	}

	FString Str(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return NumberToString(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		default:
			return FString();
		}
	}

	FString Str(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		return Object.IsValid() ? Str(Object->TryGetField(Key)) : FString();
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonValue> Field = Object->TryGetField(Key);
		return Field.IsValid() && Field->Type == EJson::Object ? Field->AsObject() : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonValue> Field = Object->TryGetField(Key);
		return Field.IsValid() && Field->Type == EJson::Array ? &Field->AsArray() : nullptr;
	}

	bool Flag(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue> Field = Object->TryGetField(Key);
		return Field.IsValid() && Field->Type == EJson::Boolean && Field->AsBool();
	}

	bool LooseBool(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		const TSharedPtr<FJsonValue> Field = Object->TryGetField(Key);
		if (!Field.IsValid())
		{
			return false;
		}

		switch (Field->Type)
		{
		case EJson::Boolean:
			return Field->AsBool();
		case EJson::Number:
			return Field->AsNumber() != 0.0;
		case EJson::String:
		{
			const FString Text = Field->AsString().TrimStartAndEnd().ToLower();
			return Text == TEXT("true") || Text == TEXT("1");
		}
		default:
			return false;
		}
	}
}
