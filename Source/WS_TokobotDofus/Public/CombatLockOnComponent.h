#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatLockOnComponent.generated.h"

class UCombatLockOnTargetComponent;
class UCameraDirectorComponent;
class UMinionSquadComponent;
class ACharacter;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCombatLockTargetChangedSignature,
	UCombatLockOnTargetComponent*, NewTarget,
	UCombatLockOnTargetComponent*, PreviousTarget);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCombatCameraCenterSnapSignature);

/**
 * Attach to the Player Character alongside UMinionThrowTargetingComponent.
 * Manages modern Z-Targeting: acquisition, 2D screen-space switching, occlusion grace,
 * auto-transfer on enemy death, character facing, and integration with UCameraDirectorComponent.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UCombatLockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatLockOnComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Events ---
	/** Bind your HUD Reticle Widget and combat SFX here. */
	UPROPERTY(BlueprintAssignable, Category = "CombatLockOn|Events")
	FOnCombatLockTargetChangedSignature OnLockTargetChanged;

	/** Fires when Z-Target is pressed with no valid enemy in range (classic OoT camera re-center). */
	UPROPERTY(BlueprintAssignable, Category = "CombatLockOn|Events")
	FOnCombatCameraCenterSnapSignature OnRequestCameraCenterSnap;

	// --- Input API (Blueprint-Ready) ---

	/** Toggle mode: releases if locked; acquires best enemy if unlocked (or triggers camera center snap). */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	bool ToggleOrAcquireLock();

	/** Hold mode (Started): attempts to acquire lock, or centers camera if no target is valid. */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	bool AcquireLock();

	/** Hold mode (Completed/Canceled) or forced unlock (cutscenes, death). */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	void ReleaseLock();

	/**
	 * Feed IA_Look (Axis2D) here BEFORE applying AddControllerYaw/PitchInput.
	 * Returns true if input was consumed by Lock-On (skip manual camera rotation);
	 * returns false if unlocked (proceed with normal camera look).
	 */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	bool HandleLookInput(FVector2D LookInput);

	/** Explicitly attempt to switch locked target in a 2D input direction (X = Right, Y = Up). */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	bool TrySwitchTarget(FVector2D StickInputDir);

	/** Temporarily pauses auto-facing (e.g., during dodge rolls or root-motion attacks). */
	UFUNCTION(BlueprintCallable, Category = "CombatLockOn")
	void SetPauseLockFacing(bool bPaused) { bPauseLockFacing = bPaused; }

	// --- Queries (AnimBP / UI / Combat) ---

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	bool IsLockedOn() const { return LockedTarget.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	UCombatLockOnTargetComponent* GetLockedTarget() const { return LockedTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "CombatLockOn")
	FVector GetLockedTargetLocation() const;

	/** Signed strafe angle in degrees [-180, 180] between character velocity and facing (for 8-way Strafe BlendSpace). */
	UFUNCTION(BlueprintPure, Category = "CombatLockOn|Animation")
	float GetStrafeAngleDegrees() const;

protected:
	// --- Acquisition & Range ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Acquisition", meta = (ClampMin = "200.0", Units = "cm"))
	float MaxGlobalLockRange = 2200.0f;

	/** Extra distance allowed before breaking an active lock (prevents range-edge flicker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Acquisition", meta = (ClampMin = "0.0", Units = "cm"))
	float BreakDistanceBuffer = 400.0f;

	/** Half-angle of the camera acquisition cone (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Acquisition", meta = (ClampMin = "15.0", ClampMax = "90.0"))
	float AcquisitionConeHalfAngleDegrees = 55.0f;

	/** If true, enemies very close to the player can still be acquired even outside the camera cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Acquisition", meta = (ClampMin = "0.0", Units = "cm"))
	float CloseProximityFallbackRadius = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Acquisition")
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;

	// --- Target Switching (Right Stick / Mouse Flick) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Switching", meta = (ClampMin = "0.2", ClampMax = "0.95"))
	float SwitchInputThreshold = 0.65f;

	/** Stick magnitude must drop below this value before a new stick switch is armed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Switching", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float SwitchResetThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Switching", meta = (ClampMin = "0.05", Units = "s"))
	float SwitchCooldown = 0.22f;

	/** Set true if your IA_Look Y axis is already negated (screen-down positive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Switching")
	bool bInvertSwitchVerticalAxis = false;

	// --- Validation & Auto-Transfer ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Validation", meta = (ClampMin = "0.03", Units = "s"))
	float ValidationInterval = 0.1f;

	/** Seconds a target can stay occluded behind a pillar/wall before lock drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Validation", meta = (ClampMin = "0.0", Units = "s"))
	float OcclusionGraceDuration = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|AutoTransfer")
	bool bAutoTransferOnKill = true;

	/** Short delay after enemy death so kill hitstop reads clearly before snapping to the next enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|AutoTransfer", meta = (EditCondition = "bAutoTransferOnKill", ClampMin = "0.0", Units = "s"))
	float AutoTransferDelay = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|AutoTransfer", meta = (EditCondition = "bAutoTransferOnKill", ClampMin = "200.0", Units = "cm"))
	float AutoTransferRadius = 1400.0f;

	// --- Character Facing ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Facing", meta = (ClampMin = "1.0"))
	float CharacterTurnInterpSpeed = 14.0f;

	// --- Debug ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatLockOn|Debug")
	bool bDebugDrawLockOn = false;

private:
	UCombatLockOnTargetComponent* FindBestAcquisitionTarget(float MaxRadiusOverride = -1.0f, bool bRequireViewCone = true) const;
	void SetLockedTargetInternal(UCombatLockOnTargetComponent* NewTarget);
	void ValidateCurrentLock();
	void HandleTargetInvalidated(UCombatLockOnTargetComponent* InvalidatedTarget, bool bWasKilled);
	void ExecuteAutoTransfer();

	bool HasLineOfSightToTarget(const UCombatLockOnTargetComponent* TargetComp) const;
	bool GetViewPoint(FVector& OutViewLoc, FVector& OutViewForward) const;
	UCameraDirectorComponent* GetCameraDirector() const;
	APlayerController* GetOwningPlayerController() const;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CachedCharacter = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CachedMovement = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMinionSquadComponent> CachedSquadComponent = nullptr;

	TWeakObjectPtr<UCombatLockOnTargetComponent> LockedTarget;
	FDelegateHandle TargetInvalidatedHandle;

	FTimerHandle ValidationTimerHandle;
	FTimerHandle AutoTransferTimerHandle;

	float TimeOccluded = 0.0f;
	double LastSwitchTime = -10.0;
	bool bSwitchArmed = true;
	bool bPauseLockFacing = false;
	bool bSavedOrientRotationToMovement = true;
	bool bHasSavedMovementState = false;
};