// BoarEnemyTypes.h
#pragma once

#include "CoreMinimal.h"
#include "CombatFeedbackTypes.h"
#include "BoarEnemyTypes.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class EBoarAIState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Patrol UMETA(DisplayName = "Patrol"),
	ReturnToZone UMETA(DisplayName = "Return To Zone"),
	ChargeUp UMETA(DisplayName = "Charge Up"),
	Rushing UMETA(DisplayName = "Rushing"),
	Recovering UMETA(DisplayName = "Recovering"),
	StunnedVulnerable UMETA(DisplayName = "Stunned (Spin Vulnerable)")
};

/** All designer-facing parameters for the Boar mob in one place. */
USTRUCT(BlueprintType)
struct FBoarEnemyTuning
{
	GENERATED_BODY()

	// --- Perception & Aggro (mirrors FBirdEnemyTuning) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "100.0", Units = "cm"))
	float DetectionRange = 1100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "100.0", Units = "cm"))
	float AggroDropRange = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception",
		meta = (ClampMin = "10.0", ClampMax = "360.0", Units = "deg"))
	float TotalVisionConeDegrees = 160.f; // Automatically halved for SightConfig

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception")
	FName TargetTag = FName("Player");

	// --- Patrol & Ground Movement ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "100.0", Units = "cm"))
	float PatrolRadius = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "10.0", Units = "cm/s"))
	float PatrolSpeed = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "10.0", Units = "cm/s"))
	float ReturnSpeed = 360.f;

	// --- Charge-Up Telegraph ---
	/** Total time the Boar stares at the player and paws the ground before launching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|ChargeUp", meta = (ClampMin = "0.2", Units = "s"))
	float ChargeUpDuration = 1.25f;

	/** Final fraction of ChargeUpDuration where aim locks so the player can sidestep or line up a counter-rush. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|ChargeUp", meta = (ClampMin = "0.0", Units = "s"))
	float AimLockFreezeWindow = 0.25f;

	/** Yaw rotation speed while tracking the player during ChargeUp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|ChargeUp",
		meta = (ClampMin = "30.0", Units = "deg/s"))
	float ChargeUpTurnRate = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|ChargeUp")
	TObjectPtr<UAnimMontage> ChargeUpMontage = nullptr;

	// --- Rush Execution ---
	/** Fixed horizontal travel distance of the rush before stopping (also stops early on wall impact). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "100.0", Units = "cm"))
	float RushDistance = 950.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "200.0", Units = "cm/s"))
	float RushSpeed = 1150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "15.0", Units = "cm"))
	float RushHitRadius = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "0.0", Units = "cm"))
	float RushHitForwardLength = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "0.0"))
	float BaseRushDamage = 10.f;

	/** Cooldown / skid recovery pause after finishing a rush before starting the next ChargeUp. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Rush", meta = (ClampMin = "0.1", Units = "s"))
	float PostRushRecoveryDuration = 0.9f;

	/** Knockback, HitStop, heart damage, and camera shake when the Boar's rush hits the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|BOTWFeedback")
	FHitFeedbackConfig RushHitFeedback;

	// --- CarryRush Clash & Spin Attack Vulnerability ---
	/** Minimum player Carry Rush horizontal speed (cm/s) required to cancel the Boar's rush and stun it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Clash", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinPlayerClashSpeed = 650.f;

	/** Minimum head-on opposing dot magnitude (0.35 = ~110 deg+ opposing) so side-grazes do not trigger a clash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Clash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinClashHeadOnDot = 0.35f;

	/** How long the Boar remains dizzy and vulnerable to the player's Spin Attack after a clash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Clash", meta = (ClampMin = "0.5", Units = "s"))
	float ClashStunDuration = 4.0f;

	/** Horizontal bounce-back velocity applied to both the Boar and Player on a valid clash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Clash", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ClashRecoilSpeed = 420.f;

	/** HitStop / camera shake / sparks played on a successful head-on clash (0 damage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Clash")
	FHitFeedbackConfig ClashFeedback;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Vulnerability")
	FName SpinAttackTypeName = FName("SpinAttack");

	/** If true, the Boar's hide deflects damage unless it is stunned from a clash. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Vulnerability")
	bool bArmoredWhenNotStunned = true;

	/** If true, only hits whose FCombatHitPayload::AttackType == SpinAttackTypeName can damage the stunned Boar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Vulnerability")
	bool bRequireSpinAttackWhileStunned = true;

	/** Extra hearts stripped when punished by a Spin Attack during the stunned vulnerability window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Vulnerability", meta = (ClampMin = "0"))
	int32 SpinAttackBonusHearts = 1;

	/** If true, landing a Spin Attack immediately wakes the Boar out of its stunned state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Vulnerability")
	bool bEndStunOnSpinAttackHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bDrawDebugTraces = false;
};

/** Thread-safe snapshot updated every tick for the Boar Animation Blueprint. */
USTRUCT(BlueprintType)
struct FBoarAnimState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	float GroundSpeed = 0.f;

	/** 0.0 (Idle) to 1.0 (Full Rush Speed). Useful for Locomotion BlendSpaces. */
	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	float NormalizedSpeed = 0.f;

	/** 0.0 to 1.0 progress during ChargeUp for pawing intensity, lean, or emissive/dust ramp. */
	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	float ChargeUpAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	bool bIsAggroed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	bool bIsChargingUp = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	bool bIsRushing = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	bool bIsStunnedVulnerable = false;

	UPROPERTY(BlueprintReadOnly, Category = "Boar|Animation")
	EBoarAIState CurrentAIState = EBoarAIState::Idle;
};
