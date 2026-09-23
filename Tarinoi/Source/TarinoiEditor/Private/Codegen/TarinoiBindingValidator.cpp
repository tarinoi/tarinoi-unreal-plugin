// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Codegen/TarinoiBindingValidator.h"

#include "Bindings/TarinoiBindings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FTarinoiBindingIssue Breaking(const FString& Message) { return {true, Message}; }
	FTarinoiBindingIssue Addition(const FString& Message) { return {false, Message}; }

	UClass* FindGenerated(const FString& ClassName)
	{
		return FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::ExactClass);
	}

	FString TypeOf(const FProperty* Property)
	{
		return Property ? Property->GetCPPType() : FString(TEXT("void"));
	}

	void ValidateFunctions(const FTarinoiCodegenModel& Model, const FString& Prefix, TArray<FTarinoiBindingIssue>& Issues)
	{
		for (const FString& Collection : FTarinoiCodegenModel::SortedKeys(Model.Functions))
		{
			const FString ClassName = TarinoiCodeNames::CollectionClass(Collection, TEXT("Functions"), Prefix);
			const UClass* Class = FindGenerated(ClassName);
			if (!Class)
			{
				Issues.Add(Addition(FString::Printf(TEXT("'%s' has functions, but no generated U%s is compiled yet."), *Collection, *ClassName)));
				continue;
			}

			TSet<FString> Expected;
			for (const FTarinoiFunctionDecl& Fn : Model.Functions[Collection])
			{
				const FString Member = TarinoiCodeNames::Function(Fn.Name);
				Expected.Add(Member);

				const UFunction* Function = Class->FindFunctionByName(FName(*Member), EIncludeSuperFlag::ExcludeSuper);
				if (!Function)
				{
					Issues.Add(Addition(FString::Printf(TEXT("U%s::%s is declared in Tarinoi but not generated yet."), *ClassName, *Member)));
					continue;
				}

				int32 Parameters = 0;
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					Parameters += It->HasAnyPropertyFlags(CPF_ReturnParm) ? 0 : 1;
				}
				if (Parameters != Fn.Args.Num())
				{
					Issues.Add(Breaking(FString::Printf(TEXT("U%s::%s now takes %d argument(s), but the generated version takes %d. Code calling it will need updating."),
						*ClassName, *Member, Fn.Args.Num(), Parameters)));
				}

				const FString ExpectedReturn = TarinoiCodeTypes::ForReturn(Fn.Returns);
				const FString ActualReturn = TypeOf(Function->GetReturnProperty());
				if (ExpectedReturn != ActualReturn)
				{
					Issues.Add(Breaking(FString::Printf(TEXT("U%s::%s now returns %s, but the generated version returns %s."),
						*ClassName, *Member, *ExpectedReturn, *ActualReturn)));
				}
			}

			for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				if (!Expected.Contains(It->GetName()))
				{
					Issues.Add(Breaking(FString::Printf(TEXT("U%s::%s no longer exists in Tarinoi. Anything overriding it will stop compiling."),
						*ClassName, *It->GetName())));
				}
			}
		}
	}

	void ValidateVariables(const FTarinoiCodegenModel& Model, const FString& Prefix, TArray<FTarinoiBindingIssue>& Issues)
	{
		for (const FString& Collection : FTarinoiCodegenModel::SortedKeys(Model.Variables))
		{
			const FString ClassName = TarinoiCodeNames::CollectionClass(Collection, TEXT("Variables"), Prefix);
			const UClass* Class = FindGenerated(ClassName);
			if (!Class)
			{
				Issues.Add(Addition(FString::Printf(TEXT("'%s' has variables, but no generated U%s is compiled yet."), *Collection, *ClassName)));
				continue;
			}

			TSet<FString> Expected;
			for (const FTarinoiVariableDecl& Var : Model.Variables[Collection])
			{
				const FString Member = TarinoiCodeNames::Variable(Var.Name);
				Expected.Add(Member);

				const FProperty* Property = FindFProperty<FProperty>(Class, FName(*Member));
				if (!Property || Property->GetOwnerClass() != Class)
				{
					Issues.Add(Addition(FString::Printf(TEXT("U%s::%s is declared in Tarinoi but not generated yet."), *ClassName, *Member)));
					continue;
				}

				const FString ExpectedType = TarinoiCodeTypes::ForData(Var.DataType);
				if (TypeOf(Property) != ExpectedType)
				{
					Issues.Add(Breaking(FString::Printf(TEXT("U%s::%s is now %s, but the generated version is %s."),
						*ClassName, *Member, *ExpectedType, *TypeOf(Property))));
				}
			}

			for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				if (!Expected.Contains(It->GetName()))
				{
					Issues.Add(Breaking(FString::Printf(TEXT("U%s::%s no longer exists in Tarinoi."), *ClassName, *It->GetName())));
				}
			}
		}
	}

	/**
	 * The scaffold is the game's code, so nothing here is breaking: a function it does not override
	 * still reaches the generated base's not-implemented default.
	 */
	void ValidateCoreFunctions(const FTarinoiCodegenModel& Model, const FString& ScaffoldHeader, const FString& Prefix, TArray<FTarinoiBindingIssue>& Issues)
	{
		const TArray<FTarinoiFunctionDecl>* Decls = Model.Functions.Find(TarinoiCoreFunctions::Collection);
		const UClass* Base = FindGenerated(TarinoiCodeNames::CollectionClass(TarinoiCoreFunctions::Collection, TEXT("Functions"), Prefix));

		const UClass* Scaffold = nullptr;
		if (Base)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (*It != Base && It->IsChildOf(Base) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) && It->IsNative())
				{
					Scaffold = *It;
					break;
				}
			}
		}

		const FString Name = TarinoiCoreFunctions::ClassName(Prefix);
		if (!Scaffold)
		{
			if (Decls)
			{
				Issues.Add(Addition(FString::Printf(TEXT("'%s' is the core function set, but no U%s is compiled yet. Regenerate Bindings scaffolds the reference implementation."),
					TarinoiCoreFunctions::Collection, *Name)));
			}
			return;
		}

		if (!Decls)
		{
			Issues.Add(Addition(FString::Printf(TEXT("%s exists, but the project has no '%s' collection. Delete it, or recreate the core functions in Tarinoi."),
				*Scaffold->GetName(), TarinoiCoreFunctions::Collection)));
			return;
		}

#if WITH_METADATA
		const FString Version = Scaffold->GetMetaData(TarinoiCoreFunctions::VersionMetadata);
		if (Version != TarinoiCoreFunctions::Version)
		{
			Issues.Add(Addition(FString::Printf(TEXT("U%s was scaffolded from core functions '%s'; this plugin scaffolds %s. Delete it to re-scaffold, or merge the changes by hand."),
				*Scaffold->GetName(), *Version, TarinoiCoreFunctions::Version)));
		}
#endif

		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *ScaffoldHeader))
		{
			return;
		}

		for (const FTarinoiFunctionDecl& Fn : *Decls)
		{
			const FString Member = TarinoiCodeNames::Function(Fn.Name);
			if (!Source.Contains(Member + TEXT("_Implementation")))
			{
				Issues.Add(Addition(FString::Printf(TEXT("U%s does not implement %s, so Fn.%s.%s reaches the not-implemented default. Add it, or delete the file to re-scaffold."),
					*Scaffold->GetName(), *Member, TarinoiCoreFunctions::Collection, *Fn.Name)));
			}
		}
	}
}

namespace TarinoiBindingValidator
{
	TArray<FTarinoiBindingIssue> Validate(const FTarinoiCodegenModel& Model, const FString& ScaffoldHeader, const FString& ClassPrefix)
	{
		TArray<FTarinoiBindingIssue> Issues;
		ValidateFunctions(Model, ClassPrefix, Issues);
		ValidateVariables(Model, ClassPrefix, Issues);
		ValidateCoreFunctions(Model, ScaffoldHeader, ClassPrefix, Issues);
		return Issues;
	}
}
