// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"

#include "TarinoiDialogueTrigger.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTarinoiOnDialogueTriggered, const FString&, CollectionId, const FString&, CardId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTarinoiOnOccupant, AActor*, Occupant);

/**
 * Marks something in the world that starts a dialogue.
 *
 * It deliberately does not start the dialogue itself. Trigger raises OnTriggered with the
 * configured entry point and your game decides: whether the player holds the right item, whether a
 * cutscene is running, which UI to open.
 *
 *   Trigger->OnTriggered.AddDynamic(this, &AMyNpc::StartTalking);
 *   ...
 *   void AMyNpc::StartTalking(const FString& CollectionId, const FString& CardId)
 *   {
 *       UTarinoiSubsystem::GetTarinoiRuntime(this)->StartDialogue(CollectionId, CardId);
 *   }
 *
 * In the editor, the Start Card dropdown lists the synced entry points and fills in both ids.
 */
UCLASS(ClassGroup = (Tarinoi), meta = (BlueprintSpawnableComponent))
class TARINOI_API UTarinoiDialogueTrigger : public UActorComponent
{
	GENERATED_BODY()

public:
	/** The collection holding the dialogue to start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	FString CollectionId;

	/** The card the dialogue starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	FString CardId;

	/** Raised by Trigger with the configured entry point. */
	UPROPERTY(BlueprintAssignable, Category = "Tarinoi")
	FTarinoiOnDialogueTriggered OnTriggered;

	/** Whether this trigger has somewhere to send the player. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	bool IsConfigured() const { return !CollectionId.IsEmpty() && !CardId.IsEmpty(); }

	/** Fires the trigger. Call it from your interaction code: a key press, an overlap, a button. */
	UFUNCTION(BlueprintCallable, Category = "Tarinoi")
	void Trigger();
};

/** When a UTarinoiDialogueTrigger volume fires. */
UENUM(BlueprintType)
enum class ETarinoiTriggerMode : uint8
{
	/** The moment something walks in: a cutscene, an ambush, a threshold. */
	OnEnter,
	/**
	 * Never on its own. The volume reports who is inside and the game decides: the shape for
	 * "press E to talk". Call Trigger on the volume's component when the player asks.
	 */
	WhileInside,
};

/**
 * A trigger box carrying a UTarinoiDialogueTrigger, driven by overlaps.
 *
 * By default only pawns controlled by a player set it off, so stray physics objects do not. In
 * WhileInside mode it fires nothing itself: it raises OnOccupantEntered and OnOccupantExited, your
 * game shows a prompt, and calls Trigger when the player asks.
 */
UCLASS(ClassGroup = (Tarinoi))
class TARINOI_API ATarinoiDialogueVolume : public ATriggerBox
{
	GENERATED_BODY()

public:
	ATarinoiDialogueVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tarinoi")
	TObjectPtr<UTarinoiDialogueTrigger> DialogueTrigger;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	ETarinoiTriggerMode Mode = ETarinoiTriggerMode::OnEnter;

	/** Fire only the first time. Ignored in WhileInside mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	bool bOnceOnly = true;

	/** Only pawns a player controls set it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	bool bPlayerPawnsOnly = true;

	/** If set, only actors with this tag set it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tarinoi")
	FName RequiredActorTag;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi")
	FTarinoiOnOccupant OnOccupantEntered;

	UPROPERTY(BlueprintAssignable, Category = "Tarinoi")
	FTarinoiOnOccupant OnOccupantExited;

	/** Whether a matching actor is inside right now. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	bool IsOccupied() const { return Occupant.IsValid(); }

	/** Whether an actor passes the filters. */
	UFUNCTION(BlueprintPure, Category = "Tarinoi")
	bool Matches(const AActor* Actor) const;

	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;

private:
	TWeakObjectPtr<AActor> Occupant;
	bool bFired = false;
};
