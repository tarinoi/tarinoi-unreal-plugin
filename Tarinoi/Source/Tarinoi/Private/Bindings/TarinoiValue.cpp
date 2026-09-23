// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Bindings/TarinoiValue.h"

#include "Bindings/TarinoiBindings.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"

FTarinoiValue FTarinoiValue::MakeBool(bool bValue)
{
	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::Bool;
	Value.BoolValue = bValue;
	return Value;
}

FTarinoiValue FTarinoiValue::MakeNumber(double Number)
{
	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::Number;
	Value.NumberValue = Number;
	return Value;
}

FTarinoiValue FTarinoiValue::MakeString(const FString& Text)
{
	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::String;
	Value.StringValue = Text;
	return Value;
}

FTarinoiValue FTarinoiValue::MakeObject(UObject* Object)
{
	if (!Object)
	{
		return None();
	}

	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::Object;
	Value.ObjectValue = Object;
	return Value;
}

FTarinoiValue FTarinoiValue::MakeJson(const TSharedPtr<FJsonObject>& Object)
{
	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::Json;
	Value.JsonValue = Object.IsValid() ? Object : MakeShared<FJsonObject>();
	return Value;
}

FTarinoiValue FTarinoiValue::MakeVariable(UTarinoiVariableCollection* Variables, const FString& InCollection, FName Name)
{
	FTarinoiValue Value;
	Value.Type = ETarinoiValueType::Variable;
	Value.ObjectValue = Variables;
	Value.Collection = InCollection;
	Value.VariableName = Name;
	return Value;
}

FTarinoiValue FTarinoiValue::FromJson(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return None();
	}

	switch (Json->Type)
	{
	case EJson::Boolean: return MakeBool(Json->AsBool());
	case EJson::Number: return MakeNumber(Json->AsNumber());
	case EJson::String: return MakeString(Json->AsString());
	default: return None();
	}
}

FTarinoiValue FTarinoiValue::Resolve() const
{
	if (Type != ETarinoiValueType::Variable)
	{
		return *this;
	}

	UTarinoiVariableCollection* Variables = Cast<UTarinoiVariableCollection>(ObjectValue);
	if (!Variables)
	{
		return None();
	}

	FTarinoiValue Current = Variables->GetVariable(VariableName);
	// A variable holding a reference would make reads unbounded; one level is all there is.
	return Current.Type == ETarinoiValueType::Variable ? None() : Current;
}

bool FTarinoiValue::Write(const FTarinoiValue& NewValue) const
{
	UTarinoiVariableCollection* Variables = Type == ETarinoiValueType::Variable ? Cast<UTarinoiVariableCollection>(ObjectValue) : nullptr;
	if (!Variables)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Cannot write %s to %s: it is not a Var.* reference."), *NewValue.Describe(), *Describe());
		return false;
	}

	Variables->SetVariable(VariableName, NewValue.Resolve());
	return true;
}

bool FTarinoiValue::IsTruthy() const
{
	const FTarinoiValue Value = Resolve();
	switch (Value.Type)
	{
	case ETarinoiValueType::Bool: return Value.BoolValue;
	case ETarinoiValueType::Number: return Value.NumberValue != 0.0;
	case ETarinoiValueType::String: return !Value.StringValue.IsEmpty();
	case ETarinoiValueType::Object: return Value.ObjectValue != nullptr;
	case ETarinoiValueType::Json: return Value.JsonValue.IsValid() && Value.JsonValue->Values.Num() > 0;
	default: return false;
	}
}

bool FTarinoiValue::ToBool(bool Fallback) const
{
	const FTarinoiValue Value = Resolve();
	switch (Value.Type)
	{
	case ETarinoiValueType::Bool: return Value.BoolValue;
	case ETarinoiValueType::Number: return Value.NumberValue != 0.0;
	case ETarinoiValueType::String:
		return !Value.StringValue.IsEmpty() && Value.StringValue != TEXT("false") && Value.StringValue != TEXT("0");
	case ETarinoiValueType::Object: return Value.ObjectValue != nullptr;
	case ETarinoiValueType::Json: return true;
	default: return Fallback;
	}
}

double FTarinoiValue::ToNumber(double Fallback) const
{
	const FTarinoiValue Value = Resolve();
	switch (Value.Type)
	{
	case ETarinoiValueType::Bool: return Value.BoolValue ? 1.0 : 0.0;
	case ETarinoiValueType::Number: return Value.NumberValue;
	case ETarinoiValueType::String:
	{
		const FString Text = Value.StringValue.TrimStartAndEnd();
		return Text.IsNumeric() ? FCString::Atod(*Text) : Fallback;
	}
	default: return Fallback;
	}
}

FString FTarinoiValue::ToString(const FString& Fallback) const
{
	const FTarinoiValue Value = Resolve();
	switch (Value.Type)
	{
	case ETarinoiValueType::Bool: return Value.BoolValue ? TEXT("true") : TEXT("false");
	case ETarinoiValueType::Number: return TarinoiJson::NumberToString(Value.NumberValue);
	case ETarinoiValueType::String: return Value.StringValue;
	case ETarinoiValueType::Object: return GetNameSafe(Value.ObjectValue);
	case ETarinoiValueType::Json: return TarinoiJson::Stringify(Value.JsonValue);
	default: return Fallback;
	}
}

FString FTarinoiValue::Describe() const
{
	switch (Type)
	{
	case ETarinoiValueType::None: return TEXT("nothing");
	case ETarinoiValueType::Bool: return FString::Printf(TEXT("%s (bool)"), BoolValue ? TEXT("true") : TEXT("false"));
	case ETarinoiValueType::Number: return FString::Printf(TEXT("%s (number)"), *TarinoiJson::NumberToString(NumberValue));
	case ETarinoiValueType::String: return FString::Printf(TEXT("\"%s\" (string)"), *StringValue);
	case ETarinoiValueType::Object: return FString::Printf(TEXT("%s (object)"), *GetNameSafe(ObjectValue));
	case ETarinoiValueType::Json: return TEXT("card data");
	case ETarinoiValueType::Variable:
	{
		const FTarinoiValue Current = Resolve();
		return FString::Printf(TEXT("Var.%s.%s = %s"), *Collection, *VariableName.ToString(),
			Current.IsNone() ? TEXT("unset") : *Current.Describe());
	}
	}
	return FString();
}

UObject* UTarinoiValueLibrary::ToObject(const FTarinoiValue& Value)
{
	const FTarinoiValue Resolved = Value.Resolve();
	return Resolved.Type == ETarinoiValueType::Object ? Resolved.ObjectValue.Get() : nullptr;
}
