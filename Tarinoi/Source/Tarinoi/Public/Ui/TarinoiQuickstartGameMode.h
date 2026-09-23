// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "TarinoiQuickstartGameMode.generated.h"

class UTarinoiQuickstartWidget;
class UTarinoiRuntime;

/**
 * A complete, playable Tarinoi setup: sync, pick an entry point, play the dialogue.
 *
 * Set it as the GameMode Override in any level's World Settings (or as the project's default game
 * mode) and press Play. It builds its own interface, so nothing else needs setting up.
 *
 * To register your game's bindings, derive from it, in C++ or Blueprint, and override
 * SetupBindings. It runs once the runtime is configured and before any dialogue plays:
 *
 *   void AMyQuickstartGameMode::SetupBindings_Implementation(UTarinoiRuntime* Runtime)
 *   {
 *       Runtime->GetBindings()->BindFunctions(TEXT("global"), NewObject<UMyGlobalFunctions>(this));
 *   }
 *
 * Whatever you leave unbound that the generated code supplies on its own (a generated variables
 * class, the scaffolded core functions) is bound afterwards, so content that only uses
 * Fn.tarinoi.* plays with no bindings written at all.
 *
 * This is a starting point for authors and a smoke test for integrators, not a shipping UI.
 */
UCLASS(Blueprintable)
class TARINOI_API ATarinoiQuickstartGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATarinoiQuickstartGameMode();

	/** Sync from the Tarinoi API when play starts. Turn off to play already-synced content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	bool bSyncOnStart = true;

	/** Bind whatever the generated code supplies for collections SetupBindings left unbound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	bool bBindGeneratedDefaults = true;

	/** The interface to show. Subclass UTarinoiQuickstartWidget to restyle it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	TSubclassOf<UTarinoiQuickstartWidget> WidgetClass;

	/** Register your game's functions, variables and entities here. */
	UFUNCTION(BlueprintNativeEvent, Category = "Tarinoi")
	void SetupBindings(UTarinoiRuntime* Runtime);

	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	UTarinoiQuickstartWidget* GetWidget() const { return Widget; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UTarinoiQuickstartWidget> Widget;
};
