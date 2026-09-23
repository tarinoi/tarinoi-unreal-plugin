// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Codegen/TarinoiCodegen.h"

#include "Codegen/TarinoiCodeWriter.h"
#include "Data/TarinoiDatabase.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"
#include "TarinoiNames.h"

// -----------------------------------------------------------------------------
// Names
// -----------------------------------------------------------------------------

namespace
{
	/**
	 * Members every binding class already has. An authored function or variable with one of these
	 * names would hide or clash with it, so it gets a prefix instead.
	 */
	const TSet<FString>& ReservedMembers()
	{
		static const TSet<FString> Names = {
			TEXT("GetName"), TEXT("GetFName"), TEXT("GetClass"), TEXT("GetOuter"), TEXT("GetWorld"), TEXT("GetPathName"),
			TEXT("GetFullName"), TEXT("GetArchetype"), TEXT("GetTypedOuter"), TEXT("Rename"), TEXT("Modify"), TEXT("IsValid"),
			TEXT("IsA"), TEXT("IsTemplate"), TEXT("ProcessEvent"), TEXT("Serialize"), TEXT("PostLoad"), TEXT("PostInitProperties"),
			TEXT("BeginDestroy"), TEXT("FinishDestroy"), TEXT("ConditionalBeginDestroy"), TEXT("MarkAsGarbage"),
			TEXT("AddReferencedObjects"), TEXT("CallFunctionByNameWithArguments"), TEXT("StaticClass"), TEXT("FindFunction"),
			TEXT("HasFunction"), TEXT("TryInvoke"), TEXT("ArityMatches"), TEXT("LogUnimplemented"), TEXT("GetVariable"),
			TEXT("SetVariable"), TEXT("GetEntity"), TEXT("GetCollectionIdentifier"), TEXT("Collection"),
		};
		return Names;
	}

	bool IsNumericType(const FString& DataType)
	{
		return DataType.Equals(TEXT("number"), ESearchCase::IgnoreCase);
	}
}

namespace TarinoiCodeNames
{
	FString Function(const FString& Authored)
	{
		const FString Name = TarinoiNames::ToPascal(Authored);
		return ReservedMembers().Contains(Name) ? TEXT("Fn") + Name : Name;
	}

	FString Variable(const FString& Authored)
	{
		const FString Name = TarinoiNames::ToPascal(Authored);
		return ReservedMembers().Contains(Name) ? TEXT("Var") + Name : Name;
	}

	FString Parameter(const FString& Authored)
	{
		return TarinoiNames::ToPascal(Authored);
	}

	FString CollectionClass(const FString& Collection, const TCHAR* Kind, const FString& Prefix)
	{
		return Prefix + TarinoiNames::ToPascal(Collection) + Kind;
	}

	FString Literal(const FString& Value)
	{
		FString Out = TEXT("TEXT(\"");
		for (const TCHAR C : Value)
		{
			switch (C)
			{
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (C < 0x20 || C > 0x7E)
				{
					// Escaped so the generated file is plain ASCII whatever the compiler's source charset.
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
				}
				else
				{
					Out.AppendChar(C);
				}
			}
		}
		return Out + TEXT("\")");
	}

	FString Comment(const FString& Value)
	{
		return Value.Replace(TEXT("*/"), TEXT("* /")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
	}
}

namespace TarinoiCodeTypes
{
	FString ForData(const FString& DataType)
	{
		const FString Type = DataType.ToLower();
		if (Type == TEXT("boolean")) return TEXT("bool");
		if (Type == TEXT("number")) return TEXT("double");
		if (Type == TEXT("string")) return TEXT("FString");
		return TEXT("FTarinoiValue");
	}

	FString ForReturn(const FString& Returns)
	{
		return Returns.IsEmpty() || Returns.Equals(TEXT("void"), ESearchCase::IgnoreCase) ? FString(TEXT("void")) : ForData(Returns);
	}

	FString DefaultReturn(const FString& Returns)
	{
		const FString Type = ForReturn(Returns);
		if (Type == TEXT("void")) return FString();
		if (Type == TEXT("bool")) return TEXT("false");
		if (Type == TEXT("double")) return TEXT("0.0");
		if (Type == TEXT("FString")) return TEXT("FString()");
		return TEXT("FTarinoiValue()");
	}

	FString DefaultValue(const FString& DataType, const TSharedPtr<FJsonValue>& Default)
	{
		const FString Type = ForData(DataType);
		if (Type == TEXT("bool"))
		{
			bool bValue = false;
			if (Default.IsValid())
			{
				if (Default->Type == EJson::Boolean) bValue = Default->AsBool();
				else if (Default->Type == EJson::Number) bValue = Default->AsNumber() != 0.0;
				else if (Default->Type == EJson::String) bValue = Default->AsString().TrimStartAndEnd().ToLower() == TEXT("true") || Default->AsString().TrimStartAndEnd() == TEXT("1");
			}
			return bValue ? TEXT("true") : TEXT("false");
		}

		if (Type == TEXT("double"))
		{
			double Value = 0.0;
			if (Default.IsValid())
			{
				if (Default->Type == EJson::Number) Value = Default->AsNumber();
				else if (Default->Type == EJson::String && Default->AsString().IsNumeric()) Value = FCString::Atod(*Default->AsString());
				else if (Default->Type == EJson::Boolean) Value = Default->AsBool() ? 1.0 : 0.0;
			}
			FString Text = TarinoiJson::NumberToString(Value);
			if (!Text.Contains(TEXT(".")) && !Text.Contains(TEXT("e")))
			{
				Text += TEXT(".0");
			}
			return Text;
		}

		if (Type == TEXT("FString"))
		{
			const FString Value = Default.IsValid() ? TarinoiJson::Str(Default) : FString();
			return Value.IsEmpty() ? FString() : TarinoiCodeNames::Literal(Value);
		}

		return FString();
	}

	FString WrapResult(const FString& Returns, const FString& Expression)
	{
		const FString Type = ForReturn(Returns);
		if (Type == TEXT("bool")) return FString::Printf(TEXT("FTarinoiValue::MakeBool(%s)"), *Expression);
		if (Type == TEXT("double")) return FString::Printf(TEXT("FTarinoiValue::MakeNumber(%s)"), *Expression);
		if (Type == TEXT("FString")) return FString::Printf(TEXT("FTarinoiValue::MakeString(%s)"), *Expression);
		return Expression;
	}
}

// -----------------------------------------------------------------------------
// Loading
// -----------------------------------------------------------------------------

namespace
{
	struct FDeclRow
	{
		FString Identifier;
		FString Collection;
		TSharedPtr<FJsonObject> Payload;
	};

	TArray<FDeclRow> QueryDecls(FTarinoiDatabase& Database, const TCHAR* DocumentType, const TCHAR* ExtraFilter = TEXT(""))
	{
		const FString Sql = FString::Printf(TEXT(
			"SELECT d.identifier, d.payload, cm.identifier AS col_name"
			" FROM documents d"
			" JOIN documents cm ON cm.document_id = d.collection_id AND cm.document_type = 'collection-manifest'"
			" WHERE d.document_type = '%s' %s AND %s"
			" ORDER BY col_name, d.identifier"),
			DocumentType, ExtraFilter, *Database.ActiveFilter());

		TArray<FDeclRow> Rows;
		Database.Query(*Sql, {}, [&Rows](const FTarinoiSqlRow& Row)
		{
			const FString Identifier = Row.GetString(TEXT("identifier"));
			const FString Collection = Row.GetString(TEXT("col_name"));
			if (Identifier.IsEmpty() || Collection.IsEmpty())
			{
				return;
			}

			const TSharedPtr<FJsonObject> Payload = TarinoiJson::Parse(Row.GetString(TEXT("payload")));
			if (!Payload.IsValid())
			{
				UE_LOG(LogTarinoi, Warning, TEXT("Codegen: '%s' has an unreadable payload; skipping it."), *Identifier);
				return;
			}

			Rows.Add({Identifier, Collection, Payload});
		});

		// SQLite orders by byte value, which matches ordinal order for the ASCII identifiers
		// authors use; sorting again here makes that independent of collation.
		Rows.StableSort([](const FDeclRow& A, const FDeclRow& B)
		{
			const int32 ByCollection = A.Collection.Compare(B.Collection, ESearchCase::CaseSensitive);
			return ByCollection != 0 ? ByCollection < 0 : A.Identifier.Compare(B.Identifier, ESearchCase::CaseSensitive) < 0;
		});
		return Rows;
	}
}

namespace TarinoiCodegen
{
	FString FileName(const FString& Prefix, const TCHAR* Kind, const TCHAR* Extension)
	{
		return FString::Printf(TEXT("%sGenerated%s.%s"), *Prefix, Kind, Extension);
	}

	FTarinoiCodegenModel Load(FTarinoiDatabase& Database)
	{
		FTarinoiCodegenModel Model;
		if (!Database.IsOpen())
		{
			UE_LOG(LogTarinoi, Error, TEXT("Codegen: no synced content. Sync before generating bindings."));
			return Model;
		}

		for (const FDeclRow& Row : QueryDecls(Database, TEXT("function-declaration")))
		{
			FTarinoiFunctionDecl& Decl = Model.Functions.FindOrAdd(Row.Collection).AddDefaulted_GetRef();
			Decl.Name = Row.Identifier;
			Decl.Returns = TarinoiJson::Str(Row.Payload, TEXT("function_returns"));
			Decl.Effect = TarinoiJson::Str(Row.Payload, TEXT("function_effect"));
			if (const TArray<TSharedPtr<FJsonValue>>* Args = TarinoiJson::Arr(Row.Payload, TEXT("function_args")))
			{
				for (const TSharedPtr<FJsonValue>& Arg : *Args)
				{
					const FString ArgName = Arg.IsValid() && Arg->Type == EJson::Object ? TarinoiJson::Str(Arg->AsObject(), TEXT("arg_name")) : FString();
					if (!ArgName.IsEmpty())
					{
						Decl.Args.Add(ArgName);
					}
				}
			}
		}

		for (const FDeclRow& Row : QueryDecls(Database, TEXT("variable-declaration")))
		{
			FTarinoiVariableDecl& Decl = Model.Variables.FindOrAdd(Row.Collection).AddDefaulted_GetRef();
			Decl.Name = Row.Identifier;
			Decl.DataType = TarinoiJson::Str(Row.Payload, TEXT("data_type"));
			Decl.DefaultValue = Row.Payload->TryGetField(TEXT("default_value"));
		}

		for (const FDeclRow& Row : QueryDecls(Database, TEXT("list-spec")))
		{
			FTarinoiListDecl& Decl = Model.Lists.FindOrAdd(Row.Collection).AddDefaulted_GetRef();
			Decl.Identifier = Row.Identifier;
			const TArray<TSharedPtr<FJsonValue>>* Options = TarinoiJson::Arr(Row.Payload, TEXT("list_options"));
			if (!Options)
			{
				Options = TarinoiJson::Arr(Row.Payload, TEXT("options"));
			}
			if (Options)
			{
				for (const TSharedPtr<FJsonValue>& Option : *Options)
				{
					const FString Key = Option.IsValid() && Option->Type == EJson::Object ? TarinoiJson::Str(Option->AsObject(), TEXT("key")) : FString();
					if (!Key.IsEmpty())
					{
						Decl.OptionKeys.Add(Key);
					}
				}
			}
		}

		// Only entities that can speak are worth a constant.
		for (const FDeclRow& Row : QueryDecls(Database, TEXT("entity"), TEXT("AND json_extract(d.payload, '$.dialog_capable') = 1")))
		{
			Model.Entities.FindOrAdd(Row.Collection).Add(Row.Identifier);
		}

		return Model;
	}
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

namespace
{
	void Header(FTarinoiCodeWriter& W, const FString& ProjectId)
	{
		W.Line(TEXT("// Generated by Tarinoi from the synced content of project '") + TarinoiCodeNames::Comment(ProjectId) + TEXT("'."));
		W.Line(TEXT("// Do not edit: regenerating overwrites this file. Implement your bindings in a class"));
		W.Line(TEXT("// derived from these, in C++ or in Blueprint."));
		W.Line();
	}

	TArray<FTarinoiFunctionDecl> SortedByName(TArray<FTarinoiFunctionDecl> Decls)
	{
		Decls.StableSort([](const FTarinoiFunctionDecl& A, const FTarinoiFunctionDecl& B) { return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0; });
		return Decls;
	}

	FString ParameterList(const FTarinoiFunctionDecl& Fn)
	{
		TArray<FString> Params;
		TSet<FString> Seen;
		for (const FString& Arg : Fn.Args)
		{
			FString Name = TarinoiCodeNames::Parameter(Arg);
			// Two arguments that PascalCase to the same name still need distinct parameters.
			for (int32 Suffix = 2; Seen.Contains(Name); ++Suffix)
			{
				Name = TarinoiCodeNames::Parameter(Arg) + FString::FromInt(Suffix);
			}
			Seen.Add(Name);
			Params.Add(TEXT("const FTarinoiValue& ") + Name);
		}
		return FString::Join(Params, TEXT(", "));
	}

	FString Signature(const FTarinoiFunctionDecl& Fn)
	{
		return FString::Printf(TEXT("%s(%s) -> %s"), *Fn.Name, *FString::Join(Fn.Args, TEXT(", ")), Fn.Returns.IsEmpty() ? TEXT("void") : *Fn.Returns);
	}

	FString RenderFunctionsHeader(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(TEXT("#pragma once"));
		W.Line();
		W.Line(TEXT("#include \"Bindings/TarinoiBindings.h\""));
		W.Line(TEXT("#include \"CoreMinimal.h\""));
		W.Line();
		W.Line(FString::Printf(TEXT("#include \"%sGeneratedFunctions.generated.h\""), *Options.ClassPrefix));

		const TArray<FString> Collections = FTarinoiCodegenModel::SortedKeys(Model.Functions);
		if (Collections.Num() == 0)
		{
			W.Line();
			W.Line(TEXT("// No function collections have been synced yet."));
		}

		for (const FString& Collection : Collections)
		{
			const FString ClassName = TarinoiCodeNames::CollectionClass(Collection, TEXT("Functions"), Options.ClassPrefix);
			W.Line();
			W.Doc({FString::Printf(TEXT("Functions the author calls as Fn.%s.*"), *TarinoiCodeNames::Comment(Collection)), FString(),
				TEXT("Derive from this, in C++ or Blueprint, and implement the ones your game uses; bind"),
				FString::Printf(TEXT("an instance under \"%s\"."), *TarinoiCodeNames::Comment(Collection))});
			W.Line(FString::Printf(TEXT("UCLASS(Abstract, Blueprintable, meta = (TarinoiCollection = \"%s\"))"), *Collection.ReplaceCharWithEscapedChar()));
			W.Line(FString::Printf(TEXT("class %s U%s : public UTarinoiFunctionCollection"), *Options.ApiMacro, *ClassName));
			W.Open();
			W.Line(TEXT("GENERATED_BODY()"));
			W.Line();
			W.Outdent();
			W.Line(TEXT("public:"));
			W.Indent();

			for (const FTarinoiFunctionDecl& Fn : SortedByName(Model.Functions[Collection]))
			{
				if (Fn.Effect.IsEmpty())
				{
					W.Doc({TarinoiCodeNames::Comment(Signature(Fn))});
				}
				else
				{
					W.Doc({TarinoiCodeNames::Comment(Signature(Fn)), TEXT("Effect: ") + TarinoiCodeNames::Comment(Fn.Effect)});
				}
				W.Line(FString::Printf(TEXT("UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = \"Tarinoi|Fn.%s\")"), *Collection.ReplaceCharWithEscapedChar()));
				W.Line(FString::Printf(TEXT("%s %s(%s);"), *TarinoiCodeTypes::ForReturn(Fn.Returns), *TarinoiCodeNames::Function(Fn.Name), *ParameterList(Fn)));
				W.Line();
			}

			W.Line(TEXT("virtual bool HasFunction(FName Name) const override;"));
			W.Line(TEXT("virtual bool TryInvoke(FName Name, const TArray<FTarinoiValue>& Args, FTarinoiValue& OutResult) override;"));
			W.Close(TEXT(";"));
		}

		return W.ToString();
	}

	FString RenderFunctionsSource(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(FString::Printf(TEXT("#include \"%s\""), *TarinoiCodegen::FunctionsHeader(Options.ClassPrefix)));

		for (const FString& Collection : FTarinoiCodegenModel::SortedKeys(Model.Functions))
		{
			const FString ClassName = TEXT("U") + TarinoiCodeNames::CollectionClass(Collection, TEXT("Functions"), Options.ClassPrefix);
			const TArray<FTarinoiFunctionDecl> Functions = SortedByName(Model.Functions[Collection]);

			for (const FTarinoiFunctionDecl& Fn : Functions)
			{
				const FString Member = TarinoiCodeNames::Function(Fn.Name);
				W.Line();
				W.Line(FString::Printf(TEXT("%s %s::%s_Implementation(%s)"), *TarinoiCodeTypes::ForReturn(Fn.Returns), *ClassName, *Member, *ParameterList(Fn)));
				W.Open();
				W.Line(FString::Printf(TEXT("LogUnimplemented(TEXT(\"%s\"));"), *Member));
				const FString Default = TarinoiCodeTypes::DefaultReturn(Fn.Returns);
				if (!Default.IsEmpty())
				{
					W.Line(FString::Printf(TEXT("return %s;"), *Default));
				}
				W.Close();
			}

			W.Line();
			W.Line(FString::Printf(TEXT("bool %s::HasFunction(FName Name) const"), *ClassName));
			W.Open();
			if (Functions.Num() == 0)
			{
				W.Line(TEXT("return false;"));
			}
			else
			{
				W.Line(TEXT("static const FName Names[] = {"));
				W.Indent();
				for (const FTarinoiFunctionDecl& Fn : Functions)
				{
					W.Line(FString::Printf(TEXT("FName(%s),"), *TarinoiCodeNames::Literal(Fn.Name)));
				}
				W.Outdent();
				W.Line(TEXT("};"));
				W.Line(TEXT("for (const FName& Known : Names)"));
				W.Open();
				W.Line(TEXT("if (Known == Name)"));
				W.Open();
				W.Line(TEXT("return true;"));
				W.Close();
				W.Close();
				W.Line(TEXT("return false;"));
			}
			W.Close();

			W.Line();
			W.Line(FString::Printf(TEXT("bool %s::TryInvoke(FName Name, const TArray<FTarinoiValue>& Args, FTarinoiValue& OutResult)"), *ClassName));
			W.Open();
			W.Line(TEXT("// A plain dispatch on the authored name, calling each BlueprintNativeEvent, so a"));
			W.Line(TEXT("// Blueprint override is reached the same way as a C++ one."));
			W.Line(TEXT("OutResult = FTarinoiValue();"));
			for (const FTarinoiFunctionDecl& Fn : Functions)
			{
				const FString Member = TarinoiCodeNames::Function(Fn.Name);
				TArray<FString> CallArgs;
				for (int32 Index = 0; Index < Fn.Args.Num(); ++Index)
				{
					CallArgs.Add(FString::Printf(TEXT("Args[%d]"), Index));
				}
				const FString Call = FString::Printf(TEXT("%s(%s)"), *Member, *FString::Join(CallArgs, TEXT(", ")));

				W.Line();
				W.Line(FString::Printf(TEXT("static const FName Name%s(%s);"), *Member, *TarinoiCodeNames::Literal(Fn.Name)));
				W.Line(FString::Printf(TEXT("if (Name == Name%s)"), *Member));
				W.Open();
				// Checked, not assumed: the synced declaration can change after this file was generated.
				W.Line(FString::Printf(TEXT("if (ArityMatches(Name, Args, %d))"), Fn.Args.Num()));
				W.Open();
				if (TarinoiCodeTypes::ForReturn(Fn.Returns) == TEXT("void"))
				{
					W.Line(Call + TEXT(";"));
				}
				else
				{
					W.Line(FString::Printf(TEXT("OutResult = %s;"), *TarinoiCodeTypes::WrapResult(Fn.Returns, Call)));
				}
				W.Close();
				W.Line(TEXT("return true;"));
				W.Close();
			}
			W.Line();
			W.Line(TEXT("return false;"));
			W.Close();
		}

		return W.ToString();
	}

	FString ReadExpression(const FTarinoiVariableDecl& Var)
	{
		const FString Member = TarinoiCodeNames::Variable(Var.Name);
		const FString Type = TarinoiCodeTypes::ForData(Var.DataType);
		if (Type == TEXT("bool")) return FString::Printf(TEXT("FTarinoiValue::MakeBool(%s)"), *Member);
		if (Type == TEXT("double")) return FString::Printf(TEXT("FTarinoiValue::MakeNumber(%s)"), *Member);
		if (Type == TEXT("FString")) return FString::Printf(TEXT("FTarinoiValue::MakeString(%s)"), *Member);
		return Member;
	}

	FString WriteExpression(const FTarinoiVariableDecl& Var)
	{
		// Values arrive loosely typed, so assignment converts rather than asserting.
		const FString Type = TarinoiCodeTypes::ForData(Var.DataType);
		if (Type == TEXT("bool")) return TEXT("Value.ToBool()");
		if (Type == TEXT("double")) return TEXT("Value.ToNumber()");
		if (Type == TEXT("FString")) return TEXT("Value.ToString()");
		return TEXT("Value.Resolve()");
	}

	TArray<FTarinoiVariableDecl> SortedVariables(TArray<FTarinoiVariableDecl> Decls)
	{
		Decls.StableSort([](const FTarinoiVariableDecl& A, const FTarinoiVariableDecl& B) { return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0; });
		return Decls;
	}

	FString RenderVariablesHeader(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(TEXT("#pragma once"));
		W.Line();
		W.Line(TEXT("#include \"Bindings/TarinoiBindings.h\""));
		W.Line(TEXT("#include \"CoreMinimal.h\""));
		W.Line();
		W.Line(FString::Printf(TEXT("#include \"%sGeneratedVariables.generated.h\""), *Options.ClassPrefix));

		const TArray<FString> Collections = FTarinoiCodegenModel::SortedKeys(Model.Variables);
		if (Collections.Num() == 0)
		{
			W.Line();
			W.Line(TEXT("// No variable collections have been synced yet."));
		}

		for (const FString& Collection : Collections)
		{
			const FString ClassName = TarinoiCodeNames::CollectionClass(Collection, TEXT("Variables"), Options.ClassPrefix);
			W.Line();
			W.Doc({FString::Printf(TEXT("State the author reads and writes as Var.%s.*"), *TarinoiCodeNames::Comment(Collection)), FString(),
				TEXT("The properties hold the values: set them from game code, or derive (in C++ or Blueprint)"),
				TEXT("and override GetVariable/SetVariable to keep them somewhere else, such as a save game.")});
			W.Line(FString::Printf(TEXT("UCLASS(Blueprintable, BlueprintType, meta = (TarinoiCollection = \"%s\"))"), *Collection.ReplaceCharWithEscapedChar()));
			W.Line(FString::Printf(TEXT("class %s U%s : public UTarinoiVariableCollection"), *Options.ApiMacro, *ClassName));
			W.Open();
			W.Line(TEXT("GENERATED_BODY()"));
			W.Line();
			W.Outdent();
			W.Line(TEXT("public:"));
			W.Indent();
			W.Doc({TEXT("The collection identifier this class binds under.")});
			W.Line(FString::Printf(TEXT("static constexpr const TCHAR* Collection = %s;"), *TarinoiCodeNames::Literal(Collection)));
			W.Line();
			W.Line(TEXT("virtual FString GetCollectionIdentifier() const override { return Collection; }"));

			for (const FTarinoiVariableDecl& Var : SortedVariables(Model.Variables[Collection]))
			{
				const FString Default = TarinoiCodeTypes::DefaultValue(Var.DataType, Var.DefaultValue);
				W.Line();
				W.Doc({FString::Printf(TEXT("Var.%s.%s"), *TarinoiCodeNames::Comment(Collection), *TarinoiCodeNames::Comment(Var.Name))});
				W.Line(FString::Printf(TEXT("UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"Tarinoi|Var.%s\")"), *Collection.ReplaceCharWithEscapedChar()));
				W.Line(FString::Printf(TEXT("%s %s%s;"), *TarinoiCodeTypes::ForData(Var.DataType), *TarinoiCodeNames::Variable(Var.Name),
					Default.IsEmpty() ? TEXT("") : *(TEXT(" = ") + Default)));
			}

			W.Line();
			W.Line(TEXT("virtual FTarinoiValue GetVariable_Implementation(FName Name) override;"));
			W.Line(TEXT("virtual void SetVariable_Implementation(FName Name, const FTarinoiValue& Value) override;"));
			W.Close(TEXT(";"));
		}

		return W.ToString();
	}

	FString RenderVariablesSource(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(FString::Printf(TEXT("#include \"%s\""), *TarinoiCodegen::VariablesHeader(Options.ClassPrefix)));
		W.Line();
		W.Line(TEXT("#include \"Tarinoi.h\""));

		for (const FString& Collection : FTarinoiCodegenModel::SortedKeys(Model.Variables))
		{
			const FString ClassName = TEXT("U") + TarinoiCodeNames::CollectionClass(Collection, TEXT("Variables"), Options.ClassPrefix);
			const TArray<FTarinoiVariableDecl> Variables = SortedVariables(Model.Variables[Collection]);

			W.Line();
			W.Line(FString::Printf(TEXT("FTarinoiValue %s::GetVariable_Implementation(FName Name)"), *ClassName));
			W.Open();
			for (const FTarinoiVariableDecl& Var : Variables)
			{
				W.Line(FString::Printf(TEXT("if (Name == FName(%s))"), *TarinoiCodeNames::Literal(Var.Name)));
				W.Open();
				W.Line(FString::Printf(TEXT("return %s;"), *ReadExpression(Var)));
				W.Close();
			}
			W.Line(FString::Printf(TEXT("UE_LOG(LogTarinoi, Warning, TEXT(\"%s has no variable '%%s'. Regenerate your bindings.\"), *Name.ToString());"), *ClassName));
			W.Line(TEXT("return FTarinoiValue();"));
			W.Close();

			W.Line();
			W.Line(FString::Printf(TEXT("void %s::SetVariable_Implementation(FName Name, const FTarinoiValue& Value)"), *ClassName));
			W.Open();
			for (const FTarinoiVariableDecl& Var : Variables)
			{
				W.Line(FString::Printf(TEXT("if (Name == FName(%s))"), *TarinoiCodeNames::Literal(Var.Name)));
				W.Open();
				W.Line(FString::Printf(TEXT("%s = %s;"), *TarinoiCodeNames::Variable(Var.Name), *WriteExpression(Var)));
				W.Line(TEXT("return;"));
				W.Close();
			}
			W.Line(FString::Printf(TEXT("UE_LOG(LogTarinoi, Warning, TEXT(\"%s has no variable '%%s'. Regenerate your bindings.\"), *Name.ToString());"), *ClassName));
			W.Close();
		}

		return W.ToString();
	}

	FString RenderLists(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(TEXT("#pragma once"));
		W.Line();
		W.Line(TEXT("#include \"CoreMinimal.h\""));
		W.Line();
		W.Doc({TEXT("Option keys for the authored lists, as Ls.collection.list.key names them.")});
		W.Line(TEXT("namespace TarinoiLists"));
		W.Open();

		const TArray<FString> Collections = FTarinoiCodegenModel::SortedKeys(Model.Lists);
		if (Collections.Num() == 0)
		{
			W.Line(TEXT("// No option lists have been synced yet."));
		}

		bool bFirst = true;
		for (const FString& Collection : Collections)
		{
			if (!bFirst)
			{
				W.Line();
			}
			bFirst = false;

			W.Doc({FString::Printf(TEXT("Ls.%s.*"), *TarinoiCodeNames::Comment(Collection))});
			W.Line(TEXT("namespace ") + TarinoiNames::ToPascal(Collection));
			W.Open();

			TArray<FTarinoiListDecl> Lists = Model.Lists[Collection];
			Lists.StableSort([](const FTarinoiListDecl& A, const FTarinoiListDecl& B) { return A.Identifier.Compare(B.Identifier, ESearchCase::CaseSensitive) < 0; });
			bool bFirstList = true;
			for (const FTarinoiListDecl& List : Lists)
			{
				if (!bFirstList)
				{
					W.Line();
				}
				bFirstList = false;

				W.Line(TEXT("namespace ") + TarinoiNames::ToPascal(List.Identifier));
				W.Open();
				TArray<FString> Keys = List.OptionKeys;
				Keys.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
				for (const FString& Key : Keys)
				{
					W.Line(FString::Printf(TEXT("constexpr const TCHAR* %s = %s;"), *TarinoiNames::ToPascal(Key), *TarinoiCodeNames::Literal(Key)));
				}
				W.Close();
			}
			W.Close();
		}

		W.Close();
		return W.ToString();
	}

	FString RenderEntities(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		FTarinoiCodeWriter W;
		Header(W, Options.ProjectId);
		W.Line(TEXT("#pragma once"));
		W.Line();
		W.Line(TEXT("#include \"CoreMinimal.h\""));
		W.Line();
		W.Doc({TEXT("Identifiers of the entities that can take part in dialogue, as Ent.collection.name names them.")});
		W.Line(TEXT("namespace TarinoiEntities"));
		W.Open();

		const TArray<FString> Collections = FTarinoiCodegenModel::SortedKeys(Model.Entities);
		if (Collections.Num() == 0)
		{
			W.Line(TEXT("// No dialogue-capable entities have been synced yet."));
		}

		bool bFirst = true;
		for (const FString& Collection : Collections)
		{
			if (!bFirst)
			{
				W.Line();
			}
			bFirst = false;

			W.Doc({FString::Printf(TEXT("Ent.%s.*"), *TarinoiCodeNames::Comment(Collection))});
			W.Line(TEXT("namespace ") + TarinoiNames::ToPascal(Collection));
			W.Open();
			TArray<FString> Ids = Model.Entities[Collection];
			Ids.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
			for (const FString& Id : Ids)
			{
				W.Line(FString::Printf(TEXT("constexpr const TCHAR* %s = %s;"), *TarinoiNames::ToPascal(Id), *TarinoiCodeNames::Literal(Id)));
			}
			W.Close();
		}

		W.Close();
		return W.ToString();
	}
}

namespace TarinoiCodegen
{
	FTarinoiGeneratedFiles Render(const FTarinoiCodegenModel& Model, const FTarinoiCodegenOptions& Options)
	{
		const FString& Prefix = Options.ClassPrefix;
		FTarinoiGeneratedFiles Out;
		Out.Files.Add(FunctionsHeader(Prefix), RenderFunctionsHeader(Model, Options));
		Out.Files.Add(FileName(Prefix, TEXT("Functions"), TEXT("cpp")), RenderFunctionsSource(Model, Options));
		Out.Files.Add(VariablesHeader(Prefix), RenderVariablesHeader(Model, Options));
		Out.Files.Add(FileName(Prefix, TEXT("Variables"), TEXT("cpp")), RenderVariablesSource(Model, Options));
		Out.Files.Add(ListsHeader(Prefix), RenderLists(Model, Options));
		Out.Files.Add(EntitiesHeader(Prefix), RenderEntities(Model, Options));
		return Out;
	}

	bool Write(const FTarinoiGeneratedFiles& Files, const FString& OutputDir)
	{
		IFileManager::Get().MakeDirectory(*OutputDir, true);

		bool bOk = true;
		for (const TPair<FString, FString>& File : Files.Files)
		{
			const FString Path = FPaths::Combine(OutputDir, File.Key);

			// Unchanged files are left alone, so regenerating does not trigger a rebuild.
			FString Existing;
			if (FFileHelper::LoadFileToString(Existing, *Path) && Existing == File.Value)
			{
				continue;
			}

			if (!FFileHelper::SaveStringToFile(File.Value, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogTarinoi, Error, TEXT("Codegen: could not write '%s'"), *Path);
				bOk = false;
			}
		}
		return bOk;
	}
}
