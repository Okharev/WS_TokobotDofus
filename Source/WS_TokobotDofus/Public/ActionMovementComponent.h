// ActionMovementComponent.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Engine/HitResult.h"
#include "SquadCarrierInterface.h"
#include "ActionMovementComponent.generated.h"

class UActionMovementConfig;

UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UActionMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UActionMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Event-Driven Locomotion API (called by Input / Targeting / Squad Stances) ---

	void SetLockedTarget(AActor* NewTarget);
	
	UFUNCTION(BlueprintCallable, Category = "ActionMovement")
	void RequestJump();

	UFUNCTION(BlueprintCallable, Category = "ActionMovement")
	void ReleaseJump();

	UFUNCTION(BlueprintPure, Category = "ActionMovement")
	bool IsGrounded() const { return bIsGrounded; }
	

	/** Directly drives planar velocity (e.g., CarryRush charge) while keeping slope projection and collision intact. */
	void SetVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride);

	/** Launches the pawn cleanly; bPreserveAirMomentum suppresses horizontal drag until landing. */
	void LaunchPawn(const FVector& LaunchVelocity, bool bPreserveAirMomentum = false);


	// Shared delegate instances exposed through ISquadCarrierInterface
	FOnCarrierGroundedChanged OnGroundedStateChanged;
	FOnCarrierJumpIntercept OnJumpIntercept;
	FOnCarrierWallHit OnBlockingWallHit;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Config")
	TObjectPtr<UActionMovementConfig> Config;

private:
	void ProbeGround(FHitResult& OutGroundHit);
	void IntegratePlanarVelocity(float DeltaTime, const FVector& InputDir);
	void IntegrateVerticalVelocity(float DeltaTime);
	void MoveWithStepUpAndSlide(const FVector& DeltaMove);
	void UpdatePawnRotation(float DeltaTime, const FVector& InputDir);
	void ExecuteJump();

	bool bIsGrounded = false;
	bool bIsJumping = false;
	bool bPreserveMomentumInAir = false;

	bool bVelocityOverrideActive = false;
	FVector OverridePlanarVelocity = FVector::ZeroVector;

	double LastGroundedTime = -10.0;
	double LastJumpRequestTime = -10.0;
	FVector GroundNormal = FVector::UpVector;

	TWeakObjectPtr<AActor> LockedTarget;
};