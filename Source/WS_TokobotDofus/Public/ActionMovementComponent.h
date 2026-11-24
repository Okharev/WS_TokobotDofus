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

	UFUNCTION(BlueprintCallable, Category = "ActionMovement")
	void RequestJump();

	UFUNCTION(BlueprintCallable, Category = "ActionMovement")
	void ReleaseJump();

	UFUNCTION(BlueprintPure, Category = "ActionMovement")
	bool IsGrounded() const { return bIsGrounded; }

	void SetLockedTarget(AActor* NewTarget);
	void SetVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride);
	void LaunchPawn(const FVector& LaunchVelocity, bool bPreserveAirMomentum = false);

	FOnCarrierGroundedChanged OnGroundedStateChanged;
	FOnCarrierJumpIntercept OnJumpIntercept;
	FOnCarrierWallHit OnBlockingWallHit;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Config")
	TObjectPtr<UActionMovementConfig> Config;

private:
	void IntegratePlanarVelocity(float DeltaTime, const FVector& InputDir);
	void IntegrateVerticalVelocity(float DeltaTime);
	void MoveWithCollideAndSlide(const FVector& DeltaMove);
	bool TryStepUp(const FVector& RemainingMove, const FHitResult& WallHit);
	void ProbeAndSnapToFloor(bool bWasGroundedBeforeMove);
	void UpdatePawnRotation(float DeltaTime, const FVector& InputDir);
	void ExecuteJump();

	bool IsWalkableFloor(const FHitResult& Hit) const;

	/**
	 * Projects a 2D heading vertically onto a slope plane so diagonal movement
	 * preserves its exact XY heading and 3D surface speed (no cosine/normal skew).
	 */
	static FVector ProjectDirectionOntoSlope(const FVector& HorizontalDir, const FVector& SlopeNormal);

	bool bIsGrounded = false;
	bool bIsJumping = false;
	bool bPreserveMomentumInAir = false;
	bool bLaunchedDuringMove = false;

	bool bVelocityOverrideActive = false;
	FVector OverridePlanarVelocity = FVector::ZeroVector;

	double LastGroundedTime = -10.0;
	double LastJumpRequestTime = -10.0;
	FVector GroundNormal = FVector::UpVector;

	TWeakObjectPtr<AActor> LockedTarget;
};