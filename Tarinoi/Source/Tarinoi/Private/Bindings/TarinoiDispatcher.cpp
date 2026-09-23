// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Bindings/TarinoiDispatcher.h"

#include "Bindings/TarinoiBindings.h"
#include "Tarinoi.h"
#include "TarinoiJson.h"

FTarinoiDispatcher::FTarinoiDispatcher(UTarinoiBindings* InBindings)
	: Bindings(InBindings)
	, ContextCard(MakeShared<FJsonObject>())
{
}

bool FTarinoiDispatcher::EvalCondition(const FString& Expression)
{
	if (Expression.IsEmpty())
	{
		return true;
	}

	const FTarinoiExprPtr Ast = GetConditionAst(Expression);
	return !Ast.IsValid() || Eval(*Ast).IsTruthy();
}

FTarinoiValue FTarinoiDispatcher::EvalCall(const FString& Expression)
{
	if (Expression.IsEmpty())
	{
		return FTarinoiValue::None();
	}

	const FTarinoiExprPtr Ast = GetCallAst(Expression);
	return Ast.IsValid() ? Eval(*Ast) : FTarinoiValue::None();
}

FTarinoiValue FTarinoiDispatcher::EvalValue(const FString& Expression)
{
	if (Expression.IsEmpty())
	{
		return FTarinoiValue::None();
	}

	const FTarinoiExprPtr Ast = GetConditionAst(Expression);
	return Ast.IsValid() ? Eval(*Ast) : FTarinoiValue::None();
}

bool FTarinoiDispatcher::HasCall(const FString& Expression)
{
	if (Expression.IsEmpty())
	{
		return false;
	}

	const FTarinoiExprPtr Ast = GetCallAst(Expression);
	if (!Ast.IsValid() || !Bindings.IsValid())
	{
		return false;
	}

	const UTarinoiFunctionCollection* Functions = Bindings->GetFunctions(Ast->Collection);
	return Functions && Functions->HasFunction(FName(*Ast->Name));
}

FTarinoiExprPtr FTarinoiDispatcher::GetConditionAst(const FString& Expression)
{
	if (const FTarinoiExprPtr* Cached = ConditionCache.Find(Expression))
	{
		return *Cached;
	}
	return ConditionCache.Add(Expression, TarinoiExpressionParser::ParseCondition(Expression));
}

FTarinoiExprPtr FTarinoiDispatcher::GetCallAst(const FString& Expression)
{
	if (const FTarinoiExprPtr* Cached = CallCache.Find(Expression))
	{
		return *Cached;
	}
	return CallCache.Add(Expression, TarinoiExpressionParser::ParseCall(Expression));
}

FTarinoiValue FTarinoiDispatcher::Eval(const FTarinoiExpr& Node)
{
	switch (Node.Kind)
	{
	case ETarinoiExprKind::Bool:
		return FTarinoiValue::MakeBool(Node.BoolValue);
	case ETarinoiExprKind::Int:
		return FTarinoiValue::MakeNumber(static_cast<double>(Node.IntValue));
	case ETarinoiExprKind::Float:
		return FTarinoiValue::MakeNumber(Node.FloatValue);
	case ETarinoiExprKind::String:
		return FTarinoiValue::MakeString(Node.Text);
	case ETarinoiExprKind::Not:
		return FTarinoiValue::MakeBool(!Eval(*Node.Children[0]).IsTruthy());
	case ETarinoiExprKind::And:
		// Short-circuits: the right side must not run, because authored functions have side effects.
		return FTarinoiValue::MakeBool(Eval(*Node.Children[0]).IsTruthy() && Eval(*Node.Children[1]).IsTruthy());
	case ETarinoiExprKind::Or:
		return FTarinoiValue::MakeBool(Eval(*Node.Children[0]).IsTruthy() || Eval(*Node.Children[1]).IsTruthy());
	case ETarinoiExprKind::Call:
		return DispatchCall(Node);
	case ETarinoiExprKind::Ref:
		return ResolveRef(Node);
	case ETarinoiExprKind::CardRef:
		return FTarinoiValue::MakeJson(ContextCard);
	}
	return FTarinoiValue::None();
}

FTarinoiValue FTarinoiDispatcher::DispatchCall(const FTarinoiExpr& Node)
{
	UTarinoiFunctionCollection* Functions = Bindings.IsValid() ? Bindings->GetFunctions(Node.Collection) : nullptr;
	if (!Functions)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Dispatcher: no bindings registered for function collection '%s' (needed by %s)"),
			*Node.Collection, *Node.ToString());
		return FTarinoiValue::MakeBool(false);
	}

	const FName Name(*Node.Name);
	if (!Functions->HasFunction(Name))
	{
		UE_LOG(LogTarinoi, Error, TEXT("Dispatcher: function '%s.%s' is not implemented. Regenerate your bindings if it was added recently."),
			*Node.Collection, *Node.Name);
		return FTarinoiValue::MakeBool(false);
	}

	TArray<FTarinoiValue> Args;
	Args.Reserve(Node.Children.Num());
	for (const TSharedRef<const FTarinoiExpr>& Arg : Node.Children)
	{
		Args.Add(Eval(*Arg));
	}

	if (UE_LOG_ACTIVE(LogTarinoi, Verbose))
	{
		TArray<FString> Described;
		for (const FTarinoiValue& Arg : Args)
		{
			Described.Add(Arg.Describe());
		}
		UE_LOG(LogTarinoi, Verbose, TEXT("-> Fn.%s.%s(%s)"), *Node.Collection, *Node.Name, *FString::Join(Described, TEXT(", ")));
	}

	FTarinoiValue Result;
	if (!Functions->TryInvoke(Name, Args, Result))
	{
		UE_LOG(LogTarinoi, Error, TEXT("Dispatcher: could not call '%s.%s'"), *Node.Collection, *Node.Name);
		return FTarinoiValue::MakeBool(false);
	}

	UE_LOG(LogTarinoi, Verbose, TEXT("   <- %s"), *Result.Describe());
	return Result;
}

FTarinoiValue FTarinoiDispatcher::ResolveRef(const FTarinoiExpr& Node)
{
	switch (Node.RefKind)
	{
	case ETarinoiRefKind::Variable:
	{
		UTarinoiVariableCollection* Variables = Bindings.IsValid() ? Bindings->GetVariables(Node.Collection) : nullptr;
		if (!Variables)
		{
			UE_LOG(LogTarinoi, Error, TEXT("Dispatcher: no bindings registered for variable collection '%s' (needed by %s)"),
				*Node.Collection, *Node.ToString());
			return FTarinoiValue::None();
		}

		// Deliberately not read here: functions receive the reference so they can write to it.
		return FTarinoiValue::MakeVariable(Variables, Node.Collection, FName(*Node.Name));
	}

	case ETarinoiRefKind::Entity:
	{
		UTarinoiEntityCollection* Entities = Bindings.IsValid() ? Bindings->GetEntities(Node.Collection) : nullptr;
		if (!Entities)
		{
			UE_LOG(LogTarinoi, Error, TEXT("Dispatcher: no bindings registered for entity collection '%s' (needed by %s)"),
				*Node.Collection, *Node.ToString());
			return FTarinoiValue::None();
		}

		return FTarinoiValue::MakeObject(Entities->GetEntity(FName(*Node.Name)));
	}

	default:
		return ResolveListOption(Node);
	}
}

FTarinoiValue FTarinoiDispatcher::ResolveListOption(const FTarinoiExpr& Node)
{
	const TArray<TSharedPtr<FJsonObject>>* Options = Lists.Find(Node.Collection + TEXT("/") + Node.Name);
	if (!Options)
	{
		UE_LOG(LogTarinoi, Warning, TEXT("Dispatcher: no list '%s.%s' in the synced content. Has it been synced since the list was added?"),
			*Node.Collection, *Node.Name);
		return FTarinoiValue::None();
	}

	for (const TSharedPtr<FJsonObject>& Option : *Options)
	{
		if (TarinoiJson::Str(Option, TEXT("key")).Equals(Node.Key, ESearchCase::CaseSensitive))
		{
			// Newer content uses option_value; older content used value.
			TSharedPtr<FJsonValue> Value = Option->TryGetField(TEXT("option_value"));
			if (!Value.IsValid())
			{
				Value = Option->TryGetField(TEXT("value"));
			}
			return FTarinoiValue::FromJson(Value);
		}
	}

	UE_LOG(LogTarinoi, Warning, TEXT("Dispatcher: list '%s.%s' has no option '%s'"), *Node.Collection, *Node.Name, *Node.Key);
	return FTarinoiValue::None();
}
