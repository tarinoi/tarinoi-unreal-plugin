// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Bindings/TarinoiBindings.h"
#include "CoreMinimal.h"

#include "TarinoiTestBindings.generated.h"

/**
 * A hand-written function collection, dispatched through the reflection default. Records what it
 * was called with, so tests can assert on arguments and on what did not run.
 */
UCLASS(NotBlueprintable)
class UTarinoiTestFunctions : public UTarinoiFunctionCollection
{
	GENERATED_BODY()

public:
	TArray<FString> Calls;
	TArray<FTarinoiValue> LastArgs;

	UFUNCTION()
	bool RecordArgs(const FTarinoiValue& A, const FTarinoiValue& B)
	{
		Calls.Add(TEXT("RecordArgs"));
		LastArgs = {A, B};
		return true;
	}

	UFUNCTION()
	bool ReturnTrue() { Calls.Add(TEXT("ReturnTrue")); return true; }

	UFUNCTION()
	bool ReturnFalse() { Calls.Add(TEXT("ReturnFalse")); return false; }

	UFUNCTION()
	FString ReturnString() { Calls.Add(TEXT("ReturnString")); return TEXT("pin_a"); }

	UFUNCTION()
	FTarinoiValue Identity(const FTarinoiValue& Value) { Calls.Add(TEXT("Identity")); return Value; }

	UFUNCTION()
	void SetTrue(const FTarinoiValue& Reference) { Calls.Add(TEXT("SetTrue")); Reference.Write(FTarinoiValue::MakeBool(true)); }

	UFUNCTION()
	bool Read(const FTarinoiValue& Value) { Calls.Add(TEXT("Read")); return Value.ToBool(); }

	/** Typed parameters, converted by the reflection default. */
	UFUNCTION()
	double Add(double A, int32 B) { Calls.Add(TEXT("Add")); return A + B; }

	UFUNCTION()
	FString Greet(const FString& Name, bool bLoud) { Calls.Add(TEXT("Greet")); return bLoud ? Name.ToUpper() : Name; }
};

/** A hand-written variable collection, read and written through the reflection default. */
UCLASS(NotBlueprintable)
class UTarinoiTestVariables : public UTarinoiVariableCollection
{
	GENERATED_BODY()

public:
	UPROPERTY()
	bool Met = false;

	UPROPERTY()
	double Gold = 0.0;

	/** Authored as "player_name": found through the PascalCase fallback. */
	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	FTarinoiValue Anything;
};

/** Returns itself for "hero" and nothing for anything else. */
UCLASS(NotBlueprintable)
class UTarinoiTestEntities : public UTarinoiEntityCollection
{
	GENERATED_BODY()

public:
	virtual UObject* GetEntity_Implementation(FName Name) override
	{
		return Name == TEXT("hero") ? this : nullptr;
	}
};
