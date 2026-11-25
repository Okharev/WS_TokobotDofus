#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "CombatFeedbackTypes.h"
#include "CombatLockOnTargetComponent.generated.h"

class UMinionThrowTargetComponent;
class UCombatLockOnTargetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatLockFocusChangedSignature, bool, bIsLocked);

/** Native event used by UCombatLockOnComponent for zero-polling reaction to enemy death or disable. */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnCombatLockTargetInvalidatedNative,
	UCombatLockOnTargetComponent* /*InvalidatedTarget*/,
	bool /*bWasKilled*/);

/**
 * Attach to an enemy chest/head socket (or root + local Z offset) to make it Z-Targetable.
 * Decoupled from UMinionThrowTargetComponent so melee lock-on still works when minion slots are full.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UCombatLockOnTargetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCombatLockOnTargetComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bind Blueprint VFX / highlight / audio cues on the enemy here. */
	UPROPERTY(BlueprintAssignable, Category = "CombatLockOn|Events")
	FOnCombatLockFocusChangedSignature OnLockFocusChanged;

	/** Internal C++ event fired when the owner dies, is destroyed, or becomes non-lockable. */
	FOnCombatLockTargetInvalidatedNative OnTargetInvalidated;

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	bool CanBeLockedOn() const;

	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	void SetLockable(bool bNewLockable);

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	float GetMaxLockDistance() const { return MaxLockDistance; }

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	float GetPriorityWeight() const { return PriorityWeight; }

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	float GetFramingRadiusScale() const { return FramingRadiusScale; }

	/** World-space lock anchor including LockWorldOffset (used by reticle, camera, and LOS traces). */
	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	FVector GetLockWorldLocation() const;

	/** Returns explicit LinkedThrowTarget if set, otherwise auto-resolves an available throw target on the same owner. */
	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	UMinionThrowTargetComponent* ResolveLinkedThrowTarget() const;

	/** Called by UCombatLockOnComponent when lock focus is gained or lost. */
	void NotifyLockFocusState(bool bNewIsLocked);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn")
	bool bIsLockable = true;

	/** Maximum acquisition range for this specific enemy (clamped against player's global max range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn", meta = (ClampMin = "200.0", Units = "cm"))
	float MaxLockDistance = 2200.0f;

	/** Higher values bias lock-on acquisition toward priority threats (e.g., 1.5 for bosses, 1.0 for grunts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float PriorityWeight = 1.0f;

	/** Scales camera arm pull-back & framing elevation for large enemies / bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Framing", meta = (ClampMin = "0.5", ClampMax = "4.0"))
	float FramingRadiusScale = 1.0f;

	/** Additional world-space Z/offset applied on top of this SceneComponent's location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Framing")
	FVector LockWorldOffset = FVector::ZeroVector;

	/** Optional explicit link to a primary throw target (leave null to auto-detect on owner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Synergy")
	TObjectPtr<UMinionThrowTargetComponent> LinkedThrowTarget = nullptr;

private:
	UFUNCTION()
	void HandleOwnerDeath(const FCombatHitPayload& KillingHitPayload);

	bool bIsCurrentlyLocked = false;
};