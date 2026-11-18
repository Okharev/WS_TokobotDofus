// MinionAgentInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MinionAgentInterface.generated.h"

class ANavLinkProxy;

/**
 * High-level behavioral states for a Pikmin/Tokobot-style minion.
 */
UENUM(BlueprintType)
enum class EMinionState : uint8
{
	Idle               UMETA(DisplayName = "Idle"),
	Following          UMETA(DisplayName = "Following Squad"),
	EvadingPlayer      UMETA(DisplayName = "Evading Player"),
	TraversingNavLink  UMETA(DisplayName = "Traversing NavLink"),
	InAttackWhip       UMETA(DisplayName = "Locked in Chaos Attack Whip"),
	InCarryRush        UMETA(DisplayName = "Carrying Player in Palanquin")
};

UINTERFACE(MinimalAPI, Blueprintable)
class UMinionAgentInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface implemented by minion characters to receive squad, avoidance, NavLink, Whip, and Carry commands.
 */
class IMinionAgentInterface
{
	GENERATED_BODY()

public:
	/** Returns the minion's current behavioral state. */
	virtual EMinionState GetMinionState() const = 0;

	/** Commands the minion to pathfind to its assigned world-space formation slot. */
	virtual bool MoveToSquadSlot(const FVector& ProjectedSlotLocation) = 0;

	/** Commands the minion to immediately sidestep out of the player's path. */
	virtual void EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight) = 0;

	/** Commands the minion to suspend crowd steering and jump across a Smart NavLink. */
	virtual void ExecuteNavLinkJump(ANavLinkProxy* NavLink, const FVector& LaunchVelocity) = 0;

	/** Enables or disables Chaos physics simulation on the minion for the Damped Chain Whip stance. */
	virtual void SetWhipModeEnabled(bool bEnabled) = 0;

	/** Enables or disables kinematic Carry Palanquin mode on the minion. */
	virtual void SetCarryRushModeEnabled(bool bEnabled) = 0;
};