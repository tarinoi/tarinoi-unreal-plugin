// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Ui/TarinoiQuickstartGameMode.h"

#include "Bindings/TarinoiBindings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Tarinoi.h"
#include "TarinoiRuntime.h"
#include "TarinoiSubsystem.h"
#include "Ui/TarinoiQuickstartWidget.h"

ATarinoiQuickstartGameMode::ATarinoiQuickstartGameMode()
{
	// Nothing to walk around in: the quickstart is all interface.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	WidgetClass = UTarinoiQuickstartWidget::StaticClass();
}

void ATarinoiQuickstartGameMode::SetupBindings_Implementation(UTarinoiRuntime* Runtime)
{
}

void ATarinoiQuickstartGameMode::BeginPlay()
{
	Super::BeginPlay();

	UTarinoiRuntime* Runtime = UTarinoiSubsystem::GetTarinoiRuntime(this);
	if (!Runtime)
	{
		UE_LOG(LogTarinoi, Error, TEXT("Quickstart: no Tarinoi runtime. Is the Tarinoi plugin enabled?"));
		return;
	}

	if (Runtime->IsConfigured())
	{
		SetupBindings(Runtime);
		if (bBindGeneratedDefaults)
		{
			for (const FString& Bound : Runtime->GetBindings()->BindGeneratedDefaults())
			{
				UE_LOG(LogTarinoi, Log, TEXT("Quickstart: bound %s from the generated bindings."), *Bound);
			}
		}
	}

	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player || !WidgetClass)
	{
		return;
	}

	Widget = CreateWidget<UTarinoiQuickstartWidget>(Player, WidgetClass);
	Widget->AddToViewport();
	Widget->BindRuntime(Runtime);
	Widget->ShowPicker();

	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(InputMode);
	Player->SetShowMouseCursor(true);

	if (bSyncOnStart && Runtime->IsConfigured())
	{
		// The picker lists what is already synced now, and refreshes when this completes.
		Runtime->Sync();
	}
}
