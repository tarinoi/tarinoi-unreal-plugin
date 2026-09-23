// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Expressions/TarinoiExpression.h"

#include "Tarinoi.h"

namespace
{
	enum class ETokenKind : uint8
	{
		Identifier,
		Int,
		Float,
		String,
		Operator,
	};

	struct FToken
	{
		ETokenKind Kind = ETokenKind::Operator;
		FString Text;
		int64 IntValue = 0;
		double FloatValue = 0.0;

		bool IsOperator(const TCHAR* Op) const { return Kind == ETokenKind::Operator && Text == Op; }
	};

	bool IsIdentifierStart(TCHAR C)
	{
		return C == TEXT('_') || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'));
	}

	bool IsDigit(TCHAR C)
	{
		return C >= TEXT('0') && C <= TEXT('9');
	}

	bool Tokenize(const FString& Expr, TArray<FToken>& OutTokens)
	{
		int32 I = 0;
		const int32 Len = Expr.Len();

		while (I < Len)
		{
			const TCHAR C = Expr[I];

			if (C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\n') || C == TEXT('\r'))
			{
				++I;
				continue;
			}

			if (I + 1 < Len && ((C == TEXT('&') && Expr[I + 1] == TEXT('&')) || (C == TEXT('|') && Expr[I + 1] == TEXT('|'))))
			{
				OutTokens.Add({ETokenKind::Operator, Expr.Mid(I, 2)});
				I += 2;
				continue;
			}

			if (C == TEXT('!') || C == TEXT('(') || C == TEXT(')') || C == TEXT(',') || C == TEXT('.'))
			{
				OutTokens.Add({ETokenKind::Operator, FString(1, &C)});
				++I;
				continue;
			}

			if (C == TEXT('"'))
			{
				// No escapes: authored strings are plain text, as the Tarinoi editor writes them.
				int32 End = I + 1;
				while (End < Len && Expr[End] != TEXT('"'))
				{
					++End;
				}

				if (End >= Len)
				{
					UE_LOG(LogTarinoi, Error, TEXT("Expression: unterminated string in: %s"), *Expr);
					return false;
				}

				OutTokens.Add({ETokenKind::String, Expr.Mid(I + 1, End - I - 1)});
				I = End + 1;
				continue;
			}

			if (IsDigit(C))
			{
				int32 End = I;
				while (End < Len && IsDigit(Expr[End]))
				{
					++End;
				}

				// A '.' starts a fraction only when a digit follows it; otherwise it is member
				// access, as in "Ls.col.list.2".
				if (End + 1 < Len && Expr[End] == TEXT('.') && IsDigit(Expr[End + 1]))
				{
					++End;
					while (End < Len && IsDigit(Expr[End]))
					{
						++End;
					}

					FToken Token{ETokenKind::Float, Expr.Mid(I, End - I)};
					Token.FloatValue = FCString::Atod(*Token.Text);
					OutTokens.Add(Token);
				}
				else
				{
					FToken Token{ETokenKind::Int, Expr.Mid(I, End - I)};
					Token.IntValue = FCString::Atoi64(*Token.Text);
					OutTokens.Add(Token);
				}

				I = End;
				continue;
			}

			if (IsIdentifierStart(C))
			{
				int32 End = I;
				while (End < Len && (IsIdentifierStart(Expr[End]) || IsDigit(Expr[End])))
				{
					++End;
				}

				OutTokens.Add({ETokenKind::Identifier, Expr.Mid(I, End - I)});
				I = End;
				continue;
			}

			UE_LOG(LogTarinoi, Error, TEXT("Expression: unexpected character '%c' at position %d in: %s"), C, I, *Expr);
			return false;
		}

		return true;
	}

	TSharedRef<FTarinoiExpr> MakeNode(ETarinoiExprKind Kind)
	{
		TSharedRef<FTarinoiExpr> Node = MakeShared<FTarinoiExpr>();
		Node->Kind = Kind;
		return Node;
	}

	TSharedRef<FTarinoiExpr> MakeBool(bool bValue)
	{
		TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Bool);
		Node->BoolValue = bValue;
		return Node;
	}

	/** Recursive-descent parser over a token list. One instance per parse. */
	class FParser
	{
	public:
		explicit FParser(const TArray<FToken>& InTokens) : Tokens(InTokens) {}

		bool HasError() const { return bError; }
		bool AtEnd() const { return Pos >= Tokens.Num(); }

		TSharedRef<FTarinoiExpr> ParseCondition() { return ParseOr(); }

		TSharedRef<FTarinoiExpr> ParseRefOrCall()
		{
			if (AtEnd() || Peek().Kind != ETokenKind::Identifier)
			{
				Fail(FString::Printf(TEXT("expected Fn/Var/Ent/Ls/Card, got '%s'"), *Describe()));
				return MakeBool(false);
			}

			const FString Prefix = Consume().Text;
			if (Prefix != TEXT("Fn") && Prefix != TEXT("Var") && Prefix != TEXT("Ent") && Prefix != TEXT("Ls") && Prefix != TEXT("Card"))
			{
				Fail(FString::Printf(TEXT("expected Fn/Var/Ent/Ls/Card, got '%s'"), *Prefix));
				return MakeBool(false);
			}

			Expect(TEXT("."));

			if (Prefix == TEXT("Card"))
			{
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::CardRef);
				Node->Text = ReadName();
				return Node;
			}

			const FString Collection = ReadName();
			Expect(TEXT("."));
			const FString Name = ReadName();

			if (Prefix == TEXT("Fn"))
			{
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Call);
				Node->Collection = Collection;
				Node->Name = Name;
				Expect(TEXT("("));
				ParseArgs(Node->Children);
				Expect(TEXT(")"));
				return Node;
			}

			TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Ref);
			Node->Collection = Collection;
			Node->Name = Name;

			if (Prefix == TEXT("Ls"))
			{
				Node->RefKind = ETarinoiRefKind::List;
				Expect(TEXT("."));
				Node->Key = ReadName();
			}
			else
			{
				Node->RefKind = Prefix == TEXT("Var") ? ETarinoiRefKind::Variable : ETarinoiRefKind::Entity;
			}

			return Node;
		}

	private:
		const FToken& Peek() const { return Tokens[Pos]; }
		const FToken& Consume() { return Tokens[Pos++]; }

		FString Describe() const
		{
			return AtEnd() ? FString(TEXT("end of expression")) : Peek().Text;
		}

		void Fail(const FString& Message)
		{
			if (!bError)
			{
				UE_LOG(LogTarinoi, Error, TEXT("Expression: %s"), *Message);
			}
			bError = true;
		}

		bool Expect(const TCHAR* Op)
		{
			if (!AtEnd() && Peek().IsOperator(Op))
			{
				++Pos;
				return true;
			}

			Fail(FString::Printf(TEXT("expected '%s', got '%s'"), Op, *Describe()));
			return false;
		}

		/**
		 * A name segment. Deliberately lenient: a missing name reads as empty and fails later as
		 * an unbound lookup naming the collection, which is more useful than a bare syntax error.
		 * A numeric segment (a list key like "2") is accepted as a name.
		 */
		FString ReadName()
		{
			if (!AtEnd() && (Peek().Kind == ETokenKind::Identifier || Peek().Kind == ETokenKind::Int))
			{
				return Consume().Text;
			}
			return FString();
		}

		TSharedRef<FTarinoiExpr> ParseOr()
		{
			TSharedRef<FTarinoiExpr> Left = ParseAnd();
			while (!bError && !AtEnd() && Peek().IsOperator(TEXT("||")))
			{
				++Pos;
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Or);
				Node->Children = {Left, ParseAnd()};
				Left = Node;
			}
			return Left;
		}

		TSharedRef<FTarinoiExpr> ParseAnd()
		{
			TSharedRef<FTarinoiExpr> Left = ParseNot();
			while (!bError && !AtEnd() && Peek().IsOperator(TEXT("&&")))
			{
				++Pos;
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::And);
				Node->Children = {Left, ParseNot()};
				Left = Node;
			}
			return Left;
		}

		TSharedRef<FTarinoiExpr> ParseNot()
		{
			if (!AtEnd() && Peek().IsOperator(TEXT("!")))
			{
				++Pos;
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Not);
				Node->Children = {ParseNot()};
				return Node;
			}
			return ParseAtom();
		}

		/** A literal, if the next token is one. */
		TSharedPtr<FTarinoiExpr> TryLiteral()
		{
			if (AtEnd())
			{
				return nullptr;
			}

			const FToken& Token = Peek();
			switch (Token.Kind)
			{
			case ETokenKind::Identifier:
				if (Token.Text == TEXT("true") || Token.Text == TEXT("false"))
				{
					return MakeBool(Consume().Text == TEXT("true"));
				}
				return nullptr;
			case ETokenKind::Int:
			{
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Int);
				Node->IntValue = Consume().IntValue;
				return Node;
			}
			case ETokenKind::Float:
			{
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::Float);
				Node->FloatValue = Consume().FloatValue;
				return Node;
			}
			case ETokenKind::String:
			{
				TSharedRef<FTarinoiExpr> Node = MakeNode(ETarinoiExprKind::String);
				Node->Text = Consume().Text;
				return Node;
			}
			default:
				return nullptr;
			}
		}

		TSharedRef<FTarinoiExpr> ParseAtom()
		{
			if (const TSharedPtr<FTarinoiExpr> Literal = TryLiteral())
			{
				return Literal.ToSharedRef();
			}

			if (!AtEnd() && Peek().IsOperator(TEXT("(")))
			{
				++Pos;
				TSharedRef<FTarinoiExpr> Inner = ParseCondition();
				Expect(TEXT(")"));
				return Inner;
			}

			return ParseRefOrCall();
		}

		void ParseArgs(TArray<TSharedRef<const FTarinoiExpr>>& OutArgs)
		{
			if (AtEnd() || Peek().IsOperator(TEXT(")")))
			{
				return;
			}

			OutArgs.Add(ParseArg());
			while (!bError && !AtEnd() && Peek().IsOperator(TEXT(",")))
			{
				++Pos;
				OutArgs.Add(ParseArg());
			}
		}

		TSharedRef<FTarinoiExpr> ParseArg()
		{
			if (AtEnd())
			{
				Fail(TEXT("unexpected end of expression in argument list"));
				return MakeBool(false);
			}

			if (const TSharedPtr<FTarinoiExpr> Literal = TryLiteral())
			{
				return Literal.ToSharedRef();
			}

			if (Peek().Kind == ETokenKind::Identifier)
			{
				return ParseRefOrCall();
			}

			Fail(FString::Printf(TEXT("unexpected token in argument: '%s'"), *Peek().Text));
			return MakeBool(false);
		}

		const TArray<FToken>& Tokens;
		int32 Pos = 0;
		bool bError = false;
	};
}

FString FTarinoiExpr::ToString() const
{
	switch (Kind)
	{
	case ETarinoiExprKind::Bool:
		return BoolValue ? TEXT("true") : TEXT("false");
	case ETarinoiExprKind::Int:
		return FString::Printf(TEXT("%lld"), IntValue);
	case ETarinoiExprKind::Float:
		return FString::SanitizeFloat(FloatValue);
	case ETarinoiExprKind::String:
		return FString::Printf(TEXT("\"%s\""), *Text);
	case ETarinoiExprKind::Not:
		return TEXT("!") + Children[0]->ToString();
	case ETarinoiExprKind::And:
		return FString::Printf(TEXT("(%s && %s)"), *Children[0]->ToString(), *Children[1]->ToString());
	case ETarinoiExprKind::Or:
		return FString::Printf(TEXT("(%s || %s)"), *Children[0]->ToString(), *Children[1]->ToString());
	case ETarinoiExprKind::Call:
	{
		TArray<FString> Args;
		for (const TSharedRef<const FTarinoiExpr>& Arg : Children)
		{
			Args.Add(Arg->ToString());
		}
		return FString::Printf(TEXT("Fn.%s.%s(%s)"), *Collection, *Name, *FString::Join(Args, TEXT(", ")));
	}
	case ETarinoiExprKind::Ref:
		switch (RefKind)
		{
		case ETarinoiRefKind::Variable: return FString::Printf(TEXT("Var.%s.%s"), *Collection, *Name);
		case ETarinoiRefKind::Entity: return FString::Printf(TEXT("Ent.%s.%s"), *Collection, *Name);
		default: return FString::Printf(TEXT("Ls.%s.%s.%s"), *Collection, *Name, *Key);
		}
	case ETarinoiExprKind::CardRef:
		return TEXT("Card.") + Text;
	}
	return FString();
}

namespace TarinoiExpressionParser
{
	FTarinoiExprPtr ParseCondition(const FString& Expression)
	{
		const FString Text = Expression.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return nullptr;
		}

		TArray<FToken> Tokens;
		if (!Tokenize(Text, Tokens))
		{
			return nullptr;
		}

		FParser Parser(Tokens);
		TSharedRef<FTarinoiExpr> Node = Parser.ParseCondition();
		if (Parser.HasError())
		{
			return nullptr;
		}

		if (!Parser.AtEnd())
		{
			UE_LOG(LogTarinoi, Error, TEXT("Expression: unexpected tokens after the end of: %s"), *Text);
			return nullptr;
		}

		return Node;
	}

	FTarinoiExprPtr ParseCall(const FString& Expression)
	{
		const FString Text = Expression.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return nullptr;
		}

		TArray<FToken> Tokens;
		if (!Tokenize(Text, Tokens))
		{
			return nullptr;
		}

		FParser Parser(Tokens);
		TSharedRef<FTarinoiExpr> Node = Parser.ParseRefOrCall();
		if (Parser.HasError())
		{
			return nullptr;
		}

		if (Node->Kind != ETarinoiExprKind::Call || !Parser.AtEnd())
		{
			UE_LOG(LogTarinoi, Error, TEXT("Expression: expected a single function call, got: %s"), *Text);
			return nullptr;
		}

		return Node;
	}
}
