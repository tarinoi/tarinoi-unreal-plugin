// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Codegen/TarinoiCodegen.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "TarinoiCodegenFixture.h"
#include "TarinoiNames.h"
#include "TarinoiTestDb.h"
#include "TarinoiTestHelpers.h"

namespace TarinoiCodegenTests
{
	FString Functions(const FTarinoiCodegenModel& Model)
	{
		const FTarinoiGeneratedFiles Files = TarinoiCodegen::Render(Model, TarinoiCodegenFixture::Options());
		return Files.Files[TarinoiCodegen::FunctionsHeader] + Files.Files[TEXT("TarinoiGeneratedFunctions.cpp")];
	}

	FString Variables(const FTarinoiCodegenModel& Model)
	{
		const FTarinoiGeneratedFiles Files = TarinoiCodegen::Render(Model, TarinoiCodegenFixture::Options());
		return Files.Files[TarinoiCodegen::VariablesHeader] + Files.Files[TEXT("TarinoiGeneratedVariables.cpp")];
	}
}

BEGIN_DEFINE_SPEC(FTarinoiCodegenSpec, "Tarinoi.Codegen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiCodegenSpec)

void FTarinoiCodegenSpec::Define()
{
	using namespace TarinoiCodegenTests;

	Describe("names", [this]()
	{
		It("turns authored identifiers into PascalCase", [this]()
		{
			const TPair<const TCHAR*, const TCHAR*> Cases[] = {
				{TEXT("global"), TEXT("Global")}, {TEXT("player_state"), TEXT("PlayerState")}, {TEXT("my_col2"), TEXT("MyCol2")},
				{TEXT("already Pascal"), TEXT("AlreadyPascal")}, {TEXT("kebab-case"), TEXT("KebabCase")}, {TEXT("dotted.name"), TEXT("DottedName")},
				{TEXT("flagRef"), TEXT("FlagRef")},
			};
			for (const TPair<const TCHAR*, const TCHAR*>& Case : Cases)
			{
				TestEqualSensitive(Case.Key, TarinoiNames::ToPascal(Case.Key), FString(Case.Value));
			}
		});

		It("makes a name starting with a digit legal", [this]()
		{
			TestEqualSensitive("digit", TarinoiNames::ToPascal(TEXT("2nd_try")), FString(TEXT("_2ndTry")));
		});

		It("falls back rather than emitting a broken name", [this]()
		{
			for (const TCHAR* Authored : {TEXT(""), TEXT("!!!"), TEXT("___"), TEXT("äö")})
			{
				TestEqualSensitive(Authored, TarinoiNames::ToPascal(Authored), FString(TEXT("Unnamed")));
			}
		});

		It("prefixes names that would clash with what every binding class has", [this]()
		{
			TestEqualSensitive("function", TarinoiCodeNames::Function(TEXT("rename")), FString(TEXT("FnRename")));
			TestEqualSensitive("variable", TarinoiCodeNames::Variable(TEXT("is_valid")), FString(TEXT("VarIsValid")));
			TestEqualSensitive("ordinary", TarinoiCodeNames::Function(TEXT("roll")), FString(TEXT("Roll")));
		});

		It("names collection classes with the Tarinoi prefix", [this]()
		{
			TestEqualSensitive("functions", TarinoiCodeNames::CollectionClass(TEXT("global"), TEXT("Functions")), FString(TEXT("TarinoiGlobalFunctions")));
			TestEqualSensitive("core base", TarinoiCodeNames::CollectionClass(TEXT("tarinoi"), TEXT("Functions")), FString(TEXT("TarinoiTarinoiFunctions")));
		});

		It("escapes string literals, keeping the file ASCII", [this]()
		{
			TestEqualSensitive("escaped", TarinoiCodeNames::Literal(TEXT("a\"b\\c\ndä")), FString(TEXT("TEXT(\"a\\\"b\\\\c\\nd\\u00e4\")")));
		});

		It("keeps authored text from closing a comment", [this]()
		{
			TestFalse("closed", TarinoiCodeNames::Comment(TEXT("sneaky */ code")).Contains(TEXT("*/")));
		});
	});

	Describe("types", [this]()
	{
		It("maps data types, numbers to double", [this]()
		{
			const TPair<const TCHAR*, const TCHAR*> Cases[] = {
				{TEXT("boolean"), TEXT("bool")}, {TEXT("number"), TEXT("double")}, {TEXT("string"), TEXT("FString")},
				{TEXT("BOOLEAN"), TEXT("bool")}, {TEXT("something_new"), TEXT("FTarinoiValue")}, {TEXT(""), TEXT("FTarinoiValue")},
			};
			for (const TPair<const TCHAR*, const TCHAR*>& Case : Cases)
			{
				TestEqualSensitive(Case.Key, TarinoiCodeTypes::ForData(Case.Key), FString(Case.Value));
			}
		});

		It("maps return types, with void for nothing", [this]()
		{
			TestEqualSensitive("void", TarinoiCodeTypes::ForReturn(TEXT("void")), FString(TEXT("void")));
			TestEqualSensitive("empty", TarinoiCodeTypes::ForReturn(TEXT("")), FString(TEXT("void")));
			TestEqualSensitive("bool", TarinoiCodeTypes::ForReturn(TEXT("boolean")), FString(TEXT("bool")));
		});

		It("gives unimplemented functions a typed default", [this]()
		{
			TestEqualSensitive("void", TarinoiCodeTypes::DefaultReturn(TEXT("void")), FString());
			TestEqualSensitive("bool", TarinoiCodeTypes::DefaultReturn(TEXT("boolean")), FString(TEXT("false")));
			TestEqualSensitive("number", TarinoiCodeTypes::DefaultReturn(TEXT("number")), FString(TEXT("0.0")));
			TestEqualSensitive("string", TarinoiCodeTypes::DefaultReturn(TEXT("string")), FString(TEXT("FString()")));
			TestEqualSensitive("other", TarinoiCodeTypes::DefaultReturn(TEXT("other")), FString(TEXT("FTarinoiValue()")));
		});

		It("initialises variables from the declared default, or the type's zero", [this]()
		{
			TestEqualSensitive("bool", TarinoiCodeTypes::DefaultValue(TEXT("boolean"), MakeShared<FJsonValueBoolean>(true)), FString(TEXT("true")));
			TestEqualSensitive("number", TarinoiCodeTypes::DefaultValue(TEXT("number"), MakeShared<FJsonValueNumber>(3)), FString(TEXT("3.0")));
			TestEqualSensitive("fraction", TarinoiCodeTypes::DefaultValue(TEXT("number"), MakeShared<FJsonValueNumber>(0.25)), FString(TEXT("0.25")));
			TestEqualSensitive("string", TarinoiCodeTypes::DefaultValue(TEXT("string"), MakeShared<FJsonValueString>(TEXT("say \"hi\""))), FString(TEXT("TEXT(\"say \\\"hi\\\"\")")));
			TestEqualSensitive("bool zero", TarinoiCodeTypes::DefaultValue(TEXT("boolean"), nullptr), FString(TEXT("false")));
			TestEqualSensitive("number zero", TarinoiCodeTypes::DefaultValue(TEXT("number"), nullptr), FString(TEXT("0.0")));
			TestEqualSensitive("string zero", TarinoiCodeTypes::DefaultValue(TEXT("string"), nullptr), FString());
		});
	});

	Describe("emitted code", [this]()
	{
		It("generates one abstract, Blueprintable class per function collection", [this]()
		{
			const FString Code = Functions(TarinoiCodegenFixture::Model());
			TestTrue("global", Code.Contains(TEXT("class TARINOITESTS_API UTarinoiFixtureGlobalFunctions : public UTarinoiFunctionCollection")));
			TestTrue("core", Code.Contains(TEXT("class TARINOITESTS_API UTarinoiFixtureTarinoiFunctions : public UTarinoiFunctionCollection")));
			TestTrue("blueprintable", Code.Contains(TEXT("UCLASS(Abstract, Blueprintable")));
		});

		It("makes every function a BlueprintNativeEvent taking FTarinoiValue", [this]()
		{
			const FString Code = Functions(TarinoiCodegenFixture::Model());
			TestTrue("event", Code.Contains(TEXT("UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = \"Tarinoi|Fn.global\")")));
			TestTrue("typed return", Code.Contains(TEXT("double Roll(const FTarinoiValue& Sides, const FTarinoiValue& Bonus);")));
			TestTrue("no args", Code.Contains(TEXT("FString PickPin();")));
			TestTrue("reserved name prefixed", Code.Contains(TEXT("void FnRename(const FTarinoiValue& Who);")));
		});

		It("documents each function with its signature and effect", [this]()
		{
			TestTrue("doc", Functions(TarinoiCodegenFixture::Model()).Contains(TEXT("Roll(sides, bonus) -> number")));
			TestTrue("effect", Functions(TarinoiCodegenFixture::Model()).Contains(TEXT("Effect: mutation")));
		});

		It("logs rather than failing in unimplemented defaults", [this]()
		{
			TestTrue("logs", Functions(TarinoiCodegenFixture::Model()).Contains(TEXT("LogUnimplemented(TEXT(\"Roll\"));")));
		});

		It("dispatches by name, checking the argument count", [this]()
		{
			const FString Code = Functions(TarinoiCodegenFixture::Model());
			TestTrue("arity", Code.Contains(TEXT("if (ArityMatches(Name, Args, 2))")));
			TestTrue("wrapped result", Code.Contains(TEXT("OutResult = FTarinoiValue::MakeNumber(Roll(Args[0], Args[1]));")));
			TestTrue("void call", Code.Contains(TEXT("FnRename(Args[0]);")));
			TestFalse("no reflection", Code.Contains(TEXT("FindFunction")));
		});

		It("lists every declared function in HasFunction", [this]()
		{
			const FString Code = Functions(TarinoiCodegenFixture::Model());
			for (const TCHAR* Name : {TEXT("CheckGate"), TEXT("Roll"), TEXT("PickPin"), TEXT("Rename"), TEXT("Describe")})
			{
				TestTrue(Name, Code.Contains(FString::Printf(TEXT("FName(TEXT(\"%s\")),"), Name)));
			}
		});

		It("still produces files for an empty model", [this]()
		{
			const FTarinoiGeneratedFiles Files = TarinoiCodegen::Render(FTarinoiCodegenModel(), TarinoiCodegenFixture::Options());
			TestEqual("six files", Files.Files.Num(), 6);
			TestTrue("says why", Files.Files[TarinoiCodegen::FunctionsHeader].Contains(TEXT("No function collections have been synced yet.")));
		});

		It("turns variables into typed, Blueprint-editable properties", [this]()
		{
			const FString Code = Variables(TarinoiCodegenFixture::Model());
			TestTrue("class", Code.Contains(TEXT("class TARINOITESTS_API UTarinoiFixtureStateVariables : public UTarinoiVariableCollection")));
			TestTrue("property", Code.Contains(TEXT("UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"Tarinoi|Var.state\")")));
			TestTrue("bool", Code.Contains(TEXT("bool MetFerryman = true;")));
			TestTrue("double", Code.Contains(TEXT("double Gold = 12.5;")));
			TestTrue("string", Code.Contains(TEXT("FString PlayerName = TEXT(\"Ad\\u00e4 \\\"the\\\" Bold\");")));
			TestTrue("unset string", Code.Contains(TEXT("FString Mood;")));
			TestTrue("other", Code.Contains(TEXT("FTarinoiValue Anything;")));
		});

		It("keys variable access on the authored name", [this]()
		{
			const FString Code = Variables(TarinoiCodegenFixture::Model());
			TestTrue("get", Code.Contains(TEXT("if (Name == FName(TEXT(\"met_ferryman\")))")));
			TestTrue("set converts", Code.Contains(TEXT("MetFerryman = Value.ToBool();")));
			TestTrue("identifier", Code.Contains(TEXT("static constexpr const TCHAR* Collection = TEXT(\"state\");")));
		});

		It("turns list option keys into constants, sorted, using the key", [this]()
		{
			const FString Lists = TarinoiCodegen::Render(TarinoiCodegenFixture::Model(), TarinoiCodegenFixture::Options()).Files[TarinoiCodegen::ListsHeader];
			TestTrue("constant", Lists.Contains(TEXT("constexpr const TCHAR* Heroic = TEXT(\"heroic\");")));
			TestTrue("sorted", Lists.Find(TEXT("Easy")) < Lists.Find(TEXT("Hard")) && Lists.Find(TEXT("Hard")) < Lists.Find(TEXT("Heroic")));
		});

		It("turns entity identifiers into constants", [this]()
		{
			const FString Entities = TarinoiCodegen::Render(TarinoiCodegenFixture::Model(), TarinoiCodegenFixture::Options()).Files[TarinoiCodegen::EntitiesHeader];
			TestTrue("constant", Entities.Contains(TEXT("constexpr const TCHAR* Ferryman = TEXT(\"ferryman\");")));
		});

		It("is deterministic, with no timestamp", [this]()
		{
			const TMap<FString, FString> First = TarinoiCodegenFixture::RenderAll();
			const TMap<FString, FString> Second = TarinoiCodegenFixture::RenderAll();
			for (const TPair<FString, FString>& File : First)
			{
				TestEqualSensitive(*File.Key, Second[File.Key], File.Value);
				TestFalse("no year", File.Value.Contains(TEXT("2026")));
			}
		});
	});

	Describe("the golden fixture", [this]()
	{
		It("matches what the emitter renders, byte for byte", [this]()
		{
			// The fixture files are compiled into this module, which is what proves generated code
			// compiles. If this fails after an intended emitter change, copy the files dumped to
			// Saved/TarinoiFixtures over Source/TarinoiTests/Private/Fixtures and rebuild.
			const FString DumpDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TarinoiFixtures"));
			for (const TPair<FString, FString>& File : TarinoiCodegenFixture::RenderAll())
			{
				FString Committed;
				FFileHelper::LoadFileToString(Committed, *FPaths::Combine(TarinoiCodegenFixture::Directory(), File.Key));
				if (!TestEqualSensitive(*File.Key, Committed, File.Value))
				{
					FFileHelper::SaveStringToFile(File.Value, *FPaths::Combine(DumpDir, File.Key), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
					AddInfo(FString::Printf(TEXT("Dumped %s to %s"), *File.Key, *DumpDir));
				}
			}
		});
	});

	Describe("loading from the database", [this]()
	{
		It("keys collections on the manifest's identifier, never its label", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("col-doc-1"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"),
				.Payload = TEXT("{\"label\":\"Global State\",\"collection_type\":\"function-collection\"}"), .Identifier = TEXT("global")});
			Fixture.Insert({.DocumentId = TEXT("fn1"), .CollectionId = TEXT("col-doc-1"), .DocumentType = TEXT("function-declaration"),
				.Payload = TEXT("{\"function_args\":[{\"arg_name\":\"door\"},{\"arg_name\":\"\"}],\"function_returns\":\"boolean\",\"function_effect\":\"pure\"}"),
				.Identifier = TEXT("CheckGate")});

			const FTarinoiCodegenModel Model = TarinoiCodegen::Load(*Fixture.Db);
			TarinoiTest::Strings(*this, TEXT("collections"), FTarinoiCodegenModel::SortedKeys(Model.Functions), {TEXT("global")});
			if (const TArray<FTarinoiFunctionDecl>* Decls = Model.Functions.Find(TEXT("global")))
			{
				TestEqualSensitive("name", (*Decls)[0].Name, FString(TEXT("CheckGate")));
				TarinoiTest::Strings(*this, TEXT("args skip blanks"), (*Decls)[0].Args, {TEXT("door")});
				TestEqualSensitive("returns", (*Decls)[0].Returns, FString(TEXT("boolean")));
				TestEqualSensitive("effect", (*Decls)[0].Effect, FString(TEXT("pure")));
			}
		});

		It("reads variables with their types and defaults", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("vc"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"), .Payload = TEXT("{}"), .Identifier = TEXT("state")});
			Fixture.Insert({.DocumentId = TEXT("v1"), .CollectionId = TEXT("vc"), .DocumentType = TEXT("variable-declaration"),
				.Payload = TEXT("{\"data_type\":\"number\",\"default_value\":7}"), .Identifier = TEXT("gold")});
			const FTarinoiCodegenModel Model = TarinoiCodegen::Load(*Fixture.Db);
			const FTarinoiVariableDecl& Var = Model.Variables[TEXT("state")][0];
			TestEqualSensitive("type", Var.DataType, FString(TEXT("number")));
			TestEqual("default", Var.DefaultValue->AsNumber(), 7.0);
		});

		It("uses list keys, not values, and falls back to the older options field", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("lc"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"), .Payload = TEXT("{}"), .Identifier = TEXT("global")});
			Fixture.Insert({.DocumentId = TEXT("l1"), .CollectionId = TEXT("lc"), .DocumentType = TEXT("list-spec"),
				.Payload = TEXT("{\"list_options\":[{\"key\":\"easy\",\"option_value\":5}]}"), .Identifier = TEXT("thresholds")});
			Fixture.Insert({.DocumentId = TEXT("l2"), .CollectionId = TEXT("lc"), .DocumentType = TEXT("list-spec"),
				.Payload = TEXT("{\"options\":[{\"key\":\"old\",\"value\":1}]}"), .Identifier = TEXT("legacy")});
			const FTarinoiCodegenModel Model = TarinoiCodegen::Load(*Fixture.Db);
			const TArray<FTarinoiListDecl>& Lists = Model.Lists[TEXT("global")];
			TarinoiTest::Strings(*this, TEXT("legacy"), Lists[0].OptionKeys, {TEXT("old")});
			TarinoiTest::Strings(*this, TEXT("thresholds"), Lists[1].OptionKeys, {TEXT("easy")});
		});

		It("generates only dialogue-capable entities", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("ec"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"), .Payload = TEXT("{}"), .Identifier = TEXT("cast")});
			Fixture.Insert({.DocumentId = TEXT("e1"), .CollectionId = TEXT("ec"), .DocumentType = TEXT("entity"), .Payload = TEXT("{\"dialog_capable\":true}"), .Identifier = TEXT("narrator")});
			Fixture.Insert({.DocumentId = TEXT("e2"), .CollectionId = TEXT("ec"), .DocumentType = TEXT("entity"), .Payload = TEXT("{\"dialog_capable\":false}"), .Identifier = TEXT("rock")});
			TarinoiTest::Strings(*this, TEXT("entities"), TarinoiCodegen::Load(*Fixture.Db).Entities[TEXT("cast")], {TEXT("narrator")});
		});

		It("excludes archived declarations and declarations with no manifest", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("fc"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"), .Payload = TEXT("{}"), .Identifier = TEXT("global")});
			Fixture.Insert({.DocumentId = TEXT("f1"), .CollectionId = TEXT("fc"), .DocumentType = TEXT("function-declaration"), .Payload = TEXT("{}"), .Identifier = TEXT("Gone"), .bArchived = true});
			Fixture.Insert({.DocumentId = TEXT("f2"), .CollectionId = TEXT("orphan"), .DocumentType = TEXT("function-declaration"), .Payload = TEXT("{}"), .Identifier = TEXT("Orphan")});
			TestTrue("nothing", TarinoiCodegen::Load(*Fixture.Db).IsEmpty());
		});

		It("skips an unreadable payload with a warning", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Insert({.DocumentId = TEXT("fc"), .CollectionId = TEXT("meta"), .DocumentType = TEXT("collection-manifest"), .Payload = TEXT("{}"), .Identifier = TEXT("global")});
			Fixture.Insert({.DocumentId = TEXT("f1"), .CollectionId = TEXT("fc"), .DocumentType = TEXT("function-declaration"), .Payload = TEXT("{broken"), .Identifier = TEXT("Bad")});
			AddExpectedMessage(TEXT("unreadable payload"), ELogVerbosity::Warning);
			TestTrue("nothing", TarinoiCodegen::Load(*Fixture.Db).IsEmpty());
		});

		It("reports a closed database", [this]()
		{
			FTarinoiTestDb Fixture;
			Fixture.Db->Close();
			AddExpectedError(TEXT("Sync before generating"));
			TestTrue("empty", TarinoiCodegen::Load(*Fixture.Db).IsEmpty());
		});
	});

	Describe("writing", [this]()
	{
		It("writes the files, and leaves unchanged ones untouched", [this]()
		{
			const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("TarinoiCodegen"), FGuid::NewGuid().ToString());
			const FTarinoiGeneratedFiles Files = TarinoiCodegen::Render(TarinoiCodegenFixture::Model(), TarinoiCodegenFixture::Options());
			TestTrue("wrote", TarinoiCodegen::Write(Files, Dir));
			const FString Header = FPaths::Combine(Dir, TarinoiCodegen::FunctionsHeader);
			const FDateTime Before = IFileManager::Get().GetTimeStamp(*Header);
			FPlatformProcess::Sleep(1.1f);
			TarinoiCodegen::Write(Files, Dir);
			TestTrue("untouched", IFileManager::Get().GetTimeStamp(*Header) == Before);
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		});
	});

	Describe("core functions scaffold", [this]()
	{
		It("emits functions in author order", [this]()
		{
			FString Header, Source;
			TArray<FString> Unknown;
			TarinoiCoreFunctions::Render(TarinoiCodegenFixture::CoreDecls(), TarinoiCodegenFixture::Options(), TEXT("X.h"), Header, Source, Unknown);
			TestTrue("SetFlag before ClearFlag", Header.Find(TEXT("SetFlag_Implementation")) < Header.Find(TEXT("ClearFlag_Implementation")));
			TestTrue("StringEquals last", Header.Find(TEXT("NumberLessThan_Implementation")) < Header.Find(TEXT("StringEquals_Implementation")));
			TestEqual("all known", Unknown.Num(), 0);
		});

		It("stubs a function it does not know, or one with the wrong arity", [this]()
		{
			TArray<FTarinoiFunctionDecl> Decls = TarinoiCodegenFixture::CoreDecls();
			Decls.Add(TarinoiCodegenFixture::Fn(TEXT("Mystery"), {}, TEXT("boolean"), TEXT("")));
			Decls[0].Args.Add(TEXT("extra")); // ClearFlag now has two arguments
			FString Header, Source;
			TArray<FString> Unknown;
			TarinoiCoreFunctions::Render(Decls, TarinoiCodegenFixture::Options(), TEXT("X.h"), Header, Source, Unknown);
			TarinoiTest::Strings(*this, TEXT("unknown"), Unknown, {TEXT("ClearFlag"), TEXT("Mystery")});
			TestTrue("stub", Source.Contains(TEXT("LogUnimplemented(TEXT(\"Mystery\"));")));
		});

		It("carries its version as class metadata", [this]()
		{
			FString Header, Source;
			TArray<FString> Unknown;
			TarinoiCoreFunctions::Render(TarinoiCodegenFixture::CoreDecls(), TarinoiCodegenFixture::Options(), TEXT("X.h"), Header, Source, Unknown);
			TestTrue("metadata", Header.Contains(FString::Printf(TEXT("UCLASS(Blueprintable, meta = (%s = \"%s\"))"), TarinoiCoreFunctions::VersionMetadata, TarinoiCoreFunctions::Version)));
		});

		It("is written once and never overwritten, and only with the core collection", [this]()
		{
			const FString Dir = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("TarinoiScaffold"), FGuid::NewGuid().ToString());
			const FTarinoiCodegenOptions Options = TarinoiCodegenFixture::Options();

			TestFalse("no collection, no scaffold", TarinoiCoreFunctions::Scaffold(FTarinoiCodegenModel(), Dir, Dir / TEXT("Generated"), Options));
			TestTrue("written", TarinoiCoreFunctions::Scaffold(TarinoiCodegenFixture::Model(), Dir, Dir / TEXT("Generated"), Options));

			const FString Header = FPaths::Combine(Dir, TarinoiCoreFunctions::ClassName(Options.ClassPrefix) + TEXT(".h"));
			FString Written;
			FFileHelper::LoadFileToString(Written, *Header);
			TestTrue("includes the generated header relatively", Written.Contains(TEXT("#include \"Generated/TarinoiGeneratedFunctions.h\"")));

			FFileHelper::SaveStringToFile(TEXT("// the game's edits"), *Header);
			TestFalse("not rewritten", TarinoiCoreFunctions::Scaffold(TarinoiCodegenFixture::Model(), Dir, Dir / TEXT("Generated"), Options));
			FFileHelper::LoadFileToString(Written, *Header);
			TestEqualSensitive("edits kept", Written, FString(TEXT("// the game's edits")));
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		});
	});
}

#endif
