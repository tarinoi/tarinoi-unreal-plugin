// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

/** A minimal indenting source writer. Tabs, as UE code is indented. */
class FTarinoiCodeWriter
{
public:
	void Line(const FString& Text = FString())
	{
		if (!Text.IsEmpty())
		{
			for (int32 I = 0; I < Depth; ++I)
			{
				Out.AppendChar(TEXT('\t'));
			}
			Out.Append(Text);
		}
		Out.AppendChar(TEXT('\n'));
	}

	void Open() { Line(TEXT("{")); ++Depth; }
	void Close(const TCHAR* Suffix = TEXT("")) { --Depth; Line(FString(TEXT("}")) + Suffix); }
	void Indent() { ++Depth; }
	void Outdent() { Depth = FMath::Max(0, Depth - 1); }

	/** A doc comment, one line per entry. */
	void Doc(std::initializer_list<FString> Lines)
	{
		if (Lines.size() == 1)
		{
			Line(FString::Printf(TEXT("/** %s */"), *(*Lines.begin())));
			return;
		}
		Line(TEXT("/**"));
		for (const FString& Text : Lines)
		{
			Line(Text.IsEmpty() ? FString(TEXT(" *")) : TEXT(" * ") + Text);
		}
		Line(TEXT(" */"));
	}

	const FString& ToString() const { return Out; }

private:
	FString Out;
	int32 Depth = 0;
};
