#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MinionLocomotionJuiceComponent.generated.h"

class USceneComponent;
class USquashStretchComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinionHardBrakeSignature, FVector, BrakeDirection);

/**
 * Adds organic banking on turns, acceleration/braking pitch lean, and subtle gait waddle.
 * Safe to pair with USquashStretchComponent (only modifies RelativeRotation, never Scale).
 */
UCLASS(ClassGroup = (Juice), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionLocomotionJuiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMinionLocomotionJuiceComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Fires when a running minion brakes or reverses sharply (bind dust VFX / foot skid SFX here). */
	UPROPERTY(BlueprintAssignable, Category = "Juice|Locomotion")
	FOnMinionHardBrakeSignature OnHardBrake;

	/** Read these in AnimBP / ControlRig if driving spine bones instead of whole-mesh rotation. */
	UFUNCTION(BlueprintPure, Category = "Juice|Locomotion")
	float GetCurrentBankRoll() const { return CurrentBankRoll; }

	UFUNCTION(BlueprintPure, Category = "Juice|Locomotion")
	float GetCurrentLeanPitch() const { return CurrentLeanPitch; }

	/** Suspend tilting while locked in Chaos Whip, CarryRush, ThrowFlight, or LatchedToTarget. */
	UFUNCTION(BlueprintCallable, Category = "Juice|Locomotion")
	void SetLocomotionJuiceEnabled(bool bEnabled);

protected:
	/** If false, only computes BankRoll/LeanPitch for AnimBP/ControlRig without rotating GetMesh(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Locomotion")
	bool bApplyToOwnerMeshRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Banking", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float MaxBankRollDegrees = 22.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Lean", meta = (ClampMin = "0.0", ClampMax = "35.0"))
	float MaxForwardLeanDegrees = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Lean", meta = (ClampMin = "0.0", ClampMax = "35.0"))
	float MaxBrakeLeanBackDegrees = 20.0f;

	/** Forward acceleration (cm/s^2) needed to reach max lean. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Lean")
	float ReferenceAcceleration = 1400.0f;

	/** Negative forward acceleration threshold that triggers OnHardBrake + optional squash pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Lean")
	float HardBrakeAccelThreshold = -1350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Waddle")
	float WaddleFrequency = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Smoothing")
	float TiltInterpSpeed = 11.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Smoothing")
	float ReferenceMaxSpeed = 520.0f;
	
	
	// Update these defaults / add deadzone & filter tuning in protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Banking", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BankYawRateDeadzone = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Banking")
	float BankSensitivity = 0.065f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Waddle", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float MaxWaddleRollDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Smoothing")
	float InputFilterSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Lean")
	bool bTriggerSquashOnHardBrake = false;

	// Add to private:
	FVector SmoothedVel2D = FVector::ZeroVector;
	FVector PrevSmoothedVel2D = FVector::ZeroVector;
	float SmoothedYawRate = 0.0f;
	float RecentPeakSpeed = 0.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TargetMesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USquashStretchComponent> CachedSquashComp = nullptr;

	FQuat BaseMeshRelQuat = FQuat::Identity;
	FVector PrevVelocity2D = FVector::ZeroVector;
	float PrevYaw = 0.0f;

	float CurrentBankRoll = 0.0f;
	float CurrentLeanPitch = 0.0f;
	float WaddlePhase = 0.0f;
	float BrakeCooldown = 0.0f;
	bool bJuiceEnabled = true;
};