// SquadCarrierInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Engine/HitResult.h"
#include "SquadCarrierInterface.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;

/** Fired on grounded <-> airborne transitions (event-driven replacement for ACharacter::LandedDelegate). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCarrierGroundedChanged, bool /*bIsGrounded*/);

/** Fired before a normal jump executes; return true to consume the jump (e.g., CarryRush Momentum Jump). */
DECLARE_DELEGATE_RetVal(bool, FOnCarrierJumpIntercept);

/** Fired when the kinematic capsule sweep hits a vertical blocking surface (replaces extra wall traces). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnCarrierWallHit, AActor* /*OtherActor*/, const FHitResult& /*Hit*/);

UINTERFACE(MinimalAPI)
class USquadCarrierInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Decouples UMinionSquadComponent stances from ACharacter / UCharacterMovementComponent.
 * Implemented by AZeldaPawn and forwarded directly to UActionMovementComponent.
 */
class WS_TOKOBOTDOFUS_API ISquadCarrierInterface
{
	GENERATED_BODY()

public:
	virtual bool IsCarrierGrounded() const = 0;
	virtual UCapsuleComponent* GetCarrierCapsule() const = 0;
	virtual USkeletalMeshComponent* GetCarrierMesh() const = 0;

	/** Overrides planar locomotion directly (used during CarryRush without hacking MaxWalkSpeed/MaxAcceleration). */
	virtual void SetCarrierVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride) = 0;

	/** Launches the carrier; bPreserveAirMomentum disables horizontal air drag until touchdown. */
	virtual void LaunchCarrier(const FVector& LaunchVelocity, bool bPreserveAirMomentum = false) = 0;

	virtual FOnCarrierGroundedChanged& GetOnCarrierGroundedChanged() = 0;
	virtual FOnCarrierJumpIntercept& GetOnCarrierJumpIntercept() = 0;
	virtual FOnCarrierWallHit& GetOnCarrierWallHit() = 0;
};