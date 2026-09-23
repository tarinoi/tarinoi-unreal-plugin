// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "TarinoiValue.generated.h"

class UTarinoiVariableCollection;

UENUM(BlueprintType)
enum class ETarinoiValueType : uint8
{
	None,
	Bool,
	Number,
	String,
	/** A reference to a game variable (Var.collection.name), not yet read. */
	Variable,
	/** A game object: what an entity binding returned for Ent.collection.name. */
	Object,
	/** Authored JSON: the current card, for Card.* */
	Json,
};

/**
 * A value flowing through an authored expression: a literal, a function's result, a list option,
 * an entity, or a reference to a game variable.
 *
 * Variable references are the one subtle case. When an author passes Var.collection.name to a
 * function, the function receives the reference, not the value, which is what lets it write back:
 * a SetFlag binding calls Write on its argument. A function that only reads should use the
 * To* conversions, which read through a reference automatically, so it works whether the author
 * wrote a variable or a literal.
 */
USTRUCT(BlueprintType)
struct TARINOI_API FTarinoiValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tarinoi")
	ETarinoiValueType Type = ETarinoiValueType::None;

	UPROPERTY()
	bool BoolValue = false;

	UPROPERTY()
	double NumberValue = 0.0;

	UPROPERTY()
	FString StringValue;

	/** The entity for Object; the variable collection for Variable. */
	UPROPERTY()
	TObjectPtr<UObject> ObjectValue;

	/** For Variable: the variable's authored name. */
	UPROPERTY()
	FName VariableName;

	/** For Variable: the collection identifier, for messages. */
	UPROPERTY()
	FString Collection;

	/** For Json. Not reflected: Blueprints read card data through the dialogue line instead. */
	TSharedPtr<FJsonObject> JsonValue;

	static FTarinoiValue None() { return FTarinoiValue(); }
	static FTarinoiValue MakeBool(bool bValue);
	static FTarinoiValue MakeNumber(double Value);
	static FTarinoiValue MakeString(const FString& Value);
	static FTarinoiValue MakeObject(UObject* Object);
	static FTarinoiValue MakeJson(const TSharedPtr<FJsonObject>& Object);
	static FTarinoiValue MakeVariable(UTarinoiVariableCollection* Variables, const FString& Collection, FName Name);

	/** A JSON scalar as a value: bool, number or string; anything else is None. */
	static FTarinoiValue FromJson(const TSharedPtr<FJsonValue>& Value);

	bool IsNone() const { return Type == ETarinoiValueType::None; }
	bool IsVariable() const { return Type == ETarinoiValueType::Variable; }

	/** Reads through a variable reference; any other value comes back unchanged. */
	FTarinoiValue Resolve() const;

	/** Writes through a variable reference. Returns false (and logs) for any other value. */
	bool Write(const FTarinoiValue& NewValue) const;

	/**
	 * Truthiness, as conditions use it: None is false; a number is true when non-zero; a string
	 * when non-empty; an object when set; card JSON when it has fields. Reads variables first.
	 */
	bool IsTruthy() const;

	/**
	 * Lenient conversions for storing a value in a typed slot. They read through variables and
	 * fall back rather than fail: "false" and "0" are false, a numeric string parses, and so on.
	 */
	bool ToBool(bool Fallback = false) const;
	double ToNumber(double Fallback = 0.0) const;
	FString ToString(const FString& Fallback = FString()) const;

	/** A description for log messages: the value and its type, or the reference and its value. */
	FString Describe() const;
};

/** Blueprint access to Tarinoi values, for bindings implemented in Blueprint. */
UCLASS()
class TARINOI_API UTarinoiValueLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "Make Tarinoi Value (Bool)"))
	static FTarinoiValue MakeBoolValue(bool bValue) { return FTarinoiValue::MakeBool(bValue); }

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "Make Tarinoi Value (Number)"))
	static FTarinoiValue MakeNumberValue(double Value) { return FTarinoiValue::MakeNumber(Value); }

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "Make Tarinoi Value (String)"))
	static FTarinoiValue MakeStringValue(const FString& Value) { return FTarinoiValue::MakeString(Value); }

	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "Make Tarinoi Value (Object)"))
	static FTarinoiValue MakeObjectValue(UObject* Object) { return FTarinoiValue::MakeObject(Object); }

	/** Reads as a boolean, through a variable reference if it is one. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "To Bool (Tarinoi Value)", CompactNodeTitle = "->", BlueprintAutocast))
	static bool ToBool(const FTarinoiValue& Value) { return Value.ToBool(); }

	/** Reads as a number, through a variable reference if it is one. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "To Number (Tarinoi Value)", CompactNodeTitle = "->", BlueprintAutocast))
	static double ToNumber(const FTarinoiValue& Value) { return Value.ToNumber(); }

	/** Reads as a string, through a variable reference if it is one. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value", meta = (DisplayName = "To String (Tarinoi Value)", CompactNodeTitle = "->", BlueprintAutocast))
	static FString ToString(const FTarinoiValue& Value) { return Value.ToString(); }

	/** The game object an entity reference resolved to, if any. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value")
	static UObject* ToObject(const FTarinoiValue& Value);

	/** Whether the author passed a variable (Var.collection.name) rather than a value. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value")
	static bool IsVariableReference(const FTarinoiValue& Value) { return Value.IsVariable(); }

	/** The current value behind a variable reference; any other value comes back unchanged. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi|Value")
	static FTarinoiValue ReadVariable(const FTarinoiValue& Reference) { return Reference.Resolve(); }

	/** Writes through a variable reference. Returns false if the value is not a reference. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi|Value")
	static bool WriteVariable(const FTarinoiValue& Reference, const FTarinoiValue& NewValue) { return Reference.Write(NewValue); }
};
