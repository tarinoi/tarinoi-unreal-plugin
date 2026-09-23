// Tarinoi core functions 0.0.1: reference implementation.
// Scaffolded by Tarinoi from the synced content of project 'fixture'.
//
// This file is yours: edit it freely, regenerating never overwrites it. Delete it (and
// its header or source) to get a fresh copy the next time you regenerate bindings.
//
// These are the Fn.tarinoi.* functions in-app playback evaluates for real. Keep the same
// semantics, in particular the defaults for unset variables, so what an author verified
// in playback holds in the game.

#pragma once

#include "CoreMinimal.h"
#include "TarinoiFixtureGeneratedFunctions.h"

#include "TarinoiFixtureCoreFunctions.generated.h"

/**
 * Tarinoi's core functions, callable as Fn.tarinoi.*
 *
 * Bind an instance under "tarinoi". The quickstart binds this class on its own.
 */
UCLASS(Blueprintable, meta = (TarinoiCoreFunctionsVersion = "0.0.1"))
class TARINOITESTS_API UTarinoiFixtureCoreFunctions : public UTarinoiFixtureTarinoiFunctions
{
	GENERATED_BODY()

public:
	/** Set a flag. */
	virtual void SetFlag_Implementation(const FTarinoiValue& FlagRef) override;

	/** Clear a flag. */
	virtual void ClearFlag_Implementation(const FTarinoiValue& FlagRef) override;

	/** Toggle a flag: clear if set, set if unset. */
	virtual void ToggleFlag_Implementation(const FTarinoiValue& FlagRef) override;

	/** Set a counter to a value, which must be a number. */
	virtual void SetCounter_Implementation(const FTarinoiValue& CounterRef, const FTarinoiValue& Value) override;

	/** Increment a counter by delta, which must be a number. Use negative numbers to decrement. An unset counter is treated as a 0. */
	virtual void IncrementCounter_Implementation(const FTarinoiValue& CounterRef, const FTarinoiValue& Delta) override;

	/** Set a text variable to a value, which must be a string. */
	virtual void SetText_Implementation(const FTarinoiValue& TextRef, const FTarinoiValue& Value) override;

	/** Returns true if the flag is set. An unset flag is treated as clear. Use the NOT operator in conditions to negate. */
	virtual bool FlagIsSet_Implementation(const FTarinoiValue& FlagRef) override;

	/** Returns true if a and b equal each other. Both must be numbers. An unset counter is treated as a 0. Use the NOT operator in conditions to negate. */
	virtual bool NumberEquals_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

	/** Returns true if a >= b. Both must be numbers. An unset counter is treated as a 0. */
	virtual bool NumberAtLeast_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

	/** Returns true if a > b. Both must be numbers. An unset counter is treated as a 0. */
	virtual bool NumberGreaterThan_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

	/** Returns true if a <= b. Both must be numbers. An unset counter is treated as a 0. */
	virtual bool NumberAtMost_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

	/** Returns true if a < b. Both must be numbers. An unset counter is treated as a 0. */
	virtual bool NumberLessThan_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

	/** Returns true if a and b equal each other. Both must be strings. An unset text variable is treated as empty. Use the NOT operator in conditions to negate. */
	virtual bool StringEquals_Implementation(const FTarinoiValue& A, const FTarinoiValue& B) override;

protected:
	// The dispatcher passes a Var.* argument as a variable reference, so the function can
	// write through it, and anything else as a plain value. Unset variables read as false,
	// 0 and "", as in-app playback has them. A value of the wrong type is an authoring
	// error: it is logged, and the default is used instead.

	/** Writes through a Var.* reference. */
	static void Write(const FTarinoiValue& Reference, const FTarinoiValue& Value);

	/** Reads a boolean: a flag, or a literal. */
	static bool Flag(const FTarinoiValue& Value);

	/** Reads a number: a counter, a list item, or a literal. */
	static double Number(const FTarinoiValue& Value);

	/** Reads a string: a text variable, a list item, or a literal. */
	static FString Text(const FTarinoiValue& Value);
};
