// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Bindings/TarinoiBindings.h"

#include "Tarinoi.h"
#include "TarinoiNames.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Converts a value into a property slot. Returns false for a type bindings cannot take. */
	bool WriteProperty(const FProperty* Property, void* Slot, const FTarinoiValue& Value)
	{
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (Struct->Struct == FTarinoiValue::StaticStruct())
			{
				*static_cast<FTarinoiValue*>(Slot) = Value;
				return true;
			}
			return false;
		}

		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			Bool->SetPropertyValue(Slot, Value.ToBool());
			return true;
		}

		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			if (Numeric->IsEnum())
			{
				return false;
			}
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Slot, Value.ToNumber());
			}
			else
			{
				Numeric->SetIntPropertyValue(Slot, static_cast<int64>(Value.ToNumber()));
			}
			return true;
		}

		if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			Str->SetPropertyValue(Slot, Value.ToString());
			return true;
		}

		if (const FNameProperty* Name = CastField<FNameProperty>(Property))
		{
			Name->SetPropertyValue(Slot, FName(*Value.ToString()));
			return true;
		}

		if (const FTextProperty* Text = CastField<FTextProperty>(Property))
		{
			Text->SetPropertyValue(Slot, FText::FromString(Value.ToString()));
			return true;
		}

		if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
		{
			const FTarinoiValue Resolved = Value.Resolve();
			UObject* Target = Resolved.Type == ETarinoiValueType::Object ? Resolved.ObjectValue.Get() : nullptr;
			if (Target && !Target->IsA(Object->PropertyClass))
			{
				Target = nullptr;
			}
			Object->SetObjectPropertyValue(Slot, Target);
			return true;
		}

		return false;
	}

	/** Reads a property slot as a value. Returns false for a type bindings cannot hand back. */
	bool ReadProperty(const FProperty* Property, const void* Slot, FTarinoiValue& OutValue)
	{
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			if (Struct->Struct == FTarinoiValue::StaticStruct())
			{
				OutValue = *static_cast<const FTarinoiValue*>(Slot);
				return true;
			}
			return false;
		}

		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			OutValue = FTarinoiValue::MakeBool(Bool->GetPropertyValue(Slot));
			return true;
		}

		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			if (Numeric->IsEnum())
			{
				return false;
			}
			OutValue = FTarinoiValue::MakeNumber(Numeric->IsFloatingPoint()
				? Numeric->GetFloatingPointPropertyValue(Slot)
				: static_cast<double>(Numeric->GetSignedIntPropertyValue(Slot)));
			return true;
		}

		if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			OutValue = FTarinoiValue::MakeString(Str->GetPropertyValue(Slot));
			return true;
		}

		if (const FNameProperty* Name = CastField<FNameProperty>(Property))
		{
			OutValue = FTarinoiValue::MakeString(Name->GetPropertyValue(Slot).ToString());
			return true;
		}

		if (const FTextProperty* Text = CastField<FTextProperty>(Property))
		{
			OutValue = FTarinoiValue::MakeString(Text->GetPropertyValue(Slot).ToString());
			return true;
		}

		if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
		{
			OutValue = FTarinoiValue::MakeObject(Object->GetObjectPropertyValue(Slot));
			return true;
		}

		return false;
	}
}

// -----------------------------------------------------------------------------
// UTarinoiFunctionCollection
// -----------------------------------------------------------------------------

UFunction* UTarinoiFunctionCollection::FindBindingFunction(FName Name) const
{
	UFunction* Function = GetClass()->FindFunctionByName(Name);
	if (!Function)
	{
		return nullptr;
	}

	// Only functions a binding class declares count, not the plumbing every UObject or
	// Blueprint carries.
	const UClass* Owner = Function->GetOwnerClass();
	if (Owner == UObject::StaticClass() || Owner == UTarinoiFunctionCollection::StaticClass()
		|| Function->GetName().StartsWith(TEXT("ExecuteUbergraph")))
	{
		return nullptr;
	}

	return Function;
}

bool UTarinoiFunctionCollection::HasFunction(FName Name) const
{
	return FindBindingFunction(Name) != nullptr;
}

bool UTarinoiFunctionCollection::TryInvoke(FName Name, const TArray<FTarinoiValue>& Args, FTarinoiValue& OutResult)
{
	OutResult = FTarinoiValue::None();

	UFunction* Function = FindBindingFunction(Name);
	if (!Function)
	{
		return false;
	}

	TArray<FProperty*> Inputs;
	FProperty* ReturnProperty = nullptr;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ReturnProperty = *It;
		}
		else if (!It->HasAnyPropertyFlags(CPF_OutParm) || It->HasAnyPropertyFlags(CPF_ReferenceParm))
		{
			Inputs.Add(*It);
		}
	}

	if (!ArityMatches(Name, Args, Inputs.Num()))
	{
		return true;
	}

	uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
	FMemory::Memzero(Params, Function->ParmsSize);
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Params);
	}

	bool bArgumentsOk = true;
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		if (!WriteProperty(Inputs[Index], Inputs[Index]->ContainerPtrToValuePtr<void>(Params), Args[Index]))
		{
			UE_LOG(LogTarinoi, Error, TEXT("Bindings: %s.%s has a parameter '%s' of a type Tarinoi cannot pass (%s)."),
				*GetClass()->GetName(), *Name.ToString(), *Inputs[Index]->GetName(), *Inputs[Index]->GetCPPType());
			bArgumentsOk = false;
			break;
		}
	}

	if (bArgumentsOk)
	{
		ProcessEvent(Function, Params);

		if (ReturnProperty && !ReadProperty(ReturnProperty, ReturnProperty->ContainerPtrToValuePtr<void>(Params), OutResult))
		{
			UE_LOG(LogTarinoi, Error, TEXT("Bindings: %s.%s returns a type Tarinoi cannot use (%s)."),
				*GetClass()->GetName(), *Name.ToString(), *ReturnProperty->GetCPPType());
		}
	}

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Params);
	}

	return true;
}

bool UTarinoiFunctionCollection::ArityMatches(FName Name, const TArray<FTarinoiValue>& Args, int32 Expected) const
{
	if (Args.Num() == Expected)
	{
		return true;
	}

	UE_LOG(LogTarinoi, Error, TEXT("Bindings: %s takes %d argument(s) but the authored call passed %d. Regenerate your bindings."),
		*Name.ToString(), Expected, Args.Num());
	return false;
}

void UTarinoiFunctionCollection::LogUnimplemented(const TCHAR* Function) const
{
	UE_LOG(LogTarinoi, Error, TEXT("%s.%s is not implemented. Override it in your bindings class."), *GetClass()->GetName(), Function);
}

// -----------------------------------------------------------------------------
// UTarinoiVariableCollection
// -----------------------------------------------------------------------------

FProperty* UTarinoiVariableCollection::FindVariableProperty(FName Name) const
{
	if (FProperty* Exact = GetClass()->FindPropertyByName(Name))
	{
		return Exact;
	}
	return GetClass()->FindPropertyByName(FName(*TarinoiNames::ToPascal(Name.ToString())));
}

FTarinoiValue UTarinoiVariableCollection::GetVariable_Implementation(FName Name)
{
	FTarinoiValue Value;
	const FProperty* Property = FindVariableProperty(Name);
	if (!Property || !ReadProperty(Property, Property->ContainerPtrToValuePtr<void>(this), Value))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Bindings: %s has no readable variable '%s'."), *GetClass()->GetName(), *Name.ToString());
		return FTarinoiValue::None();
	}
	return Value;
}

void UTarinoiVariableCollection::SetVariable_Implementation(FName Name, const FTarinoiValue& Value)
{
	const FProperty* Property = FindVariableProperty(Name);
	if (!Property || !WriteProperty(Property, Property->ContainerPtrToValuePtr<void>(this), Value))
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Bindings: %s has no writable variable '%s'."), *GetClass()->GetName(), *Name.ToString());
	}
}

// -----------------------------------------------------------------------------
// UTarinoiEntityCollection
// -----------------------------------------------------------------------------

UObject* UTarinoiEntityCollection::GetEntity_Implementation(FName Name)
{
	UE_LOG(LogTarinoi, Warning, TEXT("Bindings: %s does not override GetEntity, so Ent.*.%s has no object."),
		*GetClass()->GetName(), *Name.ToString());
	return nullptr;
}

// -----------------------------------------------------------------------------
// UTarinoiBindings
// -----------------------------------------------------------------------------

bool UTarinoiBindings::Validate(const FString& CollectionIdentifier, const UObject* Object, const TCHAR* Kind) const
{
	if (CollectionIdentifier.IsEmpty())
	{
		UE_LOG(LogTarinoi, Error, TEXT("Bindings: cannot bind a %s collection without an identifier."), Kind);
		return false;
	}

	if (!Object)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Bindings: cannot bind nothing as the '%s' %s collection."), *CollectionIdentifier, Kind);
		return false;
	}

	return true;
}

void UTarinoiBindings::Retain(UObject* Previous, UObject* Next)
{
	if (Previous)
	{
		Retained.RemoveSingle(Previous);
	}
	Retained.Add(Next);
}

void UTarinoiBindings::BindFunctions(const FString& CollectionIdentifier, UTarinoiFunctionCollection* InFunctions)
{
	if (Validate(CollectionIdentifier, InFunctions, TEXT("function")))
	{
		Retain(Functions.FindRef(CollectionIdentifier), InFunctions);
		Functions.Add(CollectionIdentifier, InFunctions);
	}
}

void UTarinoiBindings::BindVariables(const FString& CollectionIdentifier, UTarinoiVariableCollection* InVariables)
{
	if (Validate(CollectionIdentifier, InVariables, TEXT("variable")))
	{
		Retain(Variables.FindRef(CollectionIdentifier), InVariables);
		Variables.Add(CollectionIdentifier, InVariables);
	}
}

void UTarinoiBindings::BindEntities(const FString& CollectionIdentifier, UTarinoiEntityCollection* InEntities)
{
	if (Validate(CollectionIdentifier, InEntities, TEXT("entity")))
	{
		Retain(Entities.FindRef(CollectionIdentifier), InEntities);
		Entities.Add(CollectionIdentifier, InEntities);
	}
}

UTarinoiFunctionCollection* UTarinoiBindings::GetFunctions(const FString& CollectionIdentifier) const
{
	return Functions.FindRef(CollectionIdentifier);
}

UTarinoiVariableCollection* UTarinoiBindings::GetVariables(const FString& CollectionIdentifier) const
{
	return Variables.FindRef(CollectionIdentifier);
}

UTarinoiEntityCollection* UTarinoiBindings::GetEntities(const FString& CollectionIdentifier) const
{
	return Entities.FindRef(CollectionIdentifier);
}

TArray<FString> UTarinoiBindings::GetBoundFunctionCollections() const
{
	TArray<FString> Keys;
	Functions.GetKeys(Keys);
	return Keys;
}

TArray<FString> UTarinoiBindings::GetBoundVariableCollections() const
{
	TArray<FString> Keys;
	Variables.GetKeys(Keys);
	return Keys;
}

TArray<FString> UTarinoiBindings::GetBoundEntityCollections() const
{
	TArray<FString> Keys;
	Entities.GetKeys(Keys);
	return Keys;
}

void UTarinoiBindings::Clear()
{
	Functions.Reset();
	Variables.Reset();
	Entities.Reset();
	Retained.Reset();
}

TArray<FString> UTarinoiBindings::BindGeneratedDefaults(const FString& ClassPrefix)
{
	TArray<FString> Bound;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsNative() || !Class->IsChildOf(UTarinoiVariableCollection::StaticClass())
			|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		// Exactly the generated class for its collection, so nothing that merely resembles one
		// (a test fixture, a hand-written subclass) is picked up.
		const FString Collection = Class->GetDefaultObject<UTarinoiVariableCollection>()->GetCollectionIdentifier();
		if (Collection.IsEmpty() || GetVariables(Collection)
			|| !Class->GetName().Equals(ClassPrefix + TarinoiNames::ToPascal(Collection) + TEXT("Variables"), ESearchCase::CaseSensitive))
		{
			continue;
		}

		BindVariables(Collection, NewObject<UTarinoiVariableCollection>(this, Class));
		Bound.Add(FString::Printf(TEXT("Var.%s -> %s"), *Collection, *Class->GetName()));
	}

	// The core set's generated base is named after its collection, "tarinoi"; the scaffold is
	// whatever concrete class derives from it, wherever the game moved it.
	static const TCHAR* CoreCollection = TEXT("tarinoi");
	const UClass* CoreBase = FindFirstObject<UClass>(*(ClassPrefix + TEXT("TarinoiFunctions")), EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::ExactClass);
	if (CoreBase && !GetFunctions(CoreCollection))
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class != CoreBase && Class->IsChildOf(CoreBase) && Class->IsNative()
				&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				BindFunctions(CoreCollection, NewObject<UTarinoiFunctionCollection>(this, Class));
				Bound.Add(FString::Printf(TEXT("Fn.%s -> %s"), CoreCollection, *Class->GetName()));
				break;
			}
		}
	}

	return Bound;
}
