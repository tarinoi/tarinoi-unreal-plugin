// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Expressions/TarinoiExpression.h"

namespace TarinoiExpressionParserTests
{
	FString Parse(const TCHAR* Expr)
	{
		const FTarinoiExprPtr Node = TarinoiExpressionParser::ParseCondition(Expr);
		return Node.IsValid() ? Node->ToString() : FString(TEXT("<null>"));
	}
}

BEGIN_DEFINE_SPEC(FTarinoiExpressionParserSpec, "Tarinoi.Expressions.Parser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTarinoiExpressionParserSpec)

void FTarinoiExpressionParserSpec::Define()
{
	using namespace TarinoiExpressionParserTests;

	It("parses empty conditions to nothing", [this]()
	{
		for (const TCHAR* Expr : {TEXT(""), TEXT("   "), TEXT("\t\n")})
		{
			TestFalse(Expr, TarinoiExpressionParser::ParseCondition(Expr).IsValid());
		}
	});

	It("parses literals and boolean operators", [this]()
	{
		TestEqualSensitive("true", Parse(TEXT("true")), FString(TEXT("true")));
		TestEqualSensitive("not", Parse(TEXT("!false")), FString(TEXT("!false")));
		TestEqualSensitive("double not", Parse(TEXT("!!true")), FString(TEXT("!!true")));
		TestEqualSensitive("and", Parse(TEXT("true && false")), FString(TEXT("(true && false)")));
		TestEqualSensitive("or", Parse(TEXT("true || false")), FString(TEXT("(true || false)")));
	});

	It("binds && tighter than ||, and ! tighter than &&", [this]()
	{
		TestEqualSensitive("and over or", Parse(TEXT("true || false && false")), FString(TEXT("(true || (false && false))")));
		TestEqualSensitive("not over and", Parse(TEXT("!true && false")), FString(TEXT("(!true && false)")));
		TestEqualSensitive("parentheses", Parse(TEXT("(true || false) && false")), FString(TEXT("((true || false) && false)")));
		TestEqualSensitive("left associative", Parse(TEXT("true && false && true")), FString(TEXT("((true && false) && true)")));
	});

	It("parses calls, with and without arguments", [this]()
	{
		TestEqualSensitive("none", Parse(TEXT("Fn.global.Check()")), FString(TEXT("Fn.global.Check()")));
		TestEqualSensitive("literals", Parse(TEXT("Fn.global.F(1, 2.5, \"text\", true)")), FString(TEXT("Fn.global.F(1, 2.5, \"text\", true)")));
		TestEqualSensitive("nested", Parse(TEXT("Fn.a.Outer(Fn.b.Inner(1))")), FString(TEXT("Fn.a.Outer(Fn.b.Inner(1))")));
		TestEqualSensitive("references as arguments", Parse(TEXT("Fn.a.F(Var.b.c, Ent.d.e, Ls.f.g.h, Card.line)")),
			FString(TEXT("Fn.a.F(Var.b.c, Ent.d.e, Ls.f.g.h, Card.line)")));
		TestEqualSensitive("with operators", Parse(TEXT("!Fn.a.F() || Fn.b.G()")), FString(TEXT("(!Fn.a.F() || Fn.b.G())")));
	});

	It("parses each kind of reference", [this]()
	{
		const FTarinoiExprPtr Var = TarinoiExpressionParser::ParseCondition(TEXT("Var.ferry.met_ferryman"));
		TestTrue("variable", Var->Kind == ETarinoiExprKind::Ref && Var->RefKind == ETarinoiRefKind::Variable);
		TestEqualSensitive("collection", Var->Collection, FString(TEXT("ferry")));
		TestEqualSensitive("name", Var->Name, FString(TEXT("met_ferryman")));

		const FTarinoiExprPtr Ent = TarinoiExpressionParser::ParseCondition(TEXT("Ent.cast.hero"));
		TestTrue("entity", Ent->RefKind == ETarinoiRefKind::Entity);

		const FTarinoiExprPtr List = TarinoiExpressionParser::ParseCondition(TEXT("Ls.global.moods.happy"));
		TestTrue("list", List->RefKind == ETarinoiRefKind::List);
		TestEqualSensitive("key", List->Key, FString(TEXT("happy")));

		const FTarinoiExprPtr Card = TarinoiExpressionParser::ParseCondition(TEXT("Card.line"));
		TestTrue("card", Card->Kind == ETarinoiExprKind::CardRef);
		TestEqualSensitive("member", Card->Text, FString(TEXT("line")));
	});

	It("accepts a numeric list key", [this]()
	{
		// The '.' before a digit is member access here, not a decimal point.
		const FTarinoiExprPtr List = TarinoiExpressionParser::ParseCondition(TEXT("Ls.global.thresholds.2"));
		if (TestTrue("parsed", List.IsValid()))
		{
			TestEqualSensitive("key", List->Key, FString(TEXT("2")));
		}
	});

	It("tells floats from member access", [this]()
	{
		TestTrue("float", TarinoiExpressionParser::ParseCondition(TEXT("Fn.g.F(1.5)"))->Children[0]->Kind == ETarinoiExprKind::Float);
		TestTrue("int", TarinoiExpressionParser::ParseCondition(TEXT("Fn.g.F(1)"))->Children[0]->Kind == ETarinoiExprKind::Int);
	});

	It("parses a bare literal as a whole expression", [this]()
	{
		// A threshold authored as a plain number is a top-level expression. Parsing it to
		// nothing made every skill check pass against 0 in an earlier port.
		const FTarinoiExprPtr Int = TarinoiExpressionParser::ParseCondition(TEXT("10"));
		TestTrue("int", Int.IsValid() && Int->Kind == ETarinoiExprKind::Int && Int->IntValue == 10);
		const FTarinoiExprPtr Float = TarinoiExpressionParser::ParseCondition(TEXT("3.5"));
		TestTrue("float", Float.IsValid() && Float->Kind == ETarinoiExprKind::Float && Float->FloatValue == 3.5);
		const FTarinoiExprPtr String = TarinoiExpressionParser::ParseCondition(TEXT("\"hi\""));
		TestTrue("string", String.IsValid() && String->Kind == ETarinoiExprKind::String && String->Text == TEXT("hi"));
	});

	It("ignores whitespace and allows underscores and digits in identifiers", [this]()
	{
		TestEqualSensitive("spaced", Parse(TEXT("  Fn . a_1 . F_2 ( 1 ,2 )  ")), FString(TEXT("Fn.a_1.F_2(1, 2)")));
	});

	It("logs a malformed expression once and returns nothing", [this]()
	{
		struct FCase { const TCHAR* Expr; const TCHAR* Why; };
		const FCase Cases[] = {
			{TEXT("Fn.global.Check("), TEXT("unclosed argument list")},
			{TEXT("true &&"), TEXT("dangling operator")},
			{TEXT("Bogus.thing.here"), TEXT("unknown prefix")},
			{TEXT("Fn.global.Check() extra"), TEXT("trailing tokens")},
			{TEXT("@invalid"), TEXT("illegal character")},
			{TEXT("\"unterminated"), TEXT("unterminated string")},
			{TEXT("()"), TEXT("empty parentheses")},
			{TEXT("Fn.a.B(,,,"), TEXT("only the first error is reported")},
		};

		AddExpectedError(TEXT("Expression:"), EAutomationExpectedErrorFlags::Contains, UE_ARRAY_COUNT(Cases));
		for (const FCase& Case : Cases)
		{
			TestFalse(Case.Why, TarinoiExpressionParser::ParseCondition(Case.Expr).IsValid());
		}
	});

	Describe("ParseCall", [this]()
	{
		It("accepts a call", [this]()
		{
			const FTarinoiExprPtr Call = TarinoiExpressionParser::ParseCall(TEXT("Fn.global.Pick(1)"));
			TestTrue("call", Call.IsValid() && Call->Kind == ETarinoiExprKind::Call && Call->Name == TEXT("Pick"));
		});

		It("returns nothing for empty input", [this]()
		{
			TestFalse("empty", TarinoiExpressionParser::ParseCall(TEXT("   ")).IsValid());
		});

		It("rejects something that is not a call", [this]()
		{
			AddExpectedError(TEXT("expected a single function call"));
			TestFalse("variable", TarinoiExpressionParser::ParseCall(TEXT("Var.a.b")).IsValid());
		});
	});
}

#endif
