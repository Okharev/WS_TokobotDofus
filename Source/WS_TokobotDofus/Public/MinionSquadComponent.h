// MinionSquadComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/HitResult.h"
#include "MinionSquadComponent.generated.h"

class USphereComponent;
class UPhysicsConstraintComponent;
class UDamageType;

UENUM(BlueprintType)
enum class EMinionSquadStance : uint8
{
	Follow      UMETA(DisplayName = "Follow Formation (NavMesh)"),
	AttackWhip  UMETA(DisplayName = "Attack Stance (Damped Stiff Chain)"),
	CarryRush   UMETA(DisplayName = "Carry Rush Spell (Battering Ram)")
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
};

/**
 * Customizable parameters for the "Carry Rush" Battering Ram Spell.
 */
USTRUCT(BlueprintType)
struct FMinionCarryRushConfig
{
	GENERATED_BODY()

	/** Time (in seconds) where minions rapidly bunch up under the player before launching forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GatherAndFormation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GatherDuration = 0.18f;

	/** Forward/Backward spacing (in cm) between bunched-up minions so they pack tightly without stacking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GatherAndFormation", meta = (ClampMin = "25.0", ClampMax = "120.0"))
	float PalanquinRowSpacing = 48.0f;

	/** Left/Right spacing (in cm) between bunched-up minions under the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GatherAndFormation", meta = (ClampMin = "25.0", ClampMax = "120.0"))
	float PalanquinColSpacing = 48.0f;

	/** Forward offset (in cm) of the minion cluster center relative to the player center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GatherAndFormation")
	float PalanquinForwardOffset = 15.0f;

	/** Visual Z height lift (in cm) applied to the Player's SkeletalMesh so the player stands on top of the minions! */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GatherAndFormation")
	float PlayerMeshLiftOffsetZ = 42.0f;

	/** Speed (in cm/s) when the rush first launches after gathering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float InitialSpeed = 650.0f;

	/** Maximum rush speed (in cm/s) reached at full acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MaxSpeed = 1900.0f;

	/** Forward acceleration (in cm/s^2) gained every second during the rush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float Acceleration = 1500.0f;

	/** Safety maximum duration (in seconds) before the rush ends automatically if no wall is hit (0 = infinite until wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MaxRushDuration = 5.0f;

	/** Surface ImpactNormal.Z threshold: surfaces steeper than this (e.g. < 0.5) count as walls and stop the rush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallImpact", meta = (ClampMin = "0.1", ClampMax = "0.85"))
	float WallNormalZThreshold = 0.5f;

	/** Backward/upward recoil velocity applied to the player when slamming into a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WallImpact")
	float WallBounceBackStrength = 420.0f;

	/** Radius (in cm) of the front enemy-ramming hitbox in front of the rushing cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float RamHitboxRadius = 95.0f;

	/** Base damage dealt to enemies at InitialSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float BaseRamDamage = 25.0f;

	/** Extra bonus damage added as the rush approaches MaxSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float MaxSpeedBonusDamage = 40.0f;

	/** Knockback velocity applied to enemy characters plowed through during the rush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EnemyDamage")
	float EnemyKnockbackStrength = 950.0f;
};

/** Blueprint Event Dispatchers */
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

/** Fired when the Carry Rush plows into an enemy (does NOT stop the rush). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnCarryRushEnemyHitSignature,
	AActor*, HitActor,
	float, DamageAmount,
	float, CurrentRushSpeed,
	const FHitResult&, HitResult);

/** Fired when the Carry Rush slams into a wall and stops. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnCarryRushWallImpactSignature,
	FVector, ImpactPoint,
	FVector, ImpactNormal,
	float, ImpactSpeed,
	const FHitResult&, HitResult);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionSquadComponent : public USceneComponent
{
	GENERATED_BODY()

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

	/** Bind in BP to trigger VFX/SFX or custom damage when plowing through enemies during Carry Rush! */
	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryRushEnemyHitSignature OnCarryRushEnemyHit;

	/** Bind in BP to trigger screen shake, wall crash VFX, or stun when Carry Rush hits a wall! */
	UPROPERTY(BlueprintAssignable, Category = "PikminSquad|Events")
	FOnCarryRushWallImpactSignature OnCarryRushWallImpact;
	// ~End Blueprint Event Dispatchers

	// ~Begin Squad, Stance, Whip & Carry Rush Blueprint API
	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	bool RegisterMinion(AActor* MinionActor);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	void UnregisterMinion(AActor* MinionActor);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad")
	void AutoRegisterMinionsInWorld();

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Stance")
	void SetSquadStance(EMinionSquadStance NewStance);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|Stance")
	EMinionSquadStance ToggleSquadStance();

	UFUNCTION(BlueprintPure, Category = "PikminSquad|Stance")
	EMinionSquadStance GetSquadStance() const { return CurrentStance; }

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	bool StartWhipSpinAttack(bool bAutoEnterAttackStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	void SetContinuousTornadoSpin(bool bEnableContinuousSpin, bool bAutoEnterAttackStance = true);

	UFUNCTION(BlueprintCallable, Category = "PikminSquad|WhipAttack")
	void SetWhipConfig(const FMinionWhipConfig& NewConfig) { WhipConfig = NewConfig; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad|WhipAttack")
	const FMinionWhipConfig& GetWhipConfig() const { return WhipConfig; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad|WhipAttack")
	bool IsWhipSpinAttacking() const { return bIsSpinAttacking || bIsContinuousTornadoActive; }

	/**
	 * Starts the Carry Rush Spell: minions bunch up under the player (without stacking), lift the player,
	 * and rush straight ahead gaining speed and plowing through enemies until hitting a wall.
	 */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	bool StartCarryRushSpell();

	/** Manually stops an active Carry Rush Spell and returns to Follow stance. */
	UFUNCTION(BlueprintCallable, Category = "PikminSquad|CarryRushSpell")
	void StopCarryRushSpell();

	/** Returns true if the squad is currently in the Carry Rush Spell. */
	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	bool IsCarryRushActive() const { return CurrentStance == EMinionSquadStance::CarryRush; }

	/** Returns the current speed (cm/s) of the Carry Rush. */
	UFUNCTION(BlueprintPure, Category = "PikminSquad|CarryRushSpell")
	float GetCurrentCarryRushSpeed() const { return CurrentRushSpeed; }

	UFUNCTION(BlueprintPure, Category = "PikminSquad")
	int32 GetMinionCount() const { return ActiveMinions.Num(); }

	UFUNCTION(BlueprintPure, Category = "PikminSquad")
	const TArray<AActor*>& GetActiveMinions() const { return ActiveMinions; }
	// ~End Squad, Stance, Whip & Carry Rush Blueprint API

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "PikminSquad|Runtime")
	TArray<TObjectPtr<AActor>> ActiveMinions;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PikminSquad|Runtime")
	EMinionSquadStance CurrentStance = EMinionSquadStance::Follow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config")
	bool bAutoRegisterMinionsOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Config", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxMinions = 8;

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

	/** All customization settings for the Carry Rush Battering Ram Spell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|CarryRushSpell", meta = (ShowOnlyInnerProperties))
	FMinionCarryRushConfig CarryRushConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Debug")
	bool bDebugDrawSlots = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PikminSquad|Debug")
	bool bPrintDebugToScreen = true;

private:
	void EvaluateSquadAndAvoidance();
	void SortMinionsForWhipArms();
	void UpdateHandAnchorTransforms(float ExtraOrbitYaw);
	void BuildChaosPhysicsChains();
	void DestroyChaosPhysicsChains();
	void UpdateAttackWhip(float DeltaTime);
	void PerformWhipHitSweep(AActor* MinionActor, int32 LinkIndexInArm, const FVector& StartLoc, const FVector& EndLoc);

	void EnterCarryRushMode();
	void ExitCarryRushMode();
	void UpdateCarryRush(float DeltaTime);
	FVector CalculateCarryPalanquinLocation(int32 MinionIdx, int32 TotalCount, const FVector& PlayerLoc, const FVector& ForwardDir, const FVector& RightDir) const;
	void PerformCarryRushEnemySweep(const FVector& SweepStart, const FVector& SweepEnd);

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

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> AvoidanceSphere;

	UPROPERTY(Transient)
	TObjectPtr<USphereComponent> HandAnchorSpheres[2];

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicsConstraintComponent>> ActiveChainConstraints;

	TArray<FVector, TInlineAllocator<16>> PrevMinionLocations;
	TArray<float, TInlineAllocator<16>> InitialMinionHeights;
	TMap<TWeakObjectPtr<AActor>, double> HitActorTimestamps;

	/** Actors temporarily ignored by the player's capsule during Carry Rush so enemies don't stop the charge. */
	TArray<TWeakObjectPtr<AActor>> RushIgnoredEnemies;

	FVector LastAnchorLocation = FVector::ZeroVector;
	FRotator LastAnchorRotation = FRotator::ZeroRotator;
	bool bForceNextUpdate = true;

	bool bIsSpinAttacking = false;
	bool bIsContinuousTornadoActive = false;
	float SpinElapsedTime = 0.0f;
	float ContinuousTornadoAngleOffset = 0.0f;

	// Carry Rush runtime state
	FVector CarryRushDirection = FVector::ForwardVector;
	float CarryRushElapsedTime = 0.0f;
	float CurrentRushSpeed = 0.0f;
	float CachedPlayerMeshBaseZ = 0.0f;
	bool bPlayerMeshZModified = false;

	FTimerHandle SquadTimerHandle;
};