// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Bindings/TarinoiValue.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Expressions/TarinoiExpression.h"
#include "TarinoiTypes.h"
#include "UObject/WeakObjectPtr.h"

class UTarinoiBindings;

/** Authored option lists, keyed "collectionName/listIdentifier" as Ls.* expressions name them. */
using FTarinoiLists = TTarinoiMap<TArray<TSharedPtr<FJsonObject>>>;

/**
 * Evaluates authored expressions against the game's bindings.
 *
 * Nothing here fails hard. An unbound collection, a missing function or an unknown list key is
 * logged and degrades to false or None: authored content is data, and a mistake in it should show
 * up as a dialogue that behaves oddly, not as a crashed game.
 *
 * Parsed expressions are cached by their source text, failures included, so a malformed
 * expression is reported once rather than on every evaluation.
 */
class TARINOI_API FTarinoiDispatcher
{
public:
	explicit FTarinoiDispatcher(UTarinoiBindings* InBindings);

	/** Replaces the list data behind Ls.*. Called after configuration and after every sync. */
	void SetLists(const FTarinoiLists& InLists) { Lists = InLists; }

	/** Sets the card Card.* resolves to. */
	void SetContextCard(const TSharedPtr<FJsonObject>& Card) { ContextCard = Card.IsValid() ? Card : MakeShared<FJsonObject>(); }

	/**
	 * Evaluates a condition. An empty or unparseable expression passes: an author who wrote no
	 * condition, or a broken one, should not silently lose content.
	 */
	bool EvalCondition(const FString& Expression);

	/** Evaluates a function call. None when empty or unparseable. */
	FTarinoiValue EvalCall(const FString& Expression);

	/** Evaluates any expression and returns its raw value, without coercing to bool. */
	FTarinoiValue EvalValue(const FString& Expression);

	/** Whether a call names a function that is bound and exists, without calling it. */
	bool HasCall(const FString& Expression);

private:
	FTarinoiExprPtr GetConditionAst(const FString& Expression);
	FTarinoiExprPtr GetCallAst(const FString& Expression);

	FTarinoiValue Eval(const FTarinoiExpr& Node);
	FTarinoiValue DispatchCall(const FTarinoiExpr& Node);
	FTarinoiValue ResolveRef(const FTarinoiExpr& Node);
	FTarinoiValue ResolveListOption(const FTarinoiExpr& Node);

	TWeakObjectPtr<UTarinoiBindings> Bindings;
	TTarinoiMap<FTarinoiExprPtr> ConditionCache;
	TTarinoiMap<FTarinoiExprPtr> CallCache;
	FTarinoiLists Lists;
	TSharedPtr<FJsonObject> ContextCard;
};
