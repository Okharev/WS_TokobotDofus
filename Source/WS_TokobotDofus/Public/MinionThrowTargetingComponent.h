#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MinionThrowTargetingComponent.generated.h"

class UMinionThrowTargetComponent;
class UMinionSquadComponent;
class UCombatLockOnComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnThrowLockedTargetChangedSignature,
	UMinionThrowTargetComponent*, NewTarget,
	UMinionThrowTargetComponent*, PreviousTarget);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnMinionThrownAtTargetSignature,
	AActor*, ThrownMinion,
	UMinionThrowTargetComponent*, TargetComponent);

/**
 * Attach to the Player Character alongside UMinionSquadComponent and UCombatLockOnComponent.
 * Scores throw targets inside the camera view cone and automatically prioritizes the Z-Targeted enemy.
 */
UCLASS(ClassGroup = (MinionInteraction), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionThrowTargetingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMinionThrowTargetingComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Events ---
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowLockedTargetChangedSignature OnLockedTargetChanged;

	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnMinionThrownAtTargetSignature OnMinionThrown;

	// --- Single-Tap Gameplay API ---
	/** Single-tap instant throw at the best-scored locked target. Returns true if a minion was launched. */
	UFUNCTION(BlueprintCallable, Category = "MinionThrow")
	bool TryThrowMinionAtLockedTarget();

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	UMinionThrowTargetComponent* GetLockedTarget() const { return LockedTarget.Get(); }

	/** Computes world-space sample points along the live throw parabola (usable by Niagara/Spline UI as well as debug draw). */
	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	bool GetThrowArcPreviewPoints(TArray<FVector>& OutPoints, int32 NumSegments = 18) const;

protected:
	// --- Cone & Scoring Tuning ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Targeting", meta = (ClampMin = "0.02", ClampMax = "0.25", Units = "s"))
	float EvaluationInterval = 0.05f;

	/** Half-angle of the camera aim cone (degrees). 32-40 feels effortless like The Pathless / Sekiro. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Targeting", meta = (ClampMin = "5.0", ClampMax = "75.0"))
	float AimConeHalfAngleDegrees = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Targeting", meta = (ClampMin = "150.0", Units = "cm"))
	float MaxGlobalThrowRange = 2000.0f;

	/** Sticky score bonus given to the currently locked target to eliminate reticle flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Targeting", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float LockHysteresisBonus = 0.12f;

	/** If true, Z-Targeting an enemy strongly biases minion throws onto that enemy's throw target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|LockOnSynergy")
	bool bPrioritizeCombatLockTarget = true;

	/** Allows throwing minions at a Z-Targeted enemy even if they sit near the edge of a fixed camera view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|LockOnSynergy", meta = (EditCondition = "bPrioritizeCombatLockTarget"))
	bool bAllowLockedTargetOutsideCone = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|LockOnSynergy", meta = (EditCondition = "bPrioritizeCombatLockTarget", ClampMin = "0.5"))
	float CombatLockScoreBonus = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Targeting")
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;

	// --- Throw Trajectory Tuning ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Trajectory", meta = (ClampMin = "300.0"))
	float ThrowFlightSpeed = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Trajectory", meta = (ClampMin = "0.0", Units = "cm"))
	float ThrowArcHeight = 140.0f;

	/** Local offset on the player where the throw arc starts (and preview originates). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Trajectory")
	FVector PlayerLaunchLocalOffset = FVector(45.0f, 25.0f, 35.0f);

	/** Max distance from player to grab an Idle/Following minion for throwing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Trajectory", meta = (ClampMin = "150.0", Units = "cm"))
	float MaxMinionGrabRadius = 1100.0f;

	// --- Debug Visuals ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Debug")
	bool bDebugDrawTargeting = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Debug")
	bool bDebugDrawThrowArc = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MinionThrow|Debug", meta = (ClampMin = "6", ClampMax = "40"))
	int32 DebugArcSegments = 18;

private:
	struct FCandidateScore
	{
		TWeakObjectPtr<UMinionThrowTargetComponent> Target;
		float Score = 0.0f;
		float Distance = 0.0f;
	};

	void EvaluateTargets();
	UMinionThrowTargetComponent* ResolveCombatLockedThrowTarget() const;
	bool GetPlayerViewPoint(FVector& OutViewLoc, FVector& OutViewForward) const;
	bool HasLineOfSightToTarget(const FVector& TraceStart, const UMinionThrowTargetComponent* TargetComp) const;
	FVector GetLaunchOriginWorld() const;
	void UpdateDebugTickState();

	UPROPERTY(Transient)
	TObjectPtr<UMinionSquadComponent> CachedSquadComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCombatLockOnComponent> CachedLockOnComponent = nullptr;

	TWeakObjectPtr<UMinionThrowTargetComponent> LockedTarget;
	float LockedTargetScore = 0.0f;

	TArray<TWeakObjectPtr<UMinionThrowTargetComponent>> ActiveConeCandidates;
	TArray<TWeakObjectPtr<UMinionThrowTargetComponent>> OccludedConeCandidates;

	FTimerHandle EvaluationTimerHandle;
};