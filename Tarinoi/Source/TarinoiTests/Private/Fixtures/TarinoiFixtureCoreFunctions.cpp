// Tarinoi core functions 0.0.1: reference implementation.
// Scaffolded by Tarinoi from the synced content of project 'fixture'.
//
// This file is yours: edit it freely, regenerating never overwrites it. Delete it (and
// its header or source) to get a fresh copy the next time you regenerate bindings.
//
// These are the Fn.tarinoi.* functions in-app playback evaluates for real. Keep the same
// semantics, in particular the defaults for unset variables, so what an author verified
// in playback holds in the game.

#include "TarinoiFixtureCoreFunctions.h"

#include "Tarinoi.h"

void UTarinoiFixtureCoreFunctions::SetFlag_Implementation(const FTarinoiValue& FlagRef)
{
	Write(FlagRef, FTarinoiValue::MakeBool(true));
}

void UTarinoiFixtureCoreFunctions::ClearFlag_Implementation(const FTarinoiValue& FlagRef)
{
	Write(FlagRef, FTarinoiValue::MakeBool(false));
}

void UTarinoiFixtureCoreFunctions::ToggleFlag_Implementation(const FTarinoiValue& FlagRef)
{
	Write(FlagRef, FTarinoiValue::MakeBool(!Flag(FlagRef)));
}

void UTarinoiFixtureCoreFunctions::SetCounter_Implementation(const FTarinoiValue& CounterRef, const FTarinoiValue& Value)
{
	Write(CounterRef, FTarinoiValue::MakeNumber(Number(Value)));
}

void UTarinoiFixtureCoreFunctions::IncrementCounter_Implementation(const FTarinoiValue& CounterRef, const FTarinoiValue& Delta)
{
	Write(CounterRef, FTarinoiValue::MakeNumber(Number(CounterRef) + Number(Delta)));
}

void UTarinoiFixtureCoreFunctions::SetText_Implementation(const FTarinoiValue& TextRef, const FTarinoiValue& Value)
{
	Write(TextRef, FTarinoiValue::MakeString(Text(Value)));
}

bool UTarinoiFixtureCoreFunctions::FlagIsSet_Implementation(const FTarinoiValue& FlagRef)
{
	return Flag(FlagRef);
}

bool UTarinoiFixtureCoreFunctions::NumberEquals_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Number(A) == Number(B);
}

bool UTarinoiFixtureCoreFunctions::NumberAtLeast_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Number(A) >= Number(B);
}

bool UTarinoiFixtureCoreFunctions::NumberGreaterThan_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Number(A) > Number(B);
}

bool UTarinoiFixtureCoreFunctions::NumberAtMost_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Number(A) <= Number(B);
}

bool UTarinoiFixtureCoreFunctions::NumberLessThan_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Number(A) < Number(B);
}

bool UTarinoiFixtureCoreFunctions::StringEquals_Implementation(const FTarinoiValue& A, const FTarinoiValue& B)
{
	return Text(A).Equals(Text(B), ESearchCase::CaseSensitive);
}

void UTarinoiFixtureCoreFunctions::Write(const FTarinoiValue& Reference, const FTarinoiValue& Value)
{
	if (!Reference.IsVariable())
	{
		UE_LOG(LogTarinoi, Error, TEXT("Core functions: expected a Var.* reference, got %s."), *Reference.Describe());
		return;
	}
	Reference.Write(Value);
}

bool UTarinoiFixtureCoreFunctions::Flag(const FTarinoiValue& Value)
{
	const FTarinoiValue Resolved = Value.Resolve();
	switch (Resolved.Type)
	{
		case ETarinoiValueType::None: return false;
		case ETarinoiValueType::Bool: return Resolved.BoolValue;
		default: break;
	}
	UE_LOG(LogTarinoi, Error, TEXT("Core functions: expected a boolean, got %s."), *Value.Describe());
	return false;
}

double UTarinoiFixtureCoreFunctions::Number(const FTarinoiValue& Value)
{
	const FTarinoiValue Resolved = Value.Resolve();
	switch (Resolved.Type)
	{
		case ETarinoiValueType::None: return 0.0;
		case ETarinoiValueType::Number: return Resolved.NumberValue;
		default: break;
	}
	UE_LOG(LogTarinoi, Error, TEXT("Core functions: expected a number, got %s."), *Value.Describe());
	return 0.0;
}

FString UTarinoiFixtureCoreFunctions::Text(const FTarinoiValue& Value)
{
	const FTarinoiValue Resolved = Value.Resolve();
	switch (Resolved.Type)
	{
		case ETarinoiValueType::None: return FString();
		case ETarinoiValueType::String: return Resolved.StringValue;
		default: break;
	}
	UE_LOG(LogTarinoi, Error, TEXT("Core functions: expected a string, got %s."), *Value.Describe());
	return FString();
}
