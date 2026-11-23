#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CombatFeedbackTypes.h"
#include "MinionThrowTargetComponent.generated.h"

class UHeartHealthComponent;

UENUM(BlueprintType)
enum class EMinionThrowImpactBehavior : uint8
{
	ImpactAndBounce    UMETA(DisplayName = "Deal Damage / Trigger & Bounce Off"),
	LatchUntilRecalled UMETA(DisplayName = "Stick Until Recalled or Released"),
	LatchTimed         UMETA(DisplayName = "Stick for Duration Then Pop Off")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnThrowTargetFocusChangedSignature, bool, bIsLocked, bool, bInCandidateCone);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnThrowMinionLatchedChangedSignature, AActor*, MinionActor, int32, CurrentLatchedCount, int32, MaxSlots);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnThrowThresholdReachedSignature, bool, bThresholdMet);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnThrowMinionImpactSignature, AActor*, MinionActor, AActor*, InstigatorActor);

/**
 * Attach to doors, switches, platforms, or enemy skeletal sockets to make them valid single-tap throw targets.
 */
UCLASS(ClassGroup = (MinionInteraction), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionThrowTargetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMinionThrowTargetComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Designer Events ---
	/** Bind world-space widget / highlight material changes here. */
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowTargetFocusChangedSignature OnFocusChanged;

	/** Fires whenever any thrown minion arrives (for both ImpactAndBounce and Latch modes). */
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowMinionImpactSignature OnMinionImpact;

	/** Fires when a minion physically latches onto this target. */
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowMinionLatchedChangedSignature OnMinionLatched;

	/** Fires when a latched minion detaches (Recall, Timed expiry, Death, or Checkpoint Reset). */
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowMinionLatchedChangedSignature OnMinionDetached;

	/** Fires when LatchedCount crosses RequiredMinionsForThreshold (e.g., open/close a door). */
	UPROPERTY(BlueprintAssignable, Category = "MinionThrow|Events")
	FOnThrowThresholdReachedSignature OnThresholdStateChanged;

	// --- Targeting & Reservation API ---
	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	bool CanAcceptMinion() const;

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	bool IsTargetable() const { return bIsTargetable; }

	UFUNCTION(BlueprintCallable, Category = "MinionThrow")
	void SetTargetable(bool bNewTargetable);

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	float GetMaxTargetRange() const { return MaxTargetRange; }

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	float GetPriorityWeight() const { return PriorityWeight; }

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	int32 GetMaxMinionSlots() const { return MaxMinionSlots; }

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	int32 GetLatchedMinionCount() const;

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	int32 GetReservedAndLatchedCount() const;

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	int32 PeekNextAvailableSlotIndex() const;

	UFUNCTION(BlueprintPure, Category = "MinionThrow")
	FVector GetSlotWorldLocation(int32 SlotIndex) const;

	FTransform GetSlotWorldTransform(int32 SlotIndex) const;

	/** Reserves a slot immediately on launch so rapid-tapping never over-sends minions to a full target. */
	bool TryReserveSlot(AActor* IncomingMinion, int32& OutSlotIndex);

	/** Cancels an in-flight reservation without firing latch events. */
	void CancelReservation(AActor* IncomingMinion);

	/** Called by the minion when its parabolic flight reaches Alpha == 1.0. */
	void NotifyMinionArrived(AActor* MinionActor, int32 SlotIndex, const FVector& FlightDirection, AActor* InstigatorActor);

	/** Releases a specific minion (latched or in-flight). If bSnapWithoutPop is true, skips pop impulse (checkpoint reset). */
	UFUNCTION(BlueprintCallable, Category = "MinionThrow")
	void ReleaseMinion(AActor* MinionActor, const FVector& PopVelocity = FVector::ZeroVector, bool bSnapWithoutPop = false);

	/** Releases every minion currently attached or flying toward this target. */
	UFUNCTION(BlueprintCallable, Category = "MinionThrow")
	void ReleaseAllMinions(float RadialPopSpeed = 280.0f, bool bSnapWithoutPop = false);

	void NotifyFocusState(bool bIsLocked, bool bInCandidateCone);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior")
	EMinionThrowImpactBehavior ImpactBehavior = EMinionThrowImpactBehavior::LatchUntilRecalled;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxMinionSlots = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior", meta = (ClampMin = "1", ClampMax = "16"))
	int32 RequiredMinionsForThreshold = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior", meta = (EditCondition = "ImpactBehavior == EMinionThrowImpactBehavior::LatchTimed", ClampMin = "0.25", Units = "s"))
	float TimedLatchDuration = 4.0f;

	/** Optional local offsets for latched minions. Auto-distributes in a ring around the component if left empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior")
	TArray<FVector> LocalSlotOffsets;

	/** Fallback ring radius when LocalSlotOffsets is empty and MaxMinionSlots > 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Behavior", meta = (ClampMin = "0.0", Units = "cm"))
	float AutoSlotRingRadius = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Targeting")
	bool bIsTargetable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Targeting", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxTargetRange = 1800.0f;

	/** Higher values bias lock-on toward this target (e.g., 1.2 for enemies, 1.0 for switches). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Targeting", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float PriorityWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Bounce")
	float BounceBackSpeed = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Bounce")
	float BounceUpSpeed = 320.0f;

	/** If true, routes arrival damage/knockback/HitStop through owner's IHitReactableInterface or UHeartHealthComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Combat")
	bool bApplyCombatHitOnArrival = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|Combat", meta = (EditCondition = "bApplyCombatHitOnArrival"))
	FHitFeedbackConfig ThrowHitFeedback;

	/** Automatically reduces owner ACharacter's MaxWalkSpeed per latched minion (event-driven, zero tick). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|EnemySlow")
	bool bApplyMovementSlowToOwner = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|EnemySlow", meta = (EditCondition = "bApplyMovementSlowToOwner", ClampMin = "0.0", ClampMax = "0.9"))
	float SlowFractionPerMinion = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionThrow|EnemySlow", meta = (EditCondition = "bApplyMovementSlowToOwner", ClampMin = "0.05", ClampMax = "1.0"))
	float MinWalkSpeedMultiplier = 0.25f;

private:
	struct FMinionSlotEntry
	{
		TWeakObjectPtr<AActor> Minion;
		bool bIsLatched = false;
		FTimerHandle TimedLatchHandle;
	};

	UFUNCTION()
	void HandleOwnerDeath(const FCombatHitPayload& KillingHitPayload);

	void HandleTimedLatchExpired(int32 SlotIndex);
	void EvaluateThresholdAndSlow(int32 PreviousLatchedCount);
	void EnsureSlotArraySized();

	TArray<FMinionSlotEntry> SlotEntries;

	bool bLastThresholdMet = false;
	bool bLastLockedState = false;
	bool bLastInConeState = false;

	bool bCachedBaseWalkSpeed = false;
	float CachedOwnerBaseWalkSpeed = 0.0f;
};