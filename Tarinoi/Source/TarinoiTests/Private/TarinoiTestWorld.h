// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"

/** A throwaway game world, for tests that spawn actors or create widgets. */
struct FTarinoiTestWorld
{
	UWorld* World = nullptr;

	FTarinoiTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TarinoiTestWorld"));
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
	}

	~FTarinoiTestWorld()
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
};

#endif
