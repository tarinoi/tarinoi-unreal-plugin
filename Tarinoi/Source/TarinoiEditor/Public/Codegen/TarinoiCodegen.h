// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "TarinoiTypes.h"

class FTarinoiDatabase;

/** One authored function, callable as Fn.collection.Name(...). */
struct FTarinoiFunctionDecl
{
	FString Name;
	TArray<FString> Args;
	FString Returns;
	FString Effect;
};

/** One authored variable, readable as Var.collection.name. */
struct FTarinoiVariableDecl
{
	FString Name;
	FString DataType;
	TSharedPtr<FJsonValue> DefaultValue;
};

/** One authored option list, readable as Ls.collection.list.key. */
struct FTarinoiListDecl
{
	FString Identifier;
	TArray<FString> OptionKeys;
};

/** Everything codegen reads from the database, grouped by collection identifier. */
struct TARINOIEDITOR_API FTarinoiCodegenModel
{
	TTarinoiMap<TArray<FTarinoiFunctionDecl>> Functions;
	TTarinoiMap<TArray<FTarinoiVariableDecl>> Variables;
	TTarinoiMap<TArray<FTarinoiListDecl>> Lists;
	TTarinoiMap<TArray<FString>> Entities;

	bool IsEmpty() const { return Functions.Num() == 0 && Variables.Num() == 0 && Lists.Num() == 0 && Entities.Num() == 0; }

	/** Collection identifiers of a map, in the ordinal order everything is emitted in. */
	template <typename T>
	static TArray<FString> SortedKeys(const TTarinoiMap<T>& Map)
	{
		TArray<FString> Keys;
		Map.GetKeys(Keys);
		Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
		return Keys;
	}
};

/** Turns authored names and types into C++. Deterministic, because the output is committed. */
namespace TarinoiCodeNames
{
	/**
	 * A member name for an authored function or variable: PascalCase, with a prefix when it
	 * would collide with something every binding class already has (GetName, Rename, ...).
	 */
	TARINOIEDITOR_API FString Function(const FString& Authored);
	TARINOIEDITOR_API FString Variable(const FString& Authored);

	/** A parameter name: PascalCase, as UE code writes them. */
	TARINOIEDITOR_API FString Parameter(const FString& Authored);

	/**
	 * The generated class for a collection, without the U prefix: TarinoiGlobalFunctions. The
	 * prefix only changes for the test fixture, which must not clash with a game's own classes.
	 */
	TARINOIEDITOR_API FString CollectionClass(const FString& Collection, const TCHAR* Kind, const FString& Prefix = TEXT("Tarinoi"));

	/** A string as a TEXT("...") literal, escaped, with non-ASCII as \u escapes. */
	TARINOIEDITOR_API FString Literal(const FString& Value);

	/** Text made safe inside a comment. */
	TARINOIEDITOR_API FString Comment(const FString& Value);
}

/** Maps Tarinoi's authored types onto C++ ones. */
namespace TarinoiCodeTypes
{
	/** bool, double, FString, or FTarinoiValue for anything else. */
	TARINOIEDITOR_API FString ForData(const FString& DataType);

	/** As ForData, plus void for an empty or "void" return. */
	TARINOIEDITOR_API FString ForReturn(const FString& Returns);

	/** What an unimplemented function returns: false, 0.0, FString(), FTarinoiValue(). Empty for void. */
	TARINOIEDITOR_API FString DefaultReturn(const FString& Returns);

	/** A variable's initialiser from its authored default, falling back to the type's zero. */
	TARINOIEDITOR_API FString DefaultValue(const FString& DataType, const TSharedPtr<FJsonValue>& Default);

	/** How a returned C++ value becomes an FTarinoiValue. */
	TARINOIEDITOR_API FString WrapResult(const FString& Returns, const FString& Expression);
}

/** What a codegen run renders for. */
struct TARINOIEDITOR_API FTarinoiCodegenOptions
{
	/** Named in the file headers. */
	FString ProjectId;

	/** The export macro of the module the code goes into, e.g. MYGAME_API. */
	FString ApiMacro;

	/** Prefix of every generated class name. Only the test fixture changes it. */
	FString ClassPrefix = TEXT("Tarinoi");
};

/** The files codegen writes. */
struct TARINOIEDITOR_API FTarinoiGeneratedFiles
{
	TMap<FString, FString> Files; // file name -> contents
};

namespace TarinoiCodegen
{
	/** Name of the header generated binding classes are declared in. */
	TARINOIEDITOR_API extern const TCHAR* FunctionsHeader;
	TARINOIEDITOR_API extern const TCHAR* VariablesHeader;
	TARINOIEDITOR_API extern const TCHAR* ListsHeader;
	TARINOIEDITOR_API extern const TCHAR* EntitiesHeader;

	/**
	 * Loads every declaration codegen cares about. Each query joins the collection's own
	 * manifest to recover its machine identifier, which is what expressions use and what bindings
	 * register under; the display label is never used.
	 */
	TARINOIEDITOR_API FTarinoiCodegenModel Load(FTarinoiDatabase& Database);

	/** Renders the generated files. */
	TARINOIEDITOR_API FTarinoiGeneratedFiles Render(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options);

	/** Writes the generated files into OutputDir, removing any stale generated file. */
	TARINOIEDITOR_API bool Write(const FTarinoiGeneratedFiles& Files, const FString& OutputDir);
}

/**
 * Renders the reference implementation of Tarinoi's core functions (the built-in Fn.tarinoi.*
 * set, which in-app playback evaluates for real) as a C++ class the game owns. Codegen writes it
 * once into the implementation folder and never overwrites it: the game edits it freely (a
 * different variable store, save-game hooks, ...). Deleting it gets a fresh copy.
 */
namespace TarinoiCoreFunctions
{
	/** The version of the core set this plugin scaffolds; matches the public core functions document. */
	TARINOIEDITOR_API extern const TCHAR* Version;

	/** The collection identifier: Fn.tarinoi.* */
	TARINOIEDITOR_API extern const TCHAR* Collection;

	/** The scaffolded class for a class prefix, without the U: TarinoiCoreFunctions. Also the file name stem. */
	TARINOIEDITOR_API FString ClassName(const FString& Prefix = TEXT("Tarinoi"));

	/** The class metadata key the scaffold carries its version in. */
	TARINOIEDITOR_API extern const TCHAR* VersionMetadata;

	/** The author-facing order of the set. Functions outside it follow, by name. */
	TARINOIEDITOR_API const TArray<FString>& Order();

	/**
	 * Renders the scaffold's header and source. OutUnknown receives declarations that got a
	 * not-implemented stub: not in the set, or with a different arity. GeneratedInclude is the
	 * include path of the generated functions header, relative to the scaffold.
	 */
	TARINOIEDITOR_API void Render(const TArray<FTarinoiFunctionDecl>& Decls, const FTarinoiCodegenOptions& Options,
		const FString& GeneratedInclude, FString& OutHeader, FString& OutSource, TArray<FString>& OutUnknown);

	/**
	 * Writes the scaffold into ImplDir if the model has the collection and no scaffold exists.
	 * Returns whether it wrote.
	 */
	TARINOIEDITOR_API bool Scaffold(const FTarinoiCodegenModel& Model, const FString& ImplDir, const FString& OutputDir,
		const FTarinoiCodegenOptions& Options);
}
