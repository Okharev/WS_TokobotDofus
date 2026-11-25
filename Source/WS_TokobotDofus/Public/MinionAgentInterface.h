// MinionAgentInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MinionAgentInterface.generated.h"

class ANavLinkProxy;
class UMinionThrowTargetComponent;

UENUM(BlueprintType)
enum class EMinionState : uint8
{
	Idle               UMETA(DisplayName = "Idle"),
	Following          UMETA(DisplayName = "Following Squad"),
	EvadingPlayer      UMETA(DisplayName = "Evading Player"),
	TraversingNavLink  UMETA(DisplayName = "Traversing NavLink"),
	InAttackWhip       UMETA(DisplayName = "Locked in Chaos Attack Whip"),
	InCarryRush        UMETA(DisplayName = "Carrying Player in Palanquin"),
	InThrowFlight      UMETA(DisplayName = "Flying Toward Throw Target"),
	LatchedToTarget    UMETA(DisplayName = "Latched Onto Throw Target")
};

UINTERFACE(MinimalAPI, Blueprintable)
class UMinionAgentInterface : public UInterface
{
	GENERATED_BODY()
};

class IMinionAgentInterface
{
	GENERATED_BODY()

public:
	virtual EMinionState GetMinionState() const = 0;
	virtual bool MoveToSquadSlot(const FVector& ProjectedSlotLocation) = 0;
	virtual void EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight) = 0;
	virtual void ExecuteNavLinkJump(ANavLinkProxy* NavLink, const FVector& LaunchVelocity) = 0;
	virtual void SetWhipModeEnabled(bool bEnabled) = 0;
	virtual void SetCarryRushModeEnabled(bool bEnabled) = 0;

	// --- Throw & Interactable Latch API ---
	virtual bool LaunchAtThrowTarget(
		UMinionThrowTargetComponent* TargetComp,
		int32 ReservedSlotIndex,
		const FVector& LaunchOrigin,
		float FlightSpeed,
		float ArcHeight,
		AActor* ThrowInstigator) = 0;

	virtual void ConfirmLatchedToThrowTarget(UMinionThrowTargetComponent* TargetComp, int32 SlotIndex) = 0;
	virtual void DetachFromThrowTarget(const FVector& PopVelocity, bool bSnapWithoutPop = false) = 0;
	virtual UMinionThrowTargetComponent* GetActiveThrowTarget() const = 0;

	// --- Local Interactable Stranded Anchor API ---
	virtual bool HasInteractableWaitAnchor() const = 0;
	virtual FVector GetInteractableWaitAnchor() const = 0;
	virtual void ClearInteractableWaitAnchor() = 0;

	// --- Foolproof Follow, Corridor, Anti-Stuck & Throw Grace API ---
	/** Scales Detour Crowd separation down on thin bridges/corridors so minions don't shove each other off edges. */
	virtual void SetTightCorridorMode(bool bTight) {}

	/** Adjusts MaxWalkSpeed smoothly based on slot distance and leader speed (no Tick required). */
	virtual void UpdateFollowSpeedForDistance(float DistanceToSlot, float LeaderSpeed) {}

	/** Tiered anti-stuck recovery (1 = brief crowd pause + repath, 2 = unsnag hop, 3 = safe catch-up warp). */
	virtual void ExecuteUnstuckRecovery(int32 RecoveryTier, const FVector& TargetLocation) {}

	/** Returns false during post-bounce grace period or busy states so rapid throws cycle across the squad. */
	virtual bool CanBeThrown() const { return true; }
};