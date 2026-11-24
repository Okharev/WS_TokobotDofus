// ActionMovementConfig.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ActionMovementConfig.generated.h"

UCLASS(BlueprintType)
class WS_TOKOBOTDOFUS_API UActionMovementConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Ground Locomotion & Accel/Decel ---
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
	float MaxRunSpeed = 620.f;

	UPROPERTY(EditDefaultsOnly, Category = "Locomotion")
	float StrafeSpeed = 420.f;

	/** Speed ramp-up rate on ground (cm/s^2). Lower values give weightier startup momentum. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|AccelDecel", meta = (ClampMin = "100.0"))
	float GroundAcceleration = 1800.f;

	/** Braking rate when releasing input or slowing to a lower stick tilt (cm/s^2). */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|AccelDecel", meta = (ClampMin = "100.0"))
	float GroundDeceleration = 2600.f;

	/** Multiplier applied to GroundAcceleration when reversing direction (Dot < 0) so pivots stay snappy. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|AccelDecel", meta = (ClampMin = "1.0"))
	float PivotTurnBoost = 2.6f;

	/** Horizontal steering acceleration while airborne (cm/s^2). */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|AccelDecel", meta = (ClampMin = "0.0"))
	float AirAcceleration = 1400.f;

	/** Horizontal braking when releasing stick mid-air (cm/s^2). Set to 0 for pure momentum carry. */
	UPROPERTY(EditDefaultsOnly, Category = "Locomotion|AccelDecel", meta = (ClampMin = "0.0"))
	float AirDeceleration = 350.f;

	// --- Slopes & Steps ---
	UPROPERTY(EditDefaultsOnly, Category = "Slopes")
	float MaxStepHeight = 42.f;

	UPROPERTY(EditDefaultsOnly, Category = "Slopes", meta = (ClampMin = "10.0", ClampMax = "75.0"))
	float MaxWalkableSlopeAngle = 46.f;

	/** Max speed multiplier at full walkable uphill grade (1.0 = constant OoT speed; <1.0 = uphill resistance). */
	UPROPERTY(EditDefaultsOnly, Category = "Slopes", meta = (ClampMin = "0.4", ClampMax = "1.2"))
	float UphillSpeedMultiplier = 1.0f;

	/** Max speed multiplier at full walkable downhill grade (1.0 = constant OoT speed; >1.0 = downhill boost). */
	UPROPERTY(EditDefaultsOnly, Category = "Slopes", meta = (ClampMin = "0.8", ClampMax = "1.5"))
	float DownhillSpeedMultiplier = 1.0f;

	/** Optional accel bonus downhill / penalty uphill at max grade (cm/s^2). Keep 0.0 for uniform feel. */
	UPROPERTY(EditDefaultsOnly, Category = "Slopes", meta = (ClampMin = "0.0"))
	float SlopeAccelInfluence = 0.f;

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

	FORCEINLINE float GetRiseGravity() const
	{
		return (2.f * JumpApexHeight) / FMath::Square(FMath::Max(TimeToApex, 0.05f));
	}

	FORCEINLINE float GetJumpInitialVelocity() const
	{
		return (2.f * JumpApexHeight) / FMath::Max(TimeToApex, 0.05f);
	}
};