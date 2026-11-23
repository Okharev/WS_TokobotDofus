#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ActionMovementConfig.generated.h"

UCLASS(BlueprintType)
class UActionMovementConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // --- Ground Locomotion ---
    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float MaxRunSpeed = 620.f;

    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float StrafeSpeed = 420.f;

    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float GroundAcceleration = 3800.f;

    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float GroundDeceleration = 4500.f;

    /** Multiplier when reversing direction (Dot < 0) to eliminate wide sluggish turns. */
    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float PivotTurnBoost = 2.3f;

    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float MaxStepHeight = 42.f;

    UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
    float MaxWalkableSlopeAngle = 46.f;

    // --- Analytical Jump ---
    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float JumpApexHeight = 165.f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float TimeToApex = 0.34f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float FallGravityMultiplier = 2.35f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float ApexHangSpeedThreshold = 110.f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float ApexGravityMultiplier = 0.55f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float ApexAirControlBoost = 1.25f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float JumpCutVelocityScale = 0.45f;

    UPROPERTY(EditDefaultsOnly, Category = "Jump")
    float MaxFallSpeed = 2400.f;

    // --- Forgiveness Windows ---
    UPROPERTY(EditDefaultsOnly, Category = "Forgiveness")
    float CoyoteTime = 0.11f;

    UPROPERTY(EditDefaultsOnly, Category = "Forgiveness")
    float JumpBufferWindow = 0.13f;

    /** Analytical helpers — always in sync with designer metrics. */
    FORCEINLINE float GetRiseGravity() const
    {
        return (2.f * JumpApexHeight) / FMath::Square(FMath::Max(TimeToApex, 0.05f));
    }

    FORCEINLINE float GetJumpInitialVelocity() const
    {
        return (2.f * JumpApexHeight) / FMath::Max(TimeToApex, 0.05f);
    }
};