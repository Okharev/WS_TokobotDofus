// ZeldaPawn.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SquadCarrierInterface.h"
#include "ZeldaPawn.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UActionMovementComponent;

/**
 * Lightweight, zero-tick Pawn shell replacing ACharacter.
 * Delegates all locomotion to UActionMovementComponent and bridges to UMinionSquadComponent via ISquadCarrierInterface.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API AZeldaPawn : public APawn, public ISquadCarrierInterface
{
	GENERATED_BODY()

public:
	AZeldaPawn();

	virtual UPawnMovementComponent* GetMovementComponent() const override;

	UFUNCTION(BlueprintPure, Category = "Movement")
	UActionMovementComponent* GetActionMovement() const { return MovementComp; }

	// ~Begin ISquadCarrierInterface
	virtual bool IsCarrierGrounded() const override;
	virtual UCapsuleComponent* GetCarrierCapsule() const override { return CapsuleComp; }
	virtual USkeletalMeshComponent* GetCarrierMesh() const override { return MeshComp; }
	virtual void SetCarrierVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride) override;
	virtual void LaunchCarrier(const FVector& LaunchVelocity, bool bPreserveAirMomentum = false) override;
	virtual FOnCarrierGroundedChanged& GetOnCarrierGroundedChanged() override;
	virtual FOnCarrierJumpIntercept& GetOnCarrierJumpIntercept() override;
	virtual FOnCarrierWallHit& GetOnCarrierWallHit() override;
	// ~End ISquadCarrierInterface

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCapsuleComponent> CapsuleComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UActionMovementComponent> MovementComp;
};