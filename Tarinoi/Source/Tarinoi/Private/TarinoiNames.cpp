// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiNames.h"

namespace TarinoiNames
{
	FString ToPascal(const FString& Authored)
	{
		FString Out;
		bool bCapitalise = true;

		for (const TCHAR C : Authored)
		{
			if (C == TEXT('_') || C == TEXT('-') || C == TEXT(' ') || C == TEXT('.'))
			{
				bCapitalise = true;
				continue;
			}

			// ASCII only: C++ identifiers in generated code must be portable across compilers.
			const bool bLetter = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'));
			const bool bDigit = C >= TEXT('0') && C <= TEXT('9');
			if (!bLetter && !bDigit)
			{
				continue;
			}

			Out.AppendChar(bCapitalise ? FChar::ToUpper(C) : C);
			bCapitalise = false;
		}

		if (Out.IsEmpty())
		{
			return TEXT("Unnamed");
		}

		if (FChar::IsDigit(Out[0]))
		{
			Out.InsertAt(0, TEXT('_'));
		}

		return Out;
	}
}
