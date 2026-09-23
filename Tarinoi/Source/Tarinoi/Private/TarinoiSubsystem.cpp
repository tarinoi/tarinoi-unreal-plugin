// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Tarinoi.h"
#include "TarinoiRuntime.h"
#include "HAL/IConsoleManager.h"
#include "TarinoiSettings.h"

void UTarinoiSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Runtime = NewObject<UTarinoiRuntime>(this);

	const UTarinoiSettings* Settings = GetDefault<UTarinoiSettings>();
	if (Settings->GetProjectId().IsEmpty())
	{
		// Not an error: the plugin may be enabled in a project that has not been set up yet.
		UE_LOG(LogTarinoi, Warning, TEXT("Tarinoi is not set up for this project: set the API path in Project Settings > Plugins > Tarinoi."));
		return;
	}

	if (!Runtime->Configure())
	{
		return;
	}

	if (Settings->bPollEnabled && !Settings->bOfflineMode)
	{
		const float Interval = FMath::Max(1, Settings->PollInterval);
		PollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTarinoiSubsystem::Poll), Interval);
		UE_LOG(LogTarinoi, Log, TEXT("Tarinoi: re-syncing every %.0fs."), Interval);
	}
}

void UTarinoiSubsystem::Deinitialize()
{
	if (PollHandle.IsValid())
	{
		FTSTicker::RemoveTicker(PollHandle);
		PollHandle.Reset();
	}

	if (Runtime)
	{
		Runtime->Shutdown();
	}

	Super::Deinitialize();
}

bool UTarinoiSubsystem::Poll(float DeltaTime)
{
	// Safe mid-dialogue: the running card is already loaded, and a sync only refreshes the
	// caches and content for what comes next.
	if (Runtime && Runtime->IsConfigured())
	{
		Runtime->Sync();
	}
	return true;
}

UTarinoiRuntime* UTarinoiSubsystem::GetTarinoiRuntime(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UTarinoiSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UTarinoiSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetRuntime() : nullptr;
}

// -----------------------------------------------------------------------------
// Console commands: drive the running game's dialogue from the console, for development.
// -----------------------------------------------------------------------------

namespace
{
	UTarinoiRuntime* RuntimeFor(UWorld* World)
	{
		UTarinoiRuntime* Runtime = World ? UTarinoiSubsystem::GetTarinoiRuntime(World) : nullptr;
		if (!Runtime)
		{
			UE_LOG(LogTarinoi, Warning, TEXT("Tarinoi: no running game with a Tarinoi runtime."));
		}
		return Runtime;
	}

	FAutoConsoleCommandWithWorldAndArgs StartCommand(
		TEXT("Tarinoi.Start"),
		TEXT("Starts a dialogue: Tarinoi.Start <CollectionId> <CardId>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World); Runtime && Args.Num() == 2)
			{
				Runtime->StartDialogue(Args[0], Args[1]);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs StartFirstCommand(
		TEXT("Tarinoi.StartFirst"),
		TEXT("Starts the first listed entry point, or the Nth: Tarinoi.StartFirst [N]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World))
			{
				const TArray<FTarinoiStartCard> Cards = Runtime->GetStartCards();
				const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
				if (Cards.IsValidIndex(Index))
				{
					Runtime->StartDialogue(Cards[Index].CollectionId, Cards[Index].CardId);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ListCommand(
		TEXT("Tarinoi.List"),
		TEXT("Lists the entry points."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World))
			{
				int32 Index = 0;
				for (const FTarinoiStartCard& Card : Runtime->GetStartCards())
				{
					UE_LOG(LogTarinoi, Display, TEXT("%d: %s / %s  (%s %s)"), Index++, *Card.CollectionLabel, *Card.Label, *Card.CollectionId, *Card.CardId);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs AdvanceCommand(
		TEXT("Tarinoi.Advance"),
		TEXT("Moves past the line on screen."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World))
			{
				Runtime->Advance();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs ChooseCommand(
		TEXT("Tarinoi.Choose"),
		TEXT("Picks a choice by its number as shown (1 is the first): Tarinoi.Choose <N>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World); Runtime && Args.Num() == 1)
			{
				Runtime->SelectChoice(FCString::Atoi(*Args[0]) - 1);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs AbortCommand(
		TEXT("Tarinoi.Abort"),
		TEXT("Ends the running dialogue."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World))
			{
				Runtime->AbortDialogue();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs SyncCommand(
		TEXT("Tarinoi.Sync"),
		TEXT("Fetches new content from the Tarinoi API."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTarinoiRuntime* Runtime = RuntimeFor(World))
			{
				Runtime->Sync();
			}
		}));
}
