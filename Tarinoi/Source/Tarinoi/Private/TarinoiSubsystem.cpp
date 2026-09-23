// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "TarinoiSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Tarinoi.h"
#include "TarinoiRuntime.h"
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
