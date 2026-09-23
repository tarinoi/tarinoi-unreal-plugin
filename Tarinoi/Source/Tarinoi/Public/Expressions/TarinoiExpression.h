// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** What an expression node is. */
enum class ETarinoiExprKind : uint8
{
	Bool,
	Int,
	Float,
	String,
	Not,
	And,
	Or,
	/** Fn.collection.Name(args) */
	Call,
	/** Var.collection.name, Ent.collection.name, or Ls.collection.list.key */
	Ref,
	/** Card.Name: the card currently being processed. */
	CardRef,
};

enum class ETarinoiRefKind : uint8
{
	Variable,
	Entity,
	List,
};

/**
 * A node of a parsed Tarinoi expression. One tagged struct rather than a class hierarchy:
 * the grammar is small and fixed, and evaluation is a single switch on Kind.
 */
struct TARINOI_API FTarinoiExpr
{
	ETarinoiExprKind Kind = ETarinoiExprKind::Bool;

	bool BoolValue = false;
	int64 IntValue = 0;
	double FloatValue = 0.0;

	/** A string literal's text, or a Card.* member name. */
	FString Text;

	/** For Call and Ref. */
	ETarinoiRefKind RefKind = ETarinoiRefKind::Variable;
	FString Collection;
	FString Name;

	/** For List refs only. */
	FString Key;

	/** Operands of Not/And/Or, or a call's arguments. */
	TArray<TSharedRef<const FTarinoiExpr>> Children;

	/** The expression written back out, for log messages. */
	FString ToString() const;
};

using FTarinoiExprPtr = TSharedPtr<const FTarinoiExpr>;

/**
 * Parses Tarinoi expression strings.
 *
 * The grammar, loosest binding first:
 *
 *   condition := or
 *   or        := and ("||" and)*
 *   and       := not ("&&" not)*
 *   not       := "!" not | atom
 *   atom      := "true" | "false" | number | string | "(" condition ")" | ref
 *   ref       := "Fn" "." ID "." ID "(" args ")"
 *              | "Ls" "." ID "." ID "." ID
 *              | "Var" "." ID "." ID
 *              | "Ent" "." ID "." ID
 *              | "Card" "." ID
 *   args      := empty | arg ("," arg)*
 *   arg       := literal | ref
 *
 * Numbers are unsigned: there is no negative literal. Strings are double-quoted with no escapes,
 * matching what the Tarinoi editor writes.
 *
 * Failures are logged (the first error only; a derailed cursor produces noise) and reported as
 * null rather than asserted. Expressions come from authored content, so a malformed one is an
 * authoring mistake that should degrade gracefully.
 */
namespace TarinoiExpressionParser
{
	/** Parses a condition. Null for an empty expression or a parse failure. */
	TARINOI_API FTarinoiExprPtr ParseCondition(const FString& Expression);

	/** Parses a single function call, as an output selector uses. Null when empty or not a call. */
	TARINOI_API FTarinoiExprPtr ParseCall(const FString& Expression);
}
