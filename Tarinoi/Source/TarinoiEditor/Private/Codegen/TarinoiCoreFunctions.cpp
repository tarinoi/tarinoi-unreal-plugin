// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Codegen/TarinoiCodeWriter.h"
#include "Codegen/TarinoiCodegen.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tarinoi.h"

namespace
{
	struct FBody
	{
		TArray<FString> Args;
		FString Returns; // "void" or "bool"
		FString Doc;
		FString Expression;
	};

	/**
	 * The reference bodies, by function name. A declaration that does not match its entry by name
	 * and arity is not a core function this plugin knows, and gets a not-implemented stub.
	 */
	const TMap<FString, FBody>& Bodies()
	{
		static const TMap<FString, FBody> Map = {
			{TEXT("SetFlag"), {{TEXT("flagRef")}, TEXT("void"), TEXT("Set a flag."), TEXT("Write(FlagRef, FTarinoiValue::MakeBool(true))")}},
			{TEXT("ClearFlag"), {{TEXT("flagRef")}, TEXT("void"), TEXT("Clear a flag."), TEXT("Write(FlagRef, FTarinoiValue::MakeBool(false))")}},
			{TEXT("ToggleFlag"), {{TEXT("flagRef")}, TEXT("void"), TEXT("Toggle a flag: clear if set, set if unset."),
				TEXT("Write(FlagRef, FTarinoiValue::MakeBool(!Flag(FlagRef)))")}},
			{TEXT("SetCounter"), {{TEXT("counterRef"), TEXT("value")}, TEXT("void"), TEXT("Set a counter to a value, which must be a number."),
				TEXT("Write(CounterRef, FTarinoiValue::MakeNumber(Number(Value)))")}},
			{TEXT("IncrementCounter"), {{TEXT("counterRef"), TEXT("delta")}, TEXT("void"),
				TEXT("Increment a counter by delta, which must be a number. Use negative numbers to decrement. An unset counter is treated as a 0."),
				TEXT("Write(CounterRef, FTarinoiValue::MakeNumber(Number(CounterRef) + Number(Delta)))")}},
			{TEXT("SetText"), {{TEXT("textRef"), TEXT("value")}, TEXT("void"), TEXT("Set a text variable to a value, which must be a string."),
				TEXT("Write(TextRef, FTarinoiValue::MakeString(Text(Value)))")}},
			{TEXT("FlagIsSet"), {{TEXT("flagRef")}, TEXT("bool"),
				TEXT("Returns true if the flag is set. An unset flag is treated as clear. Use the NOT operator in conditions to negate."),
				TEXT("Flag(FlagRef)")}},
			{TEXT("NumberEquals"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a and b equal each other. Both must be numbers. An unset counter is treated as a 0. Use the NOT operator in conditions to negate."),
				TEXT("Number(A) == Number(B)")}},
			{TEXT("NumberAtLeast"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a >= b. Both must be numbers. An unset counter is treated as a 0."), TEXT("Number(A) >= Number(B)")}},
			{TEXT("NumberGreaterThan"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a > b. Both must be numbers. An unset counter is treated as a 0."), TEXT("Number(A) > Number(B)")}},
			{TEXT("NumberAtMost"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a <= b. Both must be numbers. An unset counter is treated as a 0."), TEXT("Number(A) <= Number(B)")}},
			{TEXT("NumberLessThan"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a < b. Both must be numbers. An unset counter is treated as a 0."), TEXT("Number(A) < Number(B)")}},
			{TEXT("StringEquals"), {{TEXT("a"), TEXT("b")}, TEXT("bool"),
				TEXT("Returns true if a and b equal each other. Both must be strings. An unset text variable is treated as empty. Use the NOT operator in conditions to negate."),
				TEXT("Text(A).Equals(Text(B), ESearchCase::CaseSensitive)")}},
		};
		return Map;
	}

	TArray<FTarinoiFunctionDecl> Ordered(const TArray<FTarinoiFunctionDecl>& Decls)
	{
		const TArray<FString>& Order = TarinoiCoreFunctions::Order();
		TArray<FTarinoiFunctionDecl> Sorted = Decls;
		Sorted.StableSort([&Order](const FTarinoiFunctionDecl& A, const FTarinoiFunctionDecl& B)
		{
			const int32 RankA = Order.Contains(A.Name) ? Order.IndexOfByKey(A.Name) : Order.Num();
			const int32 RankB = Order.Contains(B.Name) ? Order.IndexOfByKey(B.Name) : Order.Num();
			return RankA != RankB ? RankA < RankB : A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
		});
		return Sorted;
	}

	bool IsKnown(const FTarinoiFunctionDecl& Fn);

	/**
	 * Parameters named as the reference body names them, so the body compiles whatever the synced
	 * declaration calls its arguments; a stub uses the declared names.
	 */
	FString Params(const FTarinoiFunctionDecl& Fn)
	{
		TArray<FString> Out;
		for (const FString& Arg : IsKnown(Fn) ? Bodies()[Fn.Name].Args : Fn.Args)
		{
			Out.Add(TEXT("const FTarinoiValue& ") + TarinoiCodeNames::Parameter(Arg));
		}
		return FString::Join(Out, TEXT(", "));
	}

	bool IsKnown(const FTarinoiFunctionDecl& Fn)
	{
		const FBody* Body = Bodies().Find(Fn.Name);
		return Body && Body->Args.Num() == Fn.Args.Num();
	}
}

namespace TarinoiCoreFunctions
{
	const TCHAR* Version = TEXT("0.0.1");
	const TCHAR* Collection = TEXT("tarinoi");
	FString ClassName(const FString& Prefix)
	{
		return Prefix + TEXT("CoreFunctions");
	}
	const TCHAR* VersionMetadata = TEXT("TarinoiCoreFunctionsVersion");

	const TArray<FString>& Order()
	{
		static const TArray<FString> Names = {
			TEXT("SetFlag"), TEXT("ClearFlag"), TEXT("ToggleFlag"), TEXT("SetCounter"), TEXT("IncrementCounter"), TEXT("SetText"),
			TEXT("FlagIsSet"), TEXT("NumberEquals"), TEXT("NumberAtLeast"), TEXT("NumberGreaterThan"), TEXT("NumberAtMost"),
			TEXT("NumberLessThan"), TEXT("StringEquals"),
		};
		return Names;
	}

	void Render(const TArray<FTarinoiFunctionDecl>& Decls, const FTarinoiCodegenOptions& Options,
		const FString& GeneratedInclude, FString& OutHeader, FString& OutSource, TArray<FString>& OutUnknown)
	{
		OutUnknown.Reset();
		const TArray<FTarinoiFunctionDecl> Functions = Ordered(Decls);
		const FString BaseClass = TEXT("U") + TarinoiCodeNames::CollectionClass(Collection, TEXT("Functions"), Options.ClassPrefix);
		const FString Stem = ClassName(Options.ClassPrefix);
		const FString Class = TEXT("U") + Stem;
		const FString& ProjectId = Options.ProjectId;
		const FString& ApiMacro = Options.ApiMacro;

		auto Preamble = [&ProjectId](FTarinoiCodeWriter& W)
		{
			W.Line(FString::Printf(TEXT("// Tarinoi core functions %s: reference implementation."), Version));
			W.Line(FString::Printf(TEXT("// Scaffolded by Tarinoi from the synced content of project '%s'."), *TarinoiCodeNames::Comment(ProjectId)));
			W.Line(TEXT("//"));
			W.Line(TEXT("// This file is yours: edit it freely, regenerating never overwrites it. Delete it (and"));
			W.Line(TEXT("// its header or source) to get a fresh copy the next time you regenerate bindings."));
			W.Line(TEXT("//"));
			W.Line(FString::Printf(TEXT("// These are the Fn.%s.* functions in-app playback evaluates for real. Keep the same"), Collection));
			W.Line(TEXT("// semantics, in particular the defaults for unset variables, so what an author verified"));
			W.Line(TEXT("// in playback holds in the game."));
			W.Line();
		};

		// Header
		{
			FTarinoiCodeWriter W;
			Preamble(W);
			W.Line(TEXT("#pragma once"));
			W.Line();
			W.Line(TEXT("#include \"CoreMinimal.h\""));
			W.Line(FString::Printf(TEXT("#include \"%s\""), *GeneratedInclude));
			W.Line();
			W.Line(FString::Printf(TEXT("#include \"%s.generated.h\""), *Stem));
			W.Line();
			W.Doc({FString::Printf(TEXT("Tarinoi's core functions, callable as Fn.%s.*"), Collection), FString(),
				FString::Printf(TEXT("Bind an instance under \"%s\". The quickstart binds this class on its own."), Collection)});
			W.Line(FString::Printf(TEXT("UCLASS(Blueprintable, meta = (%s = \"%s\"))"), VersionMetadata, Version));
			W.Line(FString::Printf(TEXT("class %s %s : public %s"), *ApiMacro, *Class, *BaseClass));
			W.Open();
			W.Line(TEXT("GENERATED_BODY()"));
			W.Line();
			W.Outdent();
			W.Line(TEXT("public:"));
			W.Indent();
			for (const FTarinoiFunctionDecl& Fn : Functions)
			{
				const FBody* Body = Bodies().Find(Fn.Name);
				const bool bKnown = IsKnown(Fn);
				if (!bKnown)
				{
					OutUnknown.Add(Fn.Name);
				}

				W.Doc({bKnown ? Body->Doc : FString::Printf(TEXT("%s is not a core function this plugin knows; implement it."), *Fn.Name)});
				W.Line(FString::Printf(TEXT("virtual %s %s_Implementation(%s) override;"), *TarinoiCodeTypes::ForReturn(Fn.Returns),
					*TarinoiCodeNames::Function(Fn.Name), *Params(Fn)));
				W.Line();
			}

			W.Outdent();
			W.Line(TEXT("protected:"));
			W.Indent();
			W.Line(TEXT("// The dispatcher passes a Var.* argument as a variable reference, so the function can"));
			W.Line(TEXT("// write through it, and anything else as a plain value. Unset variables read as false,"));
			W.Line(TEXT("// 0 and \"\", as in-app playback has them. A value of the wrong type is an authoring"));
			W.Line(TEXT("// error: it is logged, and the default is used instead."));
			W.Line();
			W.Doc({TEXT("Writes through a Var.* reference.")});
			W.Line(TEXT("static void Write(const FTarinoiValue& Reference, const FTarinoiValue& Value);"));
			W.Line();
			W.Doc({TEXT("Reads a boolean: a flag, or a literal.")});
			W.Line(TEXT("static bool Flag(const FTarinoiValue& Value);"));
			W.Line();
			W.Doc({TEXT("Reads a number: a counter, a list item, or a literal.")});
			W.Line(TEXT("static double Number(const FTarinoiValue& Value);"));
			W.Line();
			W.Doc({TEXT("Reads a string: a text variable, a list item, or a literal.")});
			W.Line(TEXT("static FString Text(const FTarinoiValue& Value);"));
			W.Close(TEXT(";"));
			OutHeader = W.ToString();
		}

		// Source
		{
			FTarinoiCodeWriter W;
			Preamble(W);
			W.Line(FString::Printf(TEXT("#include \"%s.h\""), *Stem));
			W.Line();
			W.Line(TEXT("#include \"Tarinoi.h\""));

			for (const FTarinoiFunctionDecl& Fn : Functions)
			{
				const FString ReturnType = TarinoiCodeTypes::ForReturn(Fn.Returns);
				W.Line();
				W.Line(FString::Printf(TEXT("%s %s::%s_Implementation(%s)"), *ReturnType, *Class, *TarinoiCodeNames::Function(Fn.Name), *Params(Fn)));
				W.Open();
				if (IsKnown(Fn))
				{
					const FBody& Body = Bodies()[Fn.Name];
					W.Line(Body.Returns == TEXT("void") ? Body.Expression + TEXT(";") : TEXT("return ") + Body.Expression + TEXT(";"));
				}
				else
				{
					W.Line(FString::Printf(TEXT("LogUnimplemented(TEXT(\"%s\"));"), *TarinoiCodeNames::Function(Fn.Name)));
					const FString Default = TarinoiCodeTypes::DefaultReturn(Fn.Returns);
					if (!Default.IsEmpty())
					{
						W.Line(FString::Printf(TEXT("return %s;"), *Default));
					}
				}
				W.Close();
			}

			W.Line();
			W.Line(FString::Printf(TEXT("void %s::Write(const FTarinoiValue& Reference, const FTarinoiValue& Value)"), *Class));
			W.Open();
			W.Line(TEXT("if (!Reference.IsVariable())"));
			W.Open();
			W.Line(TEXT("UE_LOG(LogTarinoi, Error, TEXT(\"Core functions: expected a Var.* reference, got %s.\"), *Reference.Describe());"));
			W.Line(TEXT("return;"));
			W.Close();
			W.Line(TEXT("Reference.Write(Value);"));
			W.Close();

			W.Line();
			W.Line(FString::Printf(TEXT("bool %s::Flag(const FTarinoiValue& Value)"), *Class));
			W.Open();
			W.Line(TEXT("const FTarinoiValue Resolved = Value.Resolve();"));
			W.Line(TEXT("switch (Resolved.Type)"));
			W.Open();
			W.Line(TEXT("case ETarinoiValueType::None: return false;"));
			W.Line(TEXT("case ETarinoiValueType::Bool: return Resolved.BoolValue;"));
			W.Line(TEXT("default: break;"));
			W.Close();
			W.Line(TEXT("UE_LOG(LogTarinoi, Error, TEXT(\"Core functions: expected a boolean, got %s.\"), *Value.Describe());"));
			W.Line(TEXT("return false;"));
			W.Close();

			W.Line();
			W.Line(FString::Printf(TEXT("double %s::Number(const FTarinoiValue& Value)"), *Class));
			W.Open();
			W.Line(TEXT("const FTarinoiValue Resolved = Value.Resolve();"));
			W.Line(TEXT("switch (Resolved.Type)"));
			W.Open();
			W.Line(TEXT("case ETarinoiValueType::None: return 0.0;"));
			W.Line(TEXT("case ETarinoiValueType::Number: return Resolved.NumberValue;"));
			W.Line(TEXT("default: break;"));
			W.Close();
			W.Line(TEXT("UE_LOG(LogTarinoi, Error, TEXT(\"Core functions: expected a number, got %s.\"), *Value.Describe());"));
			W.Line(TEXT("return 0.0;"));
			W.Close();

			W.Line();
			W.Line(FString::Printf(TEXT("FString %s::Text(const FTarinoiValue& Value)"), *Class));
			W.Open();
			W.Line(TEXT("const FTarinoiValue Resolved = Value.Resolve();"));
			W.Line(TEXT("switch (Resolved.Type)"));
			W.Open();
			W.Line(TEXT("case ETarinoiValueType::None: return FString();"));
			W.Line(TEXT("case ETarinoiValueType::String: return Resolved.StringValue;"));
			W.Line(TEXT("default: break;"));
			W.Close();
			W.Line(TEXT("UE_LOG(LogTarinoi, Error, TEXT(\"Core functions: expected a string, got %s.\"), *Value.Describe());"));
			W.Line(TEXT("return FString();"));
			W.Close();
			OutSource = W.ToString();
		}
	}

	bool Scaffold(const FTarinoiCodegenModel& Model, const FString& ImplDir, const FString& OutputDir, const FTarinoiCodegenOptions& Options)
	{
		const TArray<FTarinoiFunctionDecl>* Decls = Model.Functions.Find(Collection);
		if (!Decls)
		{
			return false;
		}

		const FString HeaderPath = FPaths::Combine(ImplDir, ClassName(Options.ClassPrefix) + TEXT(".h"));
		const FString SourcePath = FPaths::Combine(ImplDir, ClassName(Options.ClassPrefix) + TEXT(".cpp"));
		if (FPaths::FileExists(HeaderPath) || FPaths::FileExists(SourcePath))
		{
			return false;
		}

		FString Include = FPaths::Combine(OutputDir, TarinoiCodegen::FunctionsHeader);
		FPaths::MakePathRelativeTo(Include, *(FPaths::ConvertRelativePathToFull(ImplDir) / TEXT("")));

		FString Header, Source;
		TArray<FString> Unknown;
		Render(*Decls, Options, Include, Header, Source, Unknown);

		for (const FString& Name : Unknown)
		{
			UE_LOG(LogTarinoi, Warning, TEXT("Codegen: Fn.%s.%s is not a core function this plugin knows; it is stubbed in %s. Implement it, or update the plugin."),
				Collection, *Name, *SourcePath);
		}

		IFileManager::Get().MakeDirectory(*ImplDir, true);
		if (!FFileHelper::SaveStringToFile(Header, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !FFileHelper::SaveStringToFile(Source, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogTarinoi, Error, TEXT("Codegen: could not write the core functions scaffold to %s"), *ImplDir);
			return false;
		}

		UE_LOG(LogTarinoi, Log, TEXT("Codegen: scaffolded core functions %s to %s. The file is yours to edit."), Version, *HeaderPath);
		return true;
	}
}
