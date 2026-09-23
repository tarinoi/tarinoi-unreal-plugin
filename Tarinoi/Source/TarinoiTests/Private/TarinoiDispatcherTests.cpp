// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bindings/TarinoiDispatcher.h"
#include "TarinoiJson.h"
#include "TarinoiTestBindings.h"
#include "TarinoiTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

namespace TarinoiDispatcherTests
{
	TArray<TSharedPtr<FJsonObject>> Options(const TCHAR* Json)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TSharedPtr<FJsonObject> Wrapper = TarinoiJson::Parse(FString::Printf(TEXT("{\"o\":%s}"), Json));
		for (const TSharedPtr<FJsonValue>& Value : *TarinoiJson::Arr(Wrapper, TEXT("o")))
		{
			Out.Add(Value->AsObject());
		}
		return Out;
	}
}

BEGIN_DEFINE_SPEC(FTarinoiDispatcherSpec, "Tarinoi.Bindings.Dispatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTarinoiBindings> Bindings;
	TStrongObjectPtr<UTarinoiTestFunctions> Functions;
	TStrongObjectPtr<UTarinoiTestVariables> Variables;
	TUniquePtr<FTarinoiDispatcher> Dispatcher;
END_DEFINE_SPEC(FTarinoiDispatcherSpec)

void FTarinoiDispatcherSpec::Define()
{
	using namespace TarinoiDispatcherTests;

	BeforeEach([this]()
	{
		Bindings.Reset(NewObject<UTarinoiBindings>());
		Functions.Reset(NewObject<UTarinoiTestFunctions>());
		Variables.Reset(NewObject<UTarinoiTestVariables>());
		Bindings->BindFunctions(TEXT("global"), Functions.Get());
		Bindings->BindVariables(TEXT("state"), Variables.Get());
		Bindings->BindEntities(TEXT("cast"), NewObject<UTarinoiTestEntities>());
		Dispatcher = MakeUnique<FTarinoiDispatcher>(Bindings.Get());
	});

	AfterEach([this]()
	{
		Dispatcher.Reset();
		Functions.Reset();
		Variables.Reset();
		Bindings.Reset();
	});

	Describe("conditions", [this]()
	{
		It("passes an empty condition", [this]()
		{
			TestTrue("empty", Dispatcher->EvalCondition(FString()));
		});

		It("does boolean algebra", [this]()
		{
			struct FCase { const TCHAR* Expr; bool bExpected; };
			const FCase Cases[] = {
				{TEXT("true"), true}, {TEXT("false"), false}, {TEXT("!true"), false}, {TEXT("!false"), true},
				{TEXT("true && true"), true}, {TEXT("true && false"), false}, {TEXT("false || true"), true},
				{TEXT("false || false"), false}, {TEXT("true || false && false"), true}, {TEXT("(true || false) && false"), false},
			};
			for (const FCase& Case : Cases)
			{
				TestEqual(Case.Expr, Dispatcher->EvalCondition(Case.Expr), Case.bExpected);
			}
		});

		It("short-circuits && and ||, so side effects on the right do not run", [this]()
		{
			Dispatcher->EvalCondition(TEXT("Fn.global.ReturnFalse() && Fn.global.ReturnTrue()"));
			Dispatcher->EvalCondition(TEXT("Fn.global.ReturnTrue() || Fn.global.ReturnFalse()"));
			TarinoiTest::Strings(*this, TEXT("calls"), Functions->Calls, {TEXT("ReturnFalse"), TEXT("ReturnTrue")});
		});

		It("passes an unparseable condition, reporting it once", [this]()
		{
			AddExpectedError(TEXT("Expression:"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue("first", Dispatcher->EvalCondition(TEXT("Fn.global.(")));
			TestTrue("again", Dispatcher->EvalCondition(TEXT("Fn.global.(")));
		});

		It("reads a bare variable used as a condition", [this]()
		{
			// Testing the reference itself would always be true, making the condition useless.
			TestFalse("unset flag", Dispatcher->EvalCondition(TEXT("Var.state.Met")));
			Variables->Met = true;
			TestTrue("set flag", Dispatcher->EvalCondition(TEXT("Var.state.Met")));
			TestFalse("negated", Dispatcher->EvalCondition(TEXT("!Var.state.Met")));
		});

		It("keeps expressions that differ only in case apart in its cache", [this]()
		{
			AddExpectedError(TEXT("no bindings registered for function collection 'Global'"));
			TestTrue("lower", Dispatcher->EvalCondition(TEXT("Fn.global.ReturnTrue()")));
			TestFalse("upper is a different collection", Dispatcher->EvalCondition(TEXT("Fn.Global.ReturnTrue()")));
		});
	});

	Describe("arguments", [this]()
	{
		It("passes literals with their types", [this]()
		{
			Dispatcher->EvalCondition(TEXT("Fn.global.RecordArgs(42, \"text\")"));
			TestTrue("number", Functions->LastArgs[0].Type == ETarinoiValueType::Number && Functions->LastArgs[0].NumberValue == 42.0);
			TestTrue("string", Functions->LastArgs[1].Type == ETarinoiValueType::String && Functions->LastArgs[1].StringValue == TEXT("text"));

			Dispatcher->EvalCondition(TEXT("Fn.global.RecordArgs(1.5, true)"));
			TestEqual("float", Functions->LastArgs[0].NumberValue, 1.5);
			TestTrue("bool", Functions->LastArgs[1].Type == ETarinoiValueType::Bool && Functions->LastArgs[1].BoolValue);
		});

		It("passes a nested call's result", [this]()
		{
			Dispatcher->EvalCondition(TEXT("Fn.global.RecordArgs(Fn.global.ReturnString(), 1)"));
			TestEqualSensitive("nested", Functions->LastArgs[0].ToString(), FString(TEXT("pin_a")));
		});

		It("passes a variable as an unresolved reference that can be read and written", [this]()
		{
			Dispatcher->EvalCondition(TEXT("Fn.global.RecordArgs(Var.state.Met, 0)"));
			TestTrue("reference", Functions->LastArgs[0].IsVariable());

			Dispatcher->EvalCondition(TEXT("Fn.global.SetTrue(Var.state.Met)"));
			TestTrue("written", Variables->Met);
			TestTrue("read through", Dispatcher->EvalCondition(TEXT("Fn.global.Read(Var.state.Met)")));
		});

		It("resolves an entity through its binding", [this]()
		{
			const FTarinoiValue Hero = Dispatcher->EvalValue(TEXT("Ent.cast.hero"));
			TestTrue("object", Hero.Type == ETarinoiValueType::Object && Hero.ObjectValue != nullptr);
			TestTrue("unknown entity", Dispatcher->EvalValue(TEXT("Ent.cast.nobody")).IsNone());
		});
	});

	Describe("lists", [this]()
	{
		BeforeEach([this]()
		{
			FTarinoiLists Lists;
			Lists.Add(TEXT("global/thresholds"), Options(TEXT("[{\"key\":\"easy\",\"option_value\":5},{\"key\":\"hard\",\"value\":15},{\"key\":\"both\",\"option_value\":1,\"value\":2}]")));
			Dispatcher->SetLists(Lists);
		});

		It("resolves an option by key", [this]()
		{
			TestEqual("option_value", Dispatcher->EvalValue(TEXT("Ls.global.thresholds.easy")).ToNumber(), 5.0);
		});

		It("falls back to the older value field, and prefers option_value", [this]()
		{
			TestEqual("value", Dispatcher->EvalValue(TEXT("Ls.global.thresholds.hard")).ToNumber(), 15.0);
			TestEqual("option_value wins", Dispatcher->EvalValue(TEXT("Ls.global.thresholds.both")).ToNumber(), 1.0);
		});

		It("warns and resolves to nothing for an unknown list or key", [this]()
		{
			AddExpectedMessage(TEXT("no list 'global.missing'"), ELogVerbosity::Warning);
			AddExpectedMessage(TEXT("has no option 'medium'"), ELogVerbosity::Warning);
			TestTrue("list", Dispatcher->EvalValue(TEXT("Ls.global.missing.x")).IsNone());
			TestTrue("key", Dispatcher->EvalValue(TEXT("Ls.global.thresholds.medium")).IsNone());
		});
	});

	Describe("Card.*", [this]()
	{
		It("resolves to the context card, or an empty object", [this]()
		{
			TestTrue("empty by default", Dispatcher->EvalValue(TEXT("Card.data")).Type == ETarinoiValueType::Json);
			TSharedPtr<FJsonObject> Card = MakeShared<FJsonObject>();
			Card->SetStringField(TEXT("id"), TEXT("c1"));
			Dispatcher->SetContextCard(Card);
			TestTrue("context", Dispatcher->EvalValue(TEXT("Card.data")).JsonValue == Card);
		});
	});

	Describe("failures", [this]()
	{
		It("logs an unbound function collection and evaluates false", [this]()
		{
			AddExpectedError(TEXT("no bindings registered for function collection 'combat'"));
			TestFalse("false", Dispatcher->EvalCondition(TEXT("Fn.combat.Roll()")));
		});

		It("logs a missing function and evaluates false", [this]()
		{
			AddExpectedError(TEXT("'global.Nope' is not implemented"));
			TestFalse("false", Dispatcher->EvalCondition(TEXT("Fn.global.Nope()")));
		});

		It("logs unbound variable and entity collections", [this]()
		{
			AddExpectedError(TEXT("variable collection 'other'"));
			AddExpectedError(TEXT("entity collection 'other'"));
			TestTrue("variable", Dispatcher->EvalValue(TEXT("Var.other.x")).IsNone());
			TestTrue("entity", Dispatcher->EvalValue(TEXT("Ent.other.x")).IsNone());
		});

		It("reports a wrong argument count actionably", [this]()
		{
			AddExpectedError(TEXT("takes 2 argument"));
			Dispatcher->EvalCondition(TEXT("Fn.global.RecordArgs(1)"));
		});
	});

	Describe("EvalCall, EvalValue and HasCall", [this]()
	{
		It("returns a call's result", [this]()
		{
			TestEqualSensitive("result", Dispatcher->EvalCall(TEXT("Fn.global.ReturnString()")).ToString(), FString(TEXT("pin_a")));
			TestTrue("empty", Dispatcher->EvalCall(FString()).IsNone());
		});

		It("returns raw values without coercing to bool", [this]()
		{
			TestEqual("number", Dispatcher->EvalValue(TEXT("7")).NumberValue, 7.0);
		});

		It("tells bound from unbound without calling", [this]()
		{
			TestTrue("bound", Dispatcher->HasCall(TEXT("Fn.global.ReturnTrue()")));
			TestFalse("missing function", Dispatcher->HasCall(TEXT("Fn.global.Nope()")));
			TestFalse("missing collection", Dispatcher->HasCall(TEXT("Fn.combat.Roll()")));
			TestEqual("nothing called", Functions->Calls.Num(), 0);
		});

		It("says a malformed call is not callable", [this]()
		{
			AddExpectedError(TEXT("Expression:"));
			TestFalse("malformed", Dispatcher->HasCall(TEXT("Fn.global.(")));
		});

		It("evaluates the same expression the same way repeatedly", [this]()
		{
			for (int32 I = 0; I < 3; ++I)
			{
				TestTrue("stable", Dispatcher->EvalCondition(TEXT("Fn.global.ReturnTrue() && !false")));
			}
		});

		It("sees bindings registered after it was created", [this]()
		{
			AddExpectedError(TEXT("function collection 'late'"));
			TestFalse("before", Dispatcher->EvalCondition(TEXT("Fn.late.ReturnTrue()")));
			Bindings->BindFunctions(TEXT("late"), NewObject<UTarinoiTestFunctions>());
			TestTrue("after", Dispatcher->EvalCondition(TEXT("Fn.late.ReturnTrue()")));
		});
	});
}

#endif
