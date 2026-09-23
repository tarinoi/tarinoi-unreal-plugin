// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Codegen/TarinoiCodegen.h"
#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

/**
 * The model the golden fixture is rendered from: the full core set, as the service declares it,
 * plus a small game collection of each kind. Its output lives in Fixtures/, is compiled into this
 * test module (so generated code is proven to compile), and is checked byte for byte against what
 * the emitter renders now.
 */
namespace TarinoiCodegenFixture
{
	inline FTarinoiCodegenOptions Options()
	{
		FTarinoiCodegenOptions Options;
		Options.ProjectId = TEXT("fixture");
		Options.ApiMacro = TEXT("TARINOITESTS_API");
		Options.ClassPrefix = TEXT("TarinoiFixture");
		return Options;
	}

	inline FTarinoiFunctionDecl Fn(const TCHAR* Name, std::initializer_list<const TCHAR*> Args, const TCHAR* Returns, const TCHAR* Effect)
	{
		FTarinoiFunctionDecl Decl;
		Decl.Name = Name;
		for (const TCHAR* Arg : Args)
		{
			Decl.Args.Add(Arg);
		}
		Decl.Returns = Returns;
		Decl.Effect = Effect;
		return Decl;
	}

	/** The thirteen core declarations, shaped as CORE_DECLARATIONS in tarinoi-app. */
	inline TArray<FTarinoiFunctionDecl> CoreDecls()
	{
		return {
			Fn(TEXT("ClearFlag"), {TEXT("flagRef")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("FlagIsSet"), {TEXT("flagRef")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("IncrementCounter"), {TEXT("counterRef"), TEXT("delta")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("NumberAtLeast"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("NumberAtMost"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("NumberEquals"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("NumberGreaterThan"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("NumberLessThan"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("SetCounter"), {TEXT("counterRef"), TEXT("value")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("SetFlag"), {TEXT("flagRef")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("SetText"), {TEXT("textRef"), TEXT("value")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("StringEquals"), {TEXT("a"), TEXT("b")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("ToggleFlag"), {TEXT("flagRef")}, TEXT("void"), TEXT("mutation")),
		};
	}

	inline FTarinoiCodegenModel Model()
	{
		FTarinoiCodegenModel Model;
		Model.Functions.Add(TEXT("tarinoi"), CoreDecls());
		Model.Functions.Add(TEXT("global"), {
			Fn(TEXT("CheckGate"), {TEXT("door")}, TEXT("boolean"), TEXT("pure")),
			Fn(TEXT("Roll"), {TEXT("sides"), TEXT("bonus")}, TEXT("number"), TEXT("mutation")),
			Fn(TEXT("PickPin"), {}, TEXT("string"), TEXT("")),
			Fn(TEXT("Rename"), {TEXT("who")}, TEXT("void"), TEXT("mutation")),
			Fn(TEXT("Describe"), {TEXT("thing")}, TEXT("object"), TEXT("")),
		});

		FTarinoiVariableDecl Met;
		Met.Name = TEXT("met_ferryman");
		Met.DataType = TEXT("boolean");
		Met.DefaultValue = MakeShared<FJsonValueBoolean>(true);
		FTarinoiVariableDecl Gold;
		Gold.Name = TEXT("gold");
		Gold.DataType = TEXT("number");
		Gold.DefaultValue = MakeShared<FJsonValueNumber>(12.5);
		FTarinoiVariableDecl PlayerName;
		PlayerName.Name = TEXT("player_name");
		PlayerName.DataType = TEXT("string");
		PlayerName.DefaultValue = MakeShared<FJsonValueString>(TEXT("Adä \"the\" Bold"));
		FTarinoiVariableDecl Mood;
		Mood.Name = TEXT("mood");
		Mood.DataType = TEXT("string");
		FTarinoiVariableDecl Anything;
		Anything.Name = TEXT("anything");
		Anything.DataType = TEXT("object");
		Model.Variables.Add(TEXT("state"), {Met, Gold, PlayerName, Mood, Anything});

		FTarinoiListDecl Thresholds;
		Thresholds.Identifier = TEXT("thresholds");
		Thresholds.OptionKeys = {TEXT("hard"), TEXT("easy"), TEXT("heroic")};
		Model.Lists.Add(TEXT("global"), {Thresholds});

		Model.Entities.Add(TEXT("cast"), {TEXT("narrator"), TEXT("ferryman")});
		return Model;
	}

	/** Where the committed fixture files live. */
	inline FString Directory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Tarinoi"));
		return FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/TarinoiTests/Private/Fixtures"));
	}

	/** The full rendered set, including the scaffold, by file name. */
	inline TMap<FString, FString> RenderAll()
	{
		TMap<FString, FString> Files = TarinoiCodegen::Render(Model(), Options()).Files;
		FString Header, Source;
		TArray<FString> Unknown;
		TarinoiCoreFunctions::Render(CoreDecls(), Options(), TarinoiCodegen::FunctionsHeader, Header, Source, Unknown);
		Files.Add(TarinoiCoreFunctions::ClassName(Options().ClassPrefix) + TEXT(".h"), Header);
		Files.Add(TarinoiCoreFunctions::ClassName(Options().ClassPrefix) + TEXT(".cpp"), Source);
		return Files;
	}
}
