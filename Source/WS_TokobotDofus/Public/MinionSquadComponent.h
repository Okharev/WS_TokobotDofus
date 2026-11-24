// MinionSquadComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/HitResult.h"
#include "RushClashInterface.h"
#include "UObject/ScriptInterface.h"
#include "CombatFeedbackTypes.h"
#include "MinionSquadComponent.generated.h"

class USphereComponent;
class UPhysicsConstraintComponent;
class UDamageType;
class IMinionAgentInterface;
class UMinionSquadComponent;
class UNavigationSystemV1;

/**
 * High-level Stance modes for the Minion Squad.
 */
UENUM(BlueprintType)
enum class EMinionSquadStance : uint8
{
	Follow      UMETA(DisplayName = "Follow Formation (NavMesh)"),
	AttackWhip  UMETA(DisplayName = "Attack Stance (Damped Stiff Chain)"),
	CarryRush   UMETA(DisplayName = "Carry Stance (Transport & Rush)")
};

/**
 * Customizable parameters for the Damped, Stiff Chaos Physics Chain Whip / Tornado.
 */
USTRUCT(BlueprintType)
struct FMinionWhipConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode")
	bool bDualArmWhip = true;

	/** If true, reachable minions walk on the NavMesh to their whip link positions before locking into Chaos chains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WalkInTransition")
	bool bWalkIntoWhipFormation = true;

	/** Distance (in cm) within which a minion is considered to have arrived at its whip link slot during walk-in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WalkInTransition", meta = (EditCondition = "bWalkIntoWhipFormation"))
	float WalkInArrivalRadius = 65.0f;

	/** Maximum time (in seconds) to wait for reachable minions to walk into whip formation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WalkInTransition", meta = (EditCondition = "bWalkIntoWhipFormation"))
	float MaxWalkInDuration = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	FVector RightArmLocalOffset = FVector(0.0f, 65.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	FVector LeftArmLocalOffset = FVector(0.0f, -65.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	float RightArmRestYawOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	float LeftArmRestYawOffset = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints")
	float StartOffsetFromArm = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints")
	float LinkSpacing = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "5.0", ClampMax = "75.0"))
	float MaxJointBendAngleDegrees = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "50.0", ClampMax = "10000.0"))
	float AngularBendStiffness = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "10.0", ClampMax = "2000.0"))
	float AngularBendDamping = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float LinearConstraintSlack = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "100.0", ClampMax = "20000.0"))
	float LinearSpringStiffness = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "10.0", ClampMax = "3000.0"))
	float LinearSpringDamping = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DampedChainConstraints", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float ChaosLinearDamping = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightStabilization", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float MaxVerticalSlack = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightStabilization", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float VerticalHeightStiffness = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeightStabilization", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float VerticalHeightDamping = 16.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TornadoSpin")
	float TornadoWhipForce = 3600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TornadoSpin", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float SpinRevolutions = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TornadoSpin", meta = (ClampMin = "0.15", ClampMax = "5.0"))
	float SpinDuration = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TornadoSpin")
	float ContinuousTornadoSpeedDegPerSec = 680.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TornadoSpin")
	float SpinDirection = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float BaseDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float BonusDamagePerLink = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float HitSphereRadius = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float HitCooldownPerTarget = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TEnumAsByte<ECollisionChannel> HitTraceChannel = ECC_Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	bool bApplyEnginePointDamage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TSubclassOf<UDamageType> DamageTypeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float MinPhysicsSpeedForDamage = 650.0f;
	
	/**  1-heart damage, knockback, HitStop freeze, and camera shake tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|BOTWFeedback")
	FHitFeedbackConfig HitFeedback;
};

/**
 * Customizable parameters for the Carry Transport Stance, Triggered Rush Charge, and Momentum Jump.
 */
USTRUCT(BlueprintType)
struct FMinionCarryRushConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float GatherDuration = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation", meta = (ClampMin = "25.0", ClampMax = "120.0"))
	float PalanquinRowSpacing = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation", meta = (ClampMin = "25.0", ClampMax = "120.0"))
	float PalanquinColSpacing = 48.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation")
	float PalanquinForwardOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation")
	float PlayerMeshLiftOffsetZ = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TransportFormation")
	float TransportFollowInterpSpeed = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	float InitialSpeed = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	float MaxSpeed = 1900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	float Acceleration = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	float MaxRushDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	bool bExitCarryStanceOnWallHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge", meta = (ClampMin = "0.1", ClampMax = "0.85"))
	float WallNormalZThreshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge")
	float WallBounceBackStrength = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump")
	float JumpVerticalVelocity = 680.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump", meta = (ClampMin = "0.2", ClampMax = "2.5"))
	float JumpMomentumMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump")
	float MinRushJumpForwardSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump")
	bool bKeepMinionsUnderneathDuringJump = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump")
	bool bContinueRushOnLanding = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float RamHitboxRadius = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float BaseRamDamage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float MaxSpeedBonusDamage = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float EnemyKnockbackStrength = 950.0f;
	
	/** If true, the player grabs and carries 1 minion along with them during a Carry Jump so they land with at least 1 minion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump")
	bool bCarryOneMinionDuringJump = true;

	/** Optional SkeletalMesh socket on the Player to attach the carried minion to during the jump (uses CarriedMinionLocalOffset if None). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump", meta = (EditCondition = "bCarryOneMinionDuringJump"))
	FName CarriedMinionAttachSocket = NAME_None;

	/** Local offset relative to the Player where the 1 carried minion is held during the jump (e.g. overhead or on the back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MomentumJump", meta = (EditCondition = "bCarryOneMinionDuringJump"))
	FVector CarriedMinionLocalOffset = FVector(-25.0f, 0.0f, 55.0f);
	
	/** If true, clashing head-on with a rushing enemy drops Carry Stance back to Follow so you can immediately Whip Spin Attack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RushCharge|Clash")
	bool bExitCarryStanceOnClash = true;

	/** BOTW heart damage, knockback, HitStop, and camera shake when Carry Rush rams a standard enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage|BOTWFeedback")
	FHitFeedbackConfig RamHitFeedback;
};

// ============================================================================
// MODULAR STANCE STATE CLASSES
// ============================================================================

UCLASS(Abstract)
class UMinionStanceBase : public UObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(UMinionSquadComponent* InSquad) { Squad = InSquad; }
	virtual void OnEnterStance() {}
	virtual void TickStance(float DeltaTime) {}
	virtual void OnExitStance() {}
	virtual void OnSquadRosterChanged() {}
	virtual bool RequiresComponentTick() const { return false; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UMinionSquadComponent> Squad;
};

/**
 * Stance 1: Timer-driven NavMesh formation following, player avoidance, and unreachable-player group waiting.
 */
UCLASS()
class UMinionStance_Follow : public UMinionStanceBase
{
	GENERATED_BODY()

public:
	virtual void OnEnterStance() override;
	virtual void OnExitStance() override;
	virtual void OnSquadRosterChanged() override;
	virtual bool RequiresComponentTick() const override { return false; }

	void ForceImmediateEvaluation();

private:
	void EvaluateFormationAndAvoidance();
	FVector CalculateSlotAroundTransform(int32 SlotIndex, const FTransform& AnchorTransform) const;

	/** Locked at the takeoff spot so left-behind minions stay grouped even when 1 carried minion is with the player on a ledge. */
	FTransform WaitingGroupAnchorTransform = FTransform::Identity;

	FVector LastAnchorLocation = FVector::ZeroVector;
	FRotator LastAnchorRotation = FRotator::ZeroRotator;
	bool bForceNextUpdate = true;
	int32 LastReachableMinionCount = -1;
	FTimerHandle FollowTimerHandle;
};

/**
 * Stance 2: Walk-In transition and Pooled Chaos Physics Damped Dual-Arm Whip / Tornado.
 * Operates exclusively on Squad->StanceMinions (minions that had a valid path to the player).
 */
UCLASS()
class UMinionStance_ChaosWhip : public UMinionStanceBase
{
	GENERATED_BODY()

public:
	virtual void OnEnterStance() override;
	virtual void TickStance(float DeltaTime) override;
	virtual void OnExitStance() override;
	virtual void OnSquadRosterChanged() override;
	virtual bool RequiresComponentTick() const override { return true; }

	bool StartBurstSpin();
	void SetContinuousSpin(bool bEnable);

	bool IsSpinning() const { return bIsSpinAttacking || bIsContinuousTornadoActive; }
	bool IsWalkingIntoFormation() const { return bIsWalkingIntoWhip; }

private:
	void UpdateWalkIntoWhipFormation(float DeltaTime);
	void BindPooledChaosChains();
	void ReleasePooledChaosChains();

	TArray<FVector, TInlineAllocator<16>> PrevMinionLocations;
	TArray<float, TInlineAllocator<16>> InitialMinionHeights;

	bool bIsWalkingIntoWhip = false;
	bool bPendingBurstSpinAfterWalkIn = false;
	bool bPendingContinuousSpinAfterWalkIn = false;
	float WhipWalkInElapsedTime = 0.0f;
	float WhipWalkInRepathTimer = 0.0f;

	bool bIsSpinAttacking = false;
	bool bIsContinuousTornadoActive = false;
	float SpinElapsedTime = 0.0f;
	float ContinuousTornadoAngleOffset = 0.0f;
};

/**
 * Stance 3: Non-stacking Carry Transport Palanquin, CharacterMovement-driven Rush Charge, and Momentum Jump.
 * Operates exclusively on Squad->StanceMinions (minions that had a valid path to the player).
 */
/**
 * Stance 3: Non-stacking Carry Transport Palanquin, CharacterMovement-driven Rush Charge, and Momentum Jump.
 */
UCLASS()
class UMinionStance_CarryRush : public UMinionStanceBase
{
	GENERATED_BODY()

public:
	virtual void OnEnterStance() override;
	virtual void TickStance(float DeltaTime) override;
	virtual void OnExitStance() override;
	virtual void OnSquadRosterChanged() override;
	virtual bool RequiresComponentTick() const override { return true; }

	bool StartRushCharge();
	void StopRushCharge(bool bExitCarryStance);
	bool TriggerMomentumJump();

	void HandlePlayerLanded(const FHitResult& Hit);
	void HandlePlayerWallHit(AActor* OtherActor, const FHitResult& Hit);
	void RestorePlayerAirBraking();
	void ReleaseCarriedJumpMinion();

	bool IsRushCharging() const { return bIsRushCharging; }
	bool IsCarryJumping() const { return bIsCarryJumping; }
	float GetCurrentRushSpeed() const { return CurrentRushSpeed; }
	FVector GetCarryRushDirection() const { return CarryRushDirection; }

private:
	void RestorePlayerMovementSettings();
	FVector CalculatePalanquinSlotLocation(int32 MinionIdx, int32 TotalCount, const FVector& PlayerLoc, const FVector& ForwardDir, const FVector& RightDir) const;
	void PerformFrontEnemyBumperSweep(const FVector& SweepStart, const FVector& SweepEnd);

	TArray<TWeakObjectPtr<AActor>> RushIgnoredEnemies;

	/** The single minion carried along with the player in mid-air during a Momentum Jump. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> JumpCarriedMinion = nullptr;

	bool bIsRushCharging = false;
	bool bIsCarryJumping = false;
	FVector CarryRushDirection = FVector::ForwardVector;
	float CarryStanceElapsedTime = 0.0f;
	float CarryRushChargeElapsedTime = 0.0f;
	float CurrentRushSpeed = 0.0f;

	float CachedPlayerMeshBaseZ = 0.0f;
	bool bPlayerMeshZModified = false;

	float CachedPlayerMaxWalkSpeed = 0.0f;
	float CachedPlayerMaxAcceleration = 0.0f;
	bool bPlayerMovementModified = false;

	float CachedBrakingDecelFalling = 0.0f;
	float CachedFallingLateralFriction = 0.0f;
	bool bPlayerAirBrakingModified = false;
};

// ============================================================================
// MAIN SQUAD COMPONENT
// ============================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMinionRegisteredSignature, AActor*, MinionActor, int32, SlotIndex, int32, TotalMinions);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinionUnregisteredSignature, AActor*, MinionActor, int32, TotalMinions);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSquadMoveUpdatedSignature, int32, ActiveMinionCount, FVector, AnchorWorldLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadDebugMessageSignature, const FString&, DebugMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSquadStanceChangedSignature, EMinionSquadStance, NewStance);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnWhipDamageHitSignature,
	AActor*, HitActor,
	AActor*, StrikingMinion,
	float, DamageAmount,
	int32, WhipChainIndex,
	const FHitResult&, HitResult);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWhipCombatFeedbackSignature, const FCombatHitPayload&, HitPayload);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnCarryRushEnemyHitSignature,
	AActor*, HitActor,
	float, DamageAmount,
	float, CurrentRushSpeed,
	const FHitResult&, HitResult);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnCarryRushWallImpactSignature,
	FVector, ImpactPoint,
	FVector, ImpactNormal,
	float, ImpactSpeed,
	const FHitResult&, HitResult);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCarryJumpTriggeredSignature,
	FVector, LaunchVelocity,
	float, ForwardSpeed);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCarryRushClashedSignature,
	AActor*, ClashedOpponent,
	FVector, RecoilVelocity);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionSquadComponent : public USceneComponent, public IRushClashInterface
{
	GENERATED_BODY()

	friend class UMinionStance_Follow;
	friend class UMinionStance_ChaosWhip;
	friend class UMinionStance_CarryRush;

public:
	UMinionSquadComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ~Begin Blueprint Event Dispatchers
	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnMinionRegisteredSignature OnMinionRegistered;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnMinionUnregisteredSignature OnMinionUnregistered;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnSquadMoveUpdatedSignature OnSquadMoveUpdated;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnSquadDebugMessageSignature OnSquadDebugMessage;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnSquadStanceChangedSignature OnSquadStanceChanged;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnWhipDamageHitSignature OnWhipDamageHit;
	
	/** Rich feedback event (ImpactPoint, Knockback, Hearts, bIsKillingBlow) for Whip VFX/SFX/UI. */
	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnWhipCombatFeedbackSignature OnWhipCombatFeedback;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryRushEnemyHitSignature OnCarryRushEnemyHit;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryRushWallImpactSignature OnCarryRushWallImpact;

	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryJumpTriggeredSignature OnCarryJumpTriggered;
	// ~End Blueprint Event Dispatchers

	// ~Begin Squad & Stance Blueprint API
	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	bool RegisterMinion(AActor* MinionActor);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	void UnregisterMinion(AActor* MinionActor);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	void AutoRegisterMinionsInWorld();

	/**
	 * Attempts to shift the squad into Follow, AttackWhip, or CarryRush stance.
	 * Returns false and stays in Follow if the player is isolated or fewer than MinReachableMinionsForSpecialStance (4) can reach the player!
	 */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Stance")
	bool SetSquadStance(EMinionSquadStance NewStance);

	/** Toggles between Follow and AttackWhip stance (if at least 4 minions can reach the player). */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Stance")
	EMinionSquadStance ToggleSquadStance();

	/** Toggles between Follow and Carry Transport stance (if at least 4 minions can reach the player). */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Stance")
	EMinionSquadStance ToggleCarryStance();

	/** Returns true if the player is on the NavMesh and at least MinReachableMinionsForSpecialStance (4) minions have a valid path to the player. */
	UFUNCTION(BlueprintPure, Category = "PikminSquad|Stance")
	bool CanEnterSpecialStance(int32& OutReachableMinionCount) const;

	UFUNCTION(BlueprintPure, Category = "PikminSquad|Stance")
	EMinionSquadStance GetSquadStance() const { return CurrentStance; }
	// ~End Squad & Stance Blueprint API

	// ~Begin Whip Attack Blueprint API
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	bool StartWhipSpinAttack(bool bAutoEnterAttackStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	void SetContinuousTornadoSpin(bool bEnableContinuousSpin, bool bAutoEnterAttackStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	void SetWhipConfig(const FMinionWhipConfig& NewConfig) { WhipConfig = NewConfig; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad|WhipAttack")
	const FMinionWhipConfig& GetWhipConfig() const { return WhipConfig; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad|WhipAttack")
	bool IsWhipSpinAttacking() const;

	UFUNCTION(BlueprintPure, Category = "PikminSquad|WhipAttack")
	bool IsWalkingIntoWhipFormation() const;
	// ~End Whip Attack Blueprint API

	// ~Begin Carry Transport, Rush Charge & Momentum Jump Blueprint API
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	bool StartCarryRushCharge(bool bAutoEnterCarryStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	void StopCarryRushCharge(bool bExitCarryStance = false);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	void SetCarryRushChargeActive(bool bActive, bool bAutoEnterCarryStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	bool TriggerCarryJump();

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	bool StartCarryRushSpell();

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	void StopCarryRushSpell();

	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	bool IsCarryStanceActive() const { return CurrentStance == EMinionSquadStance::CarryRush; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	bool IsCarryRushCharging() const;

	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	bool IsCarryJumping() const;

	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	float GetCurrentCarryRushSpeed() const;

	UFUNCTION(BlueprintPure, Category = "PikminSquad")
	int32 GetMinionCount() const { return ActiveMinions.Num(); }

	/** Returns the number of reachable minions currently participating in AttackWhip or CarryRush. */
	UFUNCTION(BlueprintPure, Category = "PikminSquad")
	int32 GetParticipatingMinionCount() const { return StanceMinions.Num(); }

	UFUNCTION(BlueprintPure, Category = "PikminSquad")
	const TArray<AActor*>& GetActiveMinions() const { return ActiveMinions; }
	// ~End Carry Transport, Rush Charge & Momentum Jump Blueprint API
	
	// ~Begin Throw, Recall & Checkpoint Reset API
	/** Picks the closest Idle/Following minion to ReferenceOrigin that can be thrown immediately. */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Throw")
	AActor* FindBestThrowableMinion(const FVector& ReferenceOrigin, float MaxGrabDistance = 1100.0f) const;

	/**
	 * Recalls latched minions.
	 * If bOnlyDetachIfReachable is true, minions on interactables with no NavMesh path to the player stay latched until a path exists.
	 * If false, minions detach immediately and wait around the interactable object until the player has a valid NavMesh path.
	 */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Throw")
	int32 RecallThrownMinions(bool bOnlyDetachIfReachable = false);

	/**
	 * Checkpoint reset: cleanly detaches & removes any minions from affected interactable objects (firing detach/threshold events),
	 * clears stranded anchors, and teleports the full squad onto valid NavMesh slots around the player.
	 */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Checkpoint")
	void ResetMinionsToPlayerCheckpoint(bool bDetachFromInteractables = true);

	/** Returns true if a world point has a complete NavMesh path to the player's current grounded NavMesh location. */
	UFUNCTION(BlueprintPure, Category = "PikminSquad|Throw")
	bool CanWorldLocationReachPlayer(const FVector& WorldLocation) const;
	// ~End Throw, Recall & Checkpoint Reset API
	
	// Add inside Blueprint Event Dispatchers:
	/** Fires when Carry Rush cancels head-on against a rushing enemy (e.g., Boar). Bind clash VFX/SFX/Anim here. */
	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryRushClashedSignature OnCarryRushClashed;

	// ~Begin IRushClashInterface
	virtual bool IsCarryRushing_Implementation() const override;
	virtual float GetCarryRushSpeed_Implementation() const override;
	virtual FVector GetCarryRushDirection_Implementation() const override;
	virtual void OnRushClashed_Implementation(AActor* ClashedOpponent, const FVector& RecoilVelocity) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Full squad roster visible in the Details panel during Editor Play mode (PIE). */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "PikminSquad|Runtime")
	TArray<TObjectPtr<AActor>> ActiveMinions;

	UPROPERTY(Transient)
	TArray<TScriptInterface<IMinionAgentInterface>> ActiveMinionInterfaces;

	/** Subset of ActiveMinions that had a valid path to the player when entering AttackWhip or CarryRush. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PikminSquad|Runtime")
	TArray<TObjectPtr<AActor>> StanceMinions;

	UPROPERTY(Transient)
	TArray<TScriptInterface<IMinionAgentInterface>> StanceMinionInterfaces;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PikminSquad|Runtime")
	EMinionSquadStance CurrentStance = EMinionSquadStance::Follow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config")
	bool bAutoRegisterMinionsOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxMinions = 8;

	/** Minimum number of minions that must have a valid NavMesh path to the player to allow AttackWhip or CarryRush stances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MinReachableMinionsForSpecialStance = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config")
	float UpdateInterval = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config")
	float AnchorMoveThreshold = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config")
	float PlayerEvadeRadius = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Formation")
	float RowSpacing = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Formation")
	float ColumnSpacing = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|WhipAttack", meta = (ShowOnlyInnerProperties))
	FMinionWhipConfig WhipConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|CarryRushSpell", meta = (ShowOnlyInnerProperties))
	FMinionCarryRushConfig CarryRushConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Debug")
	bool bDebugDrawSlots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Debug")
	bool bPrintDebugToScreen = true;

private:
	int32 PruneInvalidMinions();
	int32 PruneInvalidStanceMinions();

	/** Returns true if the player is grounded (not falling) and standing on a valid NavMesh polygon. */
	bool IsPlayerGroundedOnNavMesh(UNavigationSystemV1* NavSys, FVector& OutPlayerNavLoc) const;

	/** Returns true if a specific minion has a complete, non-partial NavMesh path to TargetNavLoc. */
	bool CanMinionReachNavLocation(AActor* MinionActor, UNavigationSystemV1* NavSys, const FVector& TargetNavLoc) const;

	/** Gathers only the minions from ActiveMinions that currently have a valid NavMesh path to the player. */
	int32 GatherReachableMinionsForStance(
		TArray<TObjectPtr<AActor>>& OutReachableMinions,
		TArray<TScriptInterface<IMinionAgentInterface>>& OutReachableInterfaces) const;

	void SortStanceMinionsForWhipArms();
	void UpdateHandAnchorTransforms(float ExtraOrbitYaw);
	FVector CalculateWhipLinkWorldTarget(int32 StanceMinionIndex) const;
	void PerformWhipHitSweep(AActor* MinionActor, int32 LinkIndexInArm, const FVector& StartLoc, const FVector& EndLoc);
	FVector CalculateSlotWorldLocation(int32 SlotIndex) const;
	void BroadcastDebug(const FString& Message, FColor ScreenColor = FColor::Cyan, float Duration = 3.0f) const;

	UFUNCTION()
	void OnPlayerAvoidanceOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnPlayerCarryJumpLanded(const FHitResult& Hit);

	UFUNCTION()
	void OnPlayerRushActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> AvoidanceSphere;

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> HandAnchorSpheres[2];

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicsConstraintComponent>> ConstraintPool;

	UPROPERTY(Transient)
	TObjectPtr<UMinionStance_Follow> FollowStanceInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMinionStance_ChaosWhip> WhipStanceInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMinionStance_CarryRush> CarryRushStanceInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMinionStanceBase> ActiveStanceObject;

	TMap<TWeakObjectPtr<AActor>, double> HitActorTimestamps;
};