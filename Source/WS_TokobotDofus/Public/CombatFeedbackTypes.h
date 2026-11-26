// CombatFeedbackTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "CombatFeedbackTypes.generated.h"

class UCameraShakeBase;

/** Designer-tunable impact feel parameters (assignable per weapon/stance). */
USTRUCT(BlueprintType)
struct FHitFeedbackConfig
{
	GENERATED_BODY()

	// --- Damage & Knockback ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1"))
	int32 HeartDamage = 1;

	/** Horizontal knockback speed (cm/s) applied right as HitStop releases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackHorizontalStrength = 520.0f;

	/** Small upward pop (cm/s) so floor friction doesn't eat the knockback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackVerticalStrength = 210.0f;

	/** Duration (seconds) the enemy is staggered and unable to attack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Knockback", meta = (ClampMin = "0.0", Units = "s"))
	float StaggerDuration = 0.45f;

	// --- HitStop (Time Freeze) ---
	/** Freeze duration in real-world seconds for a standard hit (~4 frames at 60fps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop",
		meta = (ClampMin = "0.0", ClampMax = "0.5", Units = "s"))
	float HitStopDuration = 0.065f;

	/** Heavier freeze duration in real-world seconds on a killing blow (~8-9 frames at 60fps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop",
		meta = (ClampMin = "0.0", ClampMax = "0.6", Units = "s"))
	float KillHitStopDuration = 0.14f;

	/** GlobalTimeDilation during HitStop (UE5 clamps 0.0 to ~0.0001; 0.02 keeps motion ultra-crisp). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitStop", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float HitStopTimeDilation = 0.02f;

	// --- Camera Shake ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> HitCameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> KillCameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.0f;
};

/** Full runtime context passed to Blueprint VFX, SFX, UI, and Animation listeners. */
USTRUCT(BlueprintType)
struct FCombatHitPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> InstigatorActor = nullptr; // Player

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> DamageCauser = nullptr; // Striking Minion

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> TargetActor = nullptr; // Hit Enemy

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector ImpactNormal = FVector::UpVector;

	/** Normalized 2D direction pushing the target away from the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector KnockbackDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector KnockbackVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 HeartDamage = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 RemainingHearts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 MaxHearts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 WhipChainIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float StaggerDuration = 0.45f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float AppliedHitStopDuration = 0.065f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsKillingBlow = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FHitResult HitResult;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName AttackType = NAME_None; // e.g., "Whip", "CarryRush", "SpinAttack"
};

/** Returned synchronously by the victim so the attacker knows whether to trigger normal vs. lethal feedback. */
USTRUCT(BlueprintType)
struct FCombatHitResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bHitAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bWasKillingBlow = false;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 RemainingHearts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 MaxHearts = 0;
};
