// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Components/TarinoiDialogueTrigger.h"

#include "GameFramework/Pawn.h"
#include "Tarinoi.h"

void UTarinoiDialogueTrigger::Trigger()
{
	if (!IsConfigured())
	{
		UE_LOG(LogTarinoi, Warning, TEXT("The dialogue trigger on '%s' has no dialogue set. Pick a Start Card in its details."),
			*GetNameSafe(GetOwner()));
		return;
	}

	OnTriggered.Broadcast(CollectionId, CardId);
}

ATarinoiDialogueVolume::ATarinoiDialogueVolume()
{
	DialogueTrigger = CreateDefaultSubobject<UTarinoiDialogueTrigger>(TEXT("DialogueTrigger"));
}

bool ATarinoiDialogueVolume::Matches(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	if (bPlayerPawnsOnly)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn || !Pawn->IsPlayerControlled())
		{
			return false;
		}
	}

	return RequiredActorTag.IsNone() || Actor->ActorHasTag(RequiredActorTag);
}

void ATarinoiDialogueVolume::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	if (!Matches(OtherActor))
	{
		return;
	}

	Occupant = OtherActor;
	OnOccupantEntered.Broadcast(OtherActor);

	if (Mode == ETarinoiTriggerMode::WhileInside || (bFired && bOnceOnly))
	{
		return;
	}

	bFired = true;
	DialogueTrigger->Trigger();
}

void ATarinoiDialogueVolume::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);
	if (!Matches(OtherActor) || Occupant.Get() != OtherActor)
	{
		return;
	}

	Occupant.Reset();
	OnOccupantExited.Broadcast(OtherActor);
}
