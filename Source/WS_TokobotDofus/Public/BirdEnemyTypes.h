// BirdEnemyTypes.h
#pragma once

#include "CoreMinimal.h"
#include "CombatFeedbackTypes.h"
#include "BirdEnemyTypes.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class EBirdAIState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Patrol UMETA(DisplayName = "Patrol"),
	ReturnToZone UMETA(DisplayName = "Return To Zone"),
	Chase UMETA(DisplayName = "Chase"),
	Attack UMETA(DisplayName = "Attack")
};

/** All designer-facing parameters in one place. Editing these updates AI Perception, Movement, and Hover automatically. */
USTRUCT(BlueprintType)
struct FBirdEnemyTuning
{
	GENERATED_BODY()

	// --- Hover & Visual Feel ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = "0.0", Units = "cm"))
	float HoverHeight = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = "0.1"))
	float HoverHeightInterpSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = "0.0", Units = "cm"))
	float BobAmplitude = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (ClampMin = "0.0"))
	float BobFrequency = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover",
		meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "deg"))
	float MaxBankAngle = 25.f;

	// --- Perception & Aggro ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "100.0", Units = "cm"))
	float DetectionRange = 1000.f; // 10m

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "100.0", Units = "cm"))
	float AggroDropRange = 1100.f; // 11m

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception",
		meta = (ClampMin = "10.0", ClampMax = "360.0", Units = "deg"))
	float TotalVisionConeDegrees = 180.f; // Automatically halved to 90 deg for SightConfig

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception")
	FName TargetTag = FName("Player");

	// --- Patrol & Movement ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "100.0", Units = "cm"))
	float PatrolRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "10.0", Units = "cm/s"))
	float PatrolSpeed = 280.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "10.0", Units = "cm/s"))
	float ChaseSpeed = 420.f;

	// --- Combat ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "50.0", Units = "cm"))
	float AttackRange = 200.f; // 2m

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "25.0", Units = "cm"))
	float AttackAcceptanceRadius = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "s"))
	float AttackCooldown = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float BaseDamage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "5.0", Units = "cm"))
	float AttackTraceRadius = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackTraceDistance = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UAnimMontage> AttackMontage = nullptr;

	/** Used automatically when no AnimBP or AttackMontage is assigned yet (greyboxing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.1", Units = "s"))
	float FallbackAttackDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bDrawDebugTraces = false;

	/** knockback, HitStop, and camera shake applied when this bird hits the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|BOTWFeedback")
	FHitFeedbackConfig AttackFeedback;

	/** Telegraph delay (seconds) before the hitbox activates when AttackMontage is unassigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Fallback", meta = (ClampMin = "0.0", Units = "s"))
	float FallbackWindupDuration = 0.25f;

	/** Active hit-trace window (seconds) when AttackMontage is unassigned. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Fallback", meta = (ClampMin = "0.05", Units = "s"))
	float FallbackActiveDuration = 0.15f;
};

/** Thread-safe snapshot updated every tick for Animation Blueprints (BlendSpaces, Banking, State transitions). */
USTRUCT(BlueprintType)
struct FBirdAnimState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	float GroundSpeed = 0.f;

	/** 0.0 (Hovering Idle) to 1.0 (Full Chase Speed). Ideal for a 1D Wing-Flap BlendSpace. */
	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	float NormalizedSpeed = 0.f;

	/** Roll angle in degrees (-MaxBankAngle to +MaxBankAngle) driven by turn rate. */
	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	float BankAngle = 0.f;

	/** Vertical hover bob velocity for subtle wing-beat emphasis. */
	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	float BobVelocity = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	bool bIsAggroed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Bird|Animation")
	EBirdAIState CurrentAIState = EBirdAIState::Idle;
};
