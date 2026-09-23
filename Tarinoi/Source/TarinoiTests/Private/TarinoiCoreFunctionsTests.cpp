// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

// The scaffolded core functions, exercised for real: the fixture scaffold in Fixtures/ is what the
// emitter renders, compiled into this module. Its semantics must match in-app playback, including
// the defaults for unset variables (false, 0, "").

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bindings/TarinoiDispatcher.h"
#include "Codegen/TarinoiBindingValidator.h"
#include "Fixtures/TarinoiFixtureCoreFunctions.h"
#include "Fixtures/TarinoiFixtureGeneratedVariables.h"
#include "TarinoiCodegenFixture.h"
#include "TarinoiTestBindings.h"
#include "TarinoiTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

BEGIN_DEFINE_SPEC(FTarinoiCoreFunctionsSpec, "Tarinoi.Codegen.CoreFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTarinoiFixtureCoreFunctions> Core;
	TStrongObjectPtr<UTarinoiMapVariables> Vars;

	FTarinoiValue Ref(const TCHAR* Name) const { return FTarinoiValue::MakeVariable(Vars.Get(), TEXT("state"), Name); }
	FTarinoiValue Num(double Value) const { return FTarinoiValue::MakeNumber(Value); }
	FTarinoiValue Str(const TCHAR* Value) const { return FTarinoiValue::MakeString(Value); }
	FTarinoiValue Stored(const TCHAR* Name) const { return Vars->Values.FindRef(Name); }
END_DEFINE_SPEC(FTarinoiCoreFunctionsSpec)

void FTarinoiCoreFunctionsSpec::Define()
{
	BeforeEach([this]()
	{
		Core.Reset(NewObject<UTarinoiFixtureCoreFunctions>());
		Vars.Reset(NewObject<UTarinoiMapVariables>());
	});

	AfterEach([this]()
	{
		Core.Reset();
		Vars.Reset();
	});

	Describe("flags", [this]()
	{
		It("reads an unset flag as clear", [this]()
		{
			TestFalse("unset", Core->FlagIsSet(Ref(TEXT("met"))));
		});

		It("sets and clears a flag", [this]()
		{
			Core->SetFlag(Ref(TEXT("met")));
			TestTrue("stored true", Stored(TEXT("met")).Type == ETarinoiValueType::Bool && Stored(TEXT("met")).BoolValue);
			TestTrue("set", Core->FlagIsSet(Ref(TEXT("met"))));
			Core->ClearFlag(Ref(TEXT("met")));
			TestFalse("cleared", Core->FlagIsSet(Ref(TEXT("met"))));
		});

		It("toggles an unset flag on, then off", [this]()
		{
			Core->ToggleFlag(Ref(TEXT("met")));
			TestTrue("on", Stored(TEXT("met")).BoolValue);
			Core->ToggleFlag(Ref(TEXT("met")));
			TestFalse("off", Stored(TEXT("met")).BoolValue);
		});

		It("logs a mutator given no reference, and does nothing", [this]()
		{
			AddExpectedError(TEXT("expected a Var\\.\\* reference"));
			Core->SetFlag(FTarinoiValue::MakeBool(true));
			TestEqual("nothing stored", Vars->Values.Num(), 0);
		});
	});

	Describe("counters and text", [this]()
	{
		It("increments from zero when unset, and by a negative delta", [this]()
		{
			Core->IncrementCounter(Ref(TEXT("morale")), Num(1));
			TestEqual("one", Stored(TEXT("morale")).NumberValue, 1.0);
			Core->IncrementCounter(Ref(TEXT("morale")), Num(-3));
			TestEqual("minus two", Stored(TEXT("morale")).NumberValue, -2.0);
		});

		It("sets a counter from a literal or a variable", [this]()
		{
			Core->SetCounter(Ref(TEXT("morale")), Num(5));
			TestEqual("literal", Stored(TEXT("morale")).NumberValue, 5.0);
			Vars->Values.Add(TEXT("starting"), Num(7));
			Core->SetCounter(Ref(TEXT("morale")), Ref(TEXT("starting")));
			TestEqual("variable", Stored(TEXT("morale")).NumberValue, 7.0);
		});

		It("sets text from a literal or a variable", [this]()
		{
			Core->SetText(Ref(TEXT("title")), Str(TEXT("Ferry Captain")));
			TestEqualSensitive("literal", Stored(TEXT("title")).StringValue, FString(TEXT("Ferry Captain")));
			Vars->Values.Add(TEXT("rank"), Str(TEXT("Admiral")));
			Core->SetText(Ref(TEXT("title")), Ref(TEXT("rank")));
			TestEqualSensitive("variable", Stored(TEXT("title")).StringValue, FString(TEXT("Admiral")));
		});

		It("logs a value of the wrong type and uses the default", [this]()
		{
			AddExpectedError(TEXT("expected a number"));
			AddExpectedError(TEXT("expected a string"));
			AddExpectedError(TEXT("expected a boolean"));
			Core->SetCounter(Ref(TEXT("morale")), Str(TEXT("lots")));
			TestEqual("zero", Stored(TEXT("morale")).NumberValue, 0.0);
			Core->SetText(Ref(TEXT("title")), Num(42));
			TestEqualSensitive("empty", Stored(TEXT("title")).StringValue, FString());
			Vars->Values.Add(TEXT("flag"), Str(TEXT("yes")));
			TestFalse("not a flag", Core->FlagIsSet(Ref(TEXT("flag"))));
		});
	});

	Describe("comparisons", [this]()
	{
		It("compares an unset counter as zero and unset text as empty", [this]()
		{
			TestTrue("equals 0", Core->NumberEquals(Ref(TEXT("nothing")), Num(0)));
			TestTrue("at most 0", Core->NumberAtMost(Ref(TEXT("nothing")), Num(0)));
			TestFalse("greater than 0", Core->NumberGreaterThan(Ref(TEXT("nothing")), Num(0)));
			TestTrue("empty text", Core->StringEquals(Ref(TEXT("nothing")), Str(TEXT(""))));
			TestFalse("not x", Core->StringEquals(Ref(TEXT("nothing")), Str(TEXT("x"))));
		});

		It("compares numbers over any mix of sources", [this]()
		{
			Vars->Values.Add(TEXT("strength"), Num(12));
			Vars->Values.Add(TEXT("hard"), Num(12));
			TestTrue("at least var", Core->NumberAtLeast(Ref(TEXT("strength")), Ref(TEXT("hard"))));
			TestTrue("at least literal", Core->NumberAtLeast(Ref(TEXT("strength")), Num(12)));
			TestFalse("greater", Core->NumberGreaterThan(Ref(TEXT("strength")), Num(12)));
			TestTrue("literal greater", Core->NumberGreaterThan(Num(13), Ref(TEXT("strength"))));
			TestTrue("at most", Core->NumberAtMost(Ref(TEXT("strength")), Num(12)));
			TestTrue("less", Core->NumberLessThan(Num(11.5), Ref(TEXT("strength"))));
			TestFalse("not less", Core->NumberLessThan(Ref(TEXT("strength")), Ref(TEXT("hard"))));
		});

		It("compares strings over any mix of sources, case-sensitively", [this]()
		{
			Vars->Values.Add(TEXT("faction"), Str(TEXT("rebels")));
			TestTrue("var = literal", Core->StringEquals(Ref(TEXT("faction")), Str(TEXT("rebels"))));
			TestTrue("literal = var", Core->StringEquals(Str(TEXT("rebels")), Ref(TEXT("faction"))));
			TestFalse("different", Core->StringEquals(Ref(TEXT("faction")), Str(TEXT("empire"))));
			TestFalse("case matters", Core->StringEquals(Ref(TEXT("faction")), Str(TEXT("Rebels"))));
		});

		It("logs mismatched operand types rather than coercing", [this]()
		{
			// Playback rejects these: "1" is not 1.
			AddExpectedError(TEXT("expected a number"));
			TestFalse("string vs number", Core->NumberEquals(Str(TEXT("1")), Num(1)));
		});
	});

	Describe("dispatch", [this]()
	{
		It("is reached through the generated switch", [this]()
		{
			FTarinoiValue Result;
			TestTrue("set", Core->TryInvoke(TEXT("SetFlag"), {Ref(TEXT("met"))}, Result));
			TestTrue("read", Core->TryInvoke(TEXT("FlagIsSet"), {Ref(TEXT("met"))}, Result));
			TestTrue("true", Result.Type == ETarinoiValueType::Bool && Result.BoolValue);
			TestFalse("unknown", Core->TryInvoke(TEXT("Nope"), {}, Result));
		});

		It("works end to end from an authored expression, with generated variables", [this]()
		{
			TStrongObjectPtr<UTarinoiBindings> Bindings(NewObject<UTarinoiBindings>());
			UTarinoiFixtureStateVariables* State = NewObject<UTarinoiFixtureStateVariables>();
			Bindings->BindFunctions(TEXT("tarinoi"), Core.Get());
			Bindings->BindVariables(TEXT("state"), State);
			FTarinoiDispatcher Dispatcher(Bindings.Get());

			State->MetFerryman = false;
			Dispatcher.EvalCall(TEXT("Fn.tarinoi.SetFlag(Var.state.met_ferryman)"));
			TestTrue("written through", State->MetFerryman);
			Dispatcher.EvalCall(TEXT("Fn.tarinoi.IncrementCounter(Var.state.gold, 2.5)"));
			TestEqual("gold", State->Gold, 15.0);
			TestTrue("condition", Dispatcher.EvalCondition(TEXT("Fn.tarinoi.NumberAtLeast(Var.state.gold, 15) && Fn.tarinoi.FlagIsSet(Var.state.met_ferryman)")));
		});
	});

	Describe("default binding", [this]()
	{
		It("binds the generated variables and the scaffold when nothing else is bound", [this]()
		{
			TStrongObjectPtr<UTarinoiBindings> Bindings(NewObject<UTarinoiBindings>());
			const TArray<FString> Bound = Bindings->BindGeneratedDefaults(TEXT("TarinoiFixture"));
			TarinoiTest::Strings(*this, TEXT("bound"), Bound,
				{TEXT("Var.state -> TarinoiFixtureStateVariables"), TEXT("Fn.tarinoi -> TarinoiFixtureCoreFunctions")});
			TestTrue("variables", Bindings->GetVariables(TEXT("state"))->IsA<UTarinoiFixtureStateVariables>());
			TestTrue("scaffold", Bindings->GetFunctions(TEXT("tarinoi"))->IsA<UTarinoiFixtureCoreFunctions>());
		});

		It("leaves explicit bindings alone", [this]()
		{
			TStrongObjectPtr<UTarinoiBindings> Bindings(NewObject<UTarinoiBindings>());
			UTarinoiMapVariables* Mine = NewObject<UTarinoiMapVariables>();
			Bindings->BindVariables(TEXT("state"), Mine);
			Bindings->BindFunctions(TEXT("tarinoi"), Core.Get());
			TestEqual("nothing bound", Bindings->BindGeneratedDefaults(TEXT("TarinoiFixture")).Num(), 0);
			TestTrue("mine kept", Bindings->GetVariables(TEXT("state")) == Mine);
		});

		It("does not pick up the fixture under the default prefix", [this]()
		{
			TStrongObjectPtr<UTarinoiBindings> Bindings(NewObject<UTarinoiBindings>());
			for (const FString& Entry : Bindings->BindGeneratedDefaults())
			{
				TestFalse(*Entry, Entry.Contains(TEXT("Fixture")));
			}
		});
	});
}

BEGIN_DEFINE_SPEC(FTarinoiBindingValidatorSpec, "Tarinoi.Codegen.Validator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TArray<FTarinoiBindingIssue> Validate(const FTarinoiCodegenModel& Model) const
	{
		const FString Header = FPaths::Combine(TarinoiCodegenFixture::Directory(), TEXT("TarinoiFixtureCoreFunctions.h"));
		return TarinoiBindingValidator::Validate(Model, Header, TEXT("TarinoiFixture"));
	}

	FString Describe(const TArray<FTarinoiBindingIssue>& Issues) const
	{
		TArray<FString> Lines;
		for (const FTarinoiBindingIssue& Issue : Issues)
		{
			Lines.Add((Issue.bBreaking ? TEXT("[breaking] ") : TEXT("[addition] ")) + Issue.Message);
		}
		return FString::Join(Lines, TEXT("\n"));
	}
END_DEFINE_SPEC(FTarinoiBindingValidatorSpec)

void FTarinoiBindingValidatorSpec::Define()
{
	It("finds nothing when the compiled bindings match the content", [this]()
	{
		const TArray<FTarinoiBindingIssue> Issues = Validate(TarinoiCodegenFixture::Model());
		TestEqual(Describe(Issues), Issues.Num(), 0);
	});

	It("reports a new function or collection as an addition", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		Model.Functions[TEXT("global")].Add(TarinoiCodegenFixture::Fn(TEXT("Brand_New"), {}, TEXT("void"), TEXT("")));
		Model.Functions.Add(TEXT("combat"), {TarinoiCodegenFixture::Fn(TEXT("Roll"), {}, TEXT("number"), TEXT(""))});
		const TArray<FTarinoiBindingIssue> Issues = Validate(Model);
		TestEqual(Describe(Issues), Issues.Num(), 2);
		for (const FTarinoiBindingIssue& Issue : Issues)
		{
			TestFalse(Issue.Message, Issue.bBreaking);
		}
	});

	It("reports a changed argument count or return type as breaking", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		for (FTarinoiFunctionDecl& Fn : Model.Functions[TEXT("global")])
		{
			if (Fn.Name == TEXT("Roll")) { Fn.Args.Pop(); }
			if (Fn.Name == TEXT("CheckGate")) { Fn.Returns = TEXT("string"); }
		}
		const TArray<FTarinoiBindingIssue> Issues = Validate(Model);
		TestEqual(Describe(Issues), Issues.Num(), 2);
		TestTrue("arity", Describe(Issues).Contains(TEXT("[breaking] UTarinoiFixtureGlobalFunctions::Roll now takes 1 argument(s)")));
		TestTrue("return", Describe(Issues).Contains(TEXT("[breaking] UTarinoiFixtureGlobalFunctions::CheckGate now returns FString")));
	});

	It("reports a removed function or variable as breaking", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		Model.Functions[TEXT("global")].RemoveAll([](const FTarinoiFunctionDecl& Fn) { return Fn.Name == TEXT("PickPin"); });
		Model.Variables[TEXT("state")].RemoveAll([](const FTarinoiVariableDecl& Var) { return Var.Name == TEXT("mood"); });
		const TArray<FTarinoiBindingIssue> Issues = Validate(Model);
		TestTrue("function", Describe(Issues).Contains(TEXT("[breaking] UTarinoiFixtureGlobalFunctions::PickPin no longer exists")));
		TestTrue("variable", Describe(Issues).Contains(TEXT("[breaking] UTarinoiFixtureStateVariables::Mood no longer exists")));
	});

	It("reports a changed variable type as breaking", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		for (FTarinoiVariableDecl& Var : Model.Variables[TEXT("state")])
		{
			if (Var.Name == TEXT("gold")) { Var.DataType = TEXT("string"); }
		}
		TestTrue("type", Describe(Validate(Model)).Contains(TEXT("[breaking] UTarinoiFixtureStateVariables::Gold is now FString")));
	});

	It("notes a core function the scaffold does not implement, without calling it breaking", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		Model.Functions[TEXT("tarinoi")].Add(TarinoiCodegenFixture::Fn(TEXT("NewCoreThing"), {}, TEXT("void"), TEXT("")));
		const FString Issues = Describe(Validate(Model));
		TestTrue("not implemented", Issues.Contains(TEXT("[addition] UTarinoiFixtureCoreFunctions does not implement NewCoreThing")));
	});

	It("notes a scaffold left behind when the core collection is gone", [this]()
	{
		FTarinoiCodegenModel Model = TarinoiCodegenFixture::Model();
		Model.Functions.Remove(TEXT("tarinoi"));
		TestTrue("orphan", Describe(Validate(Model)).Contains(TEXT("but the project has no 'tarinoi' collection")));
	});
}

#endif
