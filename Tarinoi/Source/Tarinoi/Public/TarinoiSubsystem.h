// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "TarinoiSubsystem.generated.h"

class UTarinoiRuntime;

/**
 * Owns the game's Tarinoi runtime for the lifetime of the game instance, and configures it from
 * the project settings when the game starts.
 *
 * Blueprints reach it with Get Tarinoi Runtime; C++ with
 * GetGameInstance()->GetSubsystem<UTarinoiSubsystem>()->GetRuntime().
 *
 * With polling enabled in the settings it also re-syncs on a timer, so authored changes appear
 * during Play In Editor without restarting. Leave polling off for shipping builds.
 */
UCLASS()
class TARINOI_API UTarinoiSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	UTarinoiRuntime* GetRuntime() const { return Runtime; }

	/** The runtime for the game instance a world-context object belongs to. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tarinoi Runtime"))
	static UTarinoiRuntime* GetTarinoiRuntime(const UObject* WorldContextObject);

private:
	bool Poll(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UTarinoiRuntime> Runtime;

	FTSTicker::FDelegateHandle PollHandle;
};
