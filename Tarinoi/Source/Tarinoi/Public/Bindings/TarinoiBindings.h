// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Bindings/TarinoiValue.h"
#include "CoreMinimal.h"
#include "TarinoiTypes.h"
#include "UObject/Object.h"

#include "TarinoiBindings.generated.h"

/**
 * Implements the functions an author calls as Fn.collection.Name(...).
 *
 * Normally you don't subclass this directly: codegen emits one subclass per authored function
 * collection, with a BlueprintNativeEvent per function and a typed TryInvoke, and you implement
 * those events in C++ or in a Blueprint child class.
 *
 * Subclassing it by hand also works. The default TryInvoke finds a UFUNCTION with the authored
 * name through reflection and converts arguments to its parameter types (FTarinoiValue, bool,
 * numbers, FString, FName, UObject*), which suits a quick prototype. Generated bindings are
 * better for real work: a renamed function becomes a compile error instead of a runtime one.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class TARINOI_API UTarinoiFunctionCollection : public UObject
{
	GENERATED_BODY()

public:
	/** Whether a function of this name exists. */
	virtual bool HasFunction(FName Name) const;

	/**
	 * Calls a function. Returns false if there is no such function; the dispatcher logs that.
	 * An arity mismatch is logged here and returns true with no result.
	 */
	virtual bool TryInvoke(FName Name, const TArray<FTarinoiValue>& Args, FTarinoiValue& OutResult);

protected:
	/** Logs an actionable error and returns false when the authored call passed the wrong number of arguments. */
	bool ArityMatches(FName Name, const TArray<FTarinoiValue>& Args, int32 Expected) const;

	/** Logs that a declared function has not been implemented. For generated defaults. */
	void LogUnimplemented(const TCHAR* Function) const;

private:
	UFunction* FindBindingFunction(FName Name) const;
};

/**
 * Supplies the game state an author reads and writes as Var.collection.name.
 *
 * Codegen emits one subclass per authored variable collection, with a typed property per
 * variable, so the defaults below are rarely called. They read and write a property whose name
 * matches the authored variable name (as written, or in PascalCase), which is enough for a
 * hand-written or Blueprint class. Override both in Blueprint or C++ to keep variables somewhere
 * else entirely, such as a save game.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class TARINOI_API UTarinoiVariableCollection : public UObject
{
	GENERATED_BODY()

public:
	/** The authored collection identifier this class binds to, or "" if not generated. */
	virtual FString GetCollectionIdentifier() const { return FString(); }

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tarinoi")
	FTarinoiValue GetVariable(FName Name);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tarinoi")
	void SetVariable(FName Name, const FTarinoiValue& Value);

protected:
	FProperty* FindVariableProperty(FName Name) const;
};

/**
 * Supplies the game objects an author refers to as Ent.collection.name. What an entity is stays
 * the game's decision; Tarinoi only hands the object back to your own functions.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class TARINOI_API UTarinoiEntityCollection : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tarinoi")
	UObject* GetEntity(FName Name);
};

/**
 * Maps authored collections onto the game objects that implement them.
 *
 * Keys are the collection's machine identifier, never its display label. A collection labelled
 * "Global State" might have the identifier "global", and authored expressions say Fn.global....
 * Binding against the label fails only at dialogue time, so it is worth getting right up front.
 * Keys are case-sensitive, like the identifiers themselves.
 *
 * The runtime holds this by reference, so bindings registered after configuration take effect.
 */
UCLASS(BlueprintType)
class TARINOI_API UTarinoiBindings : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Bindings")
	void BindFunctions(const FString& CollectionIdentifier, UTarinoiFunctionCollection* Functions);

	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Bindings")
	void BindVariables(const FString& CollectionIdentifier, UTarinoiVariableCollection* Variables);

	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Bindings")
	void BindEntities(const FString& CollectionIdentifier, UTarinoiEntityCollection* Entities);

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Bindings")
	UTarinoiFunctionCollection* GetFunctions(const FString& CollectionIdentifier) const;

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Bindings")
	UTarinoiVariableCollection* GetVariables(const FString& CollectionIdentifier) const;

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Bindings")
	UTarinoiEntityCollection* GetEntities(const FString& CollectionIdentifier) const;

	/** Every bound identifier, for diagnostics. */
	TArray<FString> GetBoundFunctionCollections() const;
	TArray<FString> GetBoundVariableCollections() const;
	TArray<FString> GetBoundEntityCollections() const;

	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Bindings")
	void Clear();

	/**
	 * Binds whatever the generated code supplies on its own, for any collection still unbound: the
	 * generated variable classes (a typed property per variable), and the scaffolded core functions
	 * for Fn.tarinoi.*. Content that only uses core functions then plays with no bindings written.
	 * Returns what it bound. The quickstart calls this after the game's own bindings; a real game
	 * usually binds explicitly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Bindings")
	TArray<FString> BindGeneratedDefaults(const FString& ClassPrefix = TEXT("Tarinoi"));

private:
	bool Validate(const FString& CollectionIdentifier, const UObject* Object, const TCHAR* Kind) const;
	void Retain(UObject* Previous, UObject* Next);

	// The maps are not UPROPERTYs because reflection only offers case-insensitive string keys.
	// Everything they point at is kept alive through Retained instead.
	TTarinoiMap<UTarinoiFunctionCollection*> Functions;
	TTarinoiMap<UTarinoiVariableCollection*> Variables;
	TTarinoiMap<UTarinoiEntityCollection*> Entities;

	UPROPERTY()
	TArray<TObjectPtr<UObject>> Retained;
};
