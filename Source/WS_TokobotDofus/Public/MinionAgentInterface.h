// MinionAgentInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MinionAgentInterface.generated.h"

class ANavLinkProxy;

/**
 * High-level behavioral states for a Pikmin-style minion.
 */
UENUM(BlueprintType)
enum class EMinionState : uint8
{
	Idle               UMETA(DisplayName = "Idle"),
	Following          UMETA(DisplayName = "Following Squad"),
	EvadingPlayer      UMETA(DisplayName = "Evading Player"),
	TraversingNavLink  UMETA(DisplayName = "Traversing NavLink"),
	InAttackWhip       UMETA(DisplayName = "Plucked in Attack Whip"),
	InCarryRush        UMETA(DisplayName = "Carrying Player in Rush")
};

UINTERFACE(MinimalAPI, Blueprintable)
class UMinionAgentInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface implemented by minion characters to receive squad, avoidance, NavLink, Whip, and Carry Rush commands.
 */
class IMinionAgentInterface
{
	GENERATED_BODY()

public:
	virtual EMinionState GetMinionState() const = 0;
	virtual bool MoveToSquadSlot(const FVector& ProjectedSlotLocation) = 0;
	virtual void EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight) = 0;
	virtual void ExecuteNavLinkJump(ANavLinkProxy* NavLink, const FVector& LaunchVelocity) = 0;
	virtual void SetWhipModeEnabled(bool bEnabled) = 0;

	/** Enables or disables kinematic Carry Rush mode on the minion. */
	virtual void SetCarryRushModeEnabled(bool bEnabled) = 0;
};