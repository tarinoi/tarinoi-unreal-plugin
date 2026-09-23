// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TarinoiTestBindings.h"
#include "UObject/StrongObjectPtr.h"

BEGIN_DEFINE_SPEC(FTarinoiBindingsSpec, "Tarinoi.Bindings.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTarinoiBindings> Bindings;
END_DEFINE_SPEC(FTarinoiBindingsSpec)

void FTarinoiBindingsSpec::Define()
{
	BeforeEach([this]()
	{
		Bindings.Reset(NewObject<UTarinoiBindings>());
	});

	AfterEach([this]()
	{
		Bindings.Reset();
	});

	It("returns what was bound, and nothing for anything else", [this]()
	{
		UTarinoiTestFunctions* Functions = NewObject<UTarinoiTestFunctions>();
		Bindings->BindFunctions(TEXT("global"), Functions);
		TestTrue("bound", Bindings->GetFunctions(TEXT("global")) == Functions);
		TestNull("unbound", Bindings->GetFunctions(TEXT("other")));
	});

	It("keeps the three kinds apart", [this]()
	{
		Bindings->BindFunctions(TEXT("shared"), NewObject<UTarinoiTestFunctions>());
		TestNull("no variables", Bindings->GetVariables(TEXT("shared")));
		TestNull("no entities", Bindings->GetEntities(TEXT("shared")));
	});

	It("keys on the case-sensitive machine identifier", [this]()
	{
		Bindings->BindFunctions(TEXT("global"), NewObject<UTarinoiTestFunctions>());
		TestNull("label casing does not match", Bindings->GetFunctions(TEXT("Global")));
	});

	It("replaces on rebinding", [this]()
	{
		UTarinoiTestFunctions* Second = NewObject<UTarinoiTestFunctions>();
		Bindings->BindFunctions(TEXT("global"), NewObject<UTarinoiTestFunctions>());
		Bindings->BindFunctions(TEXT("global"), Second);
		TestTrue("second", Bindings->GetFunctions(TEXT("global")) == Second);
		TestEqual("one binding", Bindings->GetBoundFunctionCollections().Num(), 1);
	});

	It("rejects a binding without an identifier, or without an object", [this]()
	{
		AddExpectedError(TEXT("without an identifier"));
		AddExpectedError(TEXT("cannot bind nothing"));
		Bindings->BindFunctions(FString(), NewObject<UTarinoiTestFunctions>());
		Bindings->BindVariables(TEXT("vars"), nullptr);
		TestEqual("nothing bound", Bindings->GetBoundFunctionCollections().Num() + Bindings->GetBoundVariableCollections().Num(), 0);
	});

	It("keeps bound objects alive through garbage collection", [this]()
	{
		Bindings->BindFunctions(TEXT("global"), NewObject<UTarinoiTestFunctions>());
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TestTrue("still valid", IsValid(Bindings->GetFunctions(TEXT("global"))));
	});

	It("clears everything", [this]()
	{
		Bindings->BindFunctions(TEXT("f"), NewObject<UTarinoiTestFunctions>());
		Bindings->BindVariables(TEXT("v"), NewObject<UTarinoiTestVariables>());
		Bindings->BindEntities(TEXT("e"), NewObject<UTarinoiTestEntities>());
		Bindings->Clear();
		TestNull("f", Bindings->GetFunctions(TEXT("f")));
		TestNull("v", Bindings->GetVariables(TEXT("v")));
		TestNull("e", Bindings->GetEntities(TEXT("e")));
	});

	Describe("reflection defaults", [this]()
	{
		It("finds declared functions, and none of the UObject plumbing", [this]()
		{
			UTarinoiTestFunctions* Functions = NewObject<UTarinoiTestFunctions>();
			TestTrue("declared", Functions->HasFunction(TEXT("ReturnTrue")));
			TestFalse("unknown", Functions->HasFunction(TEXT("Nope")));
			TestFalse("plumbing", Functions->HasFunction(TEXT("ExecuteUbergraph")));
		});

		It("converts arguments to typed parameters and the result back", [this]()
		{
			UTarinoiTestFunctions* Functions = NewObject<UTarinoiTestFunctions>();
			FTarinoiValue Result;
			TestTrue("invoked", Functions->TryInvoke(TEXT("Add"), {FTarinoiValue::MakeNumber(1.5), FTarinoiValue::MakeString(TEXT("2"))}, Result));
			TestEqual("sum", Result.ToNumber(), 3.5);

			Functions->TryInvoke(TEXT("Greet"), {FTarinoiValue::MakeString(TEXT("ada")), FTarinoiValue::MakeBool(true)}, Result);
			TestEqualSensitive("greeting", Result.ToString(), FString(TEXT("ADA")));
		});

		It("reports an unknown function as not invoked", [this]()
		{
			FTarinoiValue Result;
			TestFalse("unknown", NewObject<UTarinoiTestFunctions>()->TryInvoke(TEXT("Nope"), {}, Result));
		});

		It("reports a wrong argument count actionably, without calling", [this]()
		{
			UTarinoiTestFunctions* Functions = NewObject<UTarinoiTestFunctions>();
			AddExpectedError(TEXT("takes 2 argument\\(s\\) but the authored call passed 1"));
			FTarinoiValue Result;
			Functions->TryInvoke(TEXT("RecordArgs"), {FTarinoiValue::MakeNumber(1)}, Result);
			TestEqual("not called", Functions->Calls.Num(), 0);
		});

		It("reads and writes variables by authored name, including through PascalCase", [this]()
		{
			UTarinoiTestVariables* Variables = NewObject<UTarinoiTestVariables>();
			Variables->SetVariable(TEXT("Met"), FTarinoiValue::MakeBool(true));
			Variables->SetVariable(TEXT("gold"), FTarinoiValue::MakeString(TEXT("12")));
			Variables->SetVariable(TEXT("player_name"), FTarinoiValue::MakeString(TEXT("Ada")));

			TestTrue("bool", Variables->Met);
			TestEqual("number from string", Variables->Gold, 12.0);
			TestEqualSensitive("snake to Pascal", Variables->PlayerName, FString(TEXT("Ada")));
			TestTrue("read back", Variables->GetVariable(TEXT("met")).ToBool());
		});

		It("warns on an unknown variable", [this]()
		{
			AddExpectedMessage(TEXT("no readable variable 'missing'"), ELogVerbosity::Warning);
			TestTrue("none", NewObject<UTarinoiTestVariables>()->GetVariable(TEXT("missing")).IsNone());
		});
	});
}

#endif
