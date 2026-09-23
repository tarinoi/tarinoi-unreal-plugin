// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Sync/TarinoiNdjson.h"

#include "TarinoiJson.h"

namespace TarinoiNdjson
{
	FTarinoiNdjsonPage Parse(const FString& Body)
	{
		FTarinoiNdjsonPage Page;

		TArray<FString> Lines;
		Body.ParseIntoArrayLines(Lines, true);

		for (const FString& RawLine : Lines)
		{
			const FString Line = RawLine.TrimStartAndEnd();
			if (Line.IsEmpty())
			{
				continue;
			}

			const TSharedPtr<FJsonObject> Object = TarinoiJson::Parse(Line);
			if (!Object.IsValid())
			{
				continue;
			}

			if (Object->Values.Num() == 1 && Object->HasField(TEXT("cursor")))
			{
				const TSharedPtr<FJsonValue> Cursor = Object->TryGetField(TEXT("cursor"));
				if (Cursor.IsValid() && Cursor->Type != EJson::Null)
				{
					Page.Cursor = TarinoiJson::Str(Cursor);
				}
				else
				{
					Page.Cursor.Reset();
				}
				continue;
			}

			Page.Documents.Add(Object);
		}

		return Page;
	}
}
