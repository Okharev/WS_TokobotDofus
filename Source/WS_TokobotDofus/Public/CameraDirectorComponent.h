// CameraDirectorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraDirectorComponent.generated.h"

class ACameraZoneActor;
class UCombatLockOnTargetComponent;
class USpringArmComponent;

UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UCameraDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCameraDirectorComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// --- Retro Camera Zone Stack ---
	UFUNCTION(BlueprintCallable, Category = "Retro Camera")
	void PushZone(ACameraZoneActor* Zone);

	UFUNCTION(BlueprintCallable, Category = "Retro Camera")
	void PopZone(ACameraZoneActor* Zone);

	UFUNCTION(BlueprintCallable, Category = "Retro Camera")
	void ClearAllZones(float BlendOutTime = 0.5f);

	UFUNCTION(BlueprintPure, Category = "Retro Camera")
	bool IsInFixedCameraZone() const { return CurrentActiveZone.IsValid(); }

	// --- Combat Z-Targeting & Center Snap ---
	/** Called automatically by UCombatLockOnComponent when lock state changes. */
	UFUNCTION(BlueprintCallable, Category = "Retro Camera|LockOn")
	void SetCombatLockTarget(UCombatLockOnTargetComponent* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Retro Camera|LockOn")
	UCombatLockOnTargetComponent* GetActiveCombatLockTarget() const { return ActiveLockTarget.Get(); }

	/** Smoothly centers the free-3P camera behind the player (Ocarina Z-tap with no enemy). */
	UFUNCTION(BlueprintCallable, Category = "Retro Camera|LockOn")
	void RequestCameraCenterSnap();

	/** Cancels an in-progress camera center snap when the player manually moves the look stick/mouse. */
	UFUNCTION(BlueprintCallable, Category = "Retro Camera|LockOn")
	void NotifyManualLookInput();

	// --- Movement Basis ---
	// Call from IA_Move (Triggered): converts 2D input (X = Right, Y = Forward) into a world move vector
	UFUNCTION(BlueprintCallable, Category = "Retro Camera|Movement")
	FVector GetWorldMoveDirection(FVector2D MoveInput);

	// Call from IA_Move (Completed / Canceled) to immediately refresh direction on stick release
	UFUNCTION(BlueprintCallable, Category = "Retro Camera|Movement")
	void NotifyMoveInputReleased();

	// --- Retro Camera Movement Latching Tuning ---
	// Keeps the previous movement direction across camera cuts until stick is released or turned
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement")
	bool bLatchBasisOnCameraSwitch = true;

	// Keeps movement straight while holding steady in a Look-At camera (prevents orbiting/flipping)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement")
	bool bLatchBasisDuringLookAt = true;

	// Turning the stick past this angle (degrees) breaks the latch and smoothly aligns to the new view
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement",
		meta = (ClampMin = "5.0", ClampMax = "180.0"))
	float StickBreakAngleDegrees = 35.0f;

	// How fast the basis catches up to the live camera after breaking a latch mid-hold
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement", meta = (ClampMin = "1.0"))
	float BasisCatchupSpeed = 8.0f;

	// --- Combat Z-Target Movement & Camera Framing Tuning ---
	/** True = classic Ocarina circular orbit strafing around locked target (works in both Free 3P and Fixed Zones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn")
	bool bUseTargetOrbitMovement = true;

	/** Blend weight between Player (0.0) and Locked Enemy (1.0) for free-3P camera aim focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn",
		meta = (ClampMin = "0.1", ClampMax = "0.85"))
	float LockCameraFocusWeight = 0.42f;

	/** How fast free-3P ControlRotation tracks the locked duel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn", meta = (ClampMin = "1.0"))
	float LockCameraRotInterpSpeed = 9.0f;

	/** Extra SpringArm length added while Z-Targeting (scaled by target's FramingRadiusScale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn", meta = (Units = "cm"))
	float LockExtraArmLength = 110.0f;

	/** Subtle lateral/vertical SocketOffset added during lock-on so the player doesn't block enemy telegraphs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn")
	FVector LockAdditionalSocketOffset = FVector(0.0f, 52.0f, 22.0f);

	/** Blend speed for SpringArm length & shoulder offset transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn", meta = (ClampMin = "1.0"))
	float LockSpringArmInterpSpeed = 7.0f;

	/** Clamped pitch bounds for free-3P Z-Targeting camera (degrees, negative looks slightly downward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn",
		meta = (ClampMin = "-60.0", ClampMax = "10.0"))
	float LockPitchMin = -26.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn",
		meta = (ClampMin = "-45.0", ClampMax = "20.0"))
	float LockPitchMax = -10.0f;

	/** Downward pitch angle applied on an Ocarina no-target Z-tap center snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn",
		meta = (ClampMin = "-45.0", ClampMax = "10.0"))
	float CenterSnapPitch = -14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|LockOn", meta = (ClampMin = "1.0"))
	float CenterSnapInterpSpeed = 12.0f;

private:
	void EvaluateStack(const ACameraZoneActor* ExitingZone = nullptr);
	ACameraZoneActor* GetTopPriorityZone();
	void ComputeScreenBasis(const FRotator& CamRot, FVector& OutForward, FVector& OutRight) const;

	bool EnsureSpringArmCached(APawn* Pawn);
	void UpdateFreeCameraLockOn(float DeltaTime, APlayerController* PC, APawn* Pawn);
	void UpdateCameraCenterSnap(float DeltaTime, APlayerController* PC, APawn* Pawn);
	void RestoreSpringArmToBase(float DeltaTime, APawn* Pawn);
	void UpdateTickState();

	UPROPERTY()
	TArray<TWeakObjectPtr<ACameraZoneActor>> ZoneStack;

	UPROPERTY()
	TWeakObjectPtr<ACameraZoneActor> CurrentActiveZone;

	// Movement latching state
	FVector ActiveForward = FVector::ForwardVector;
	FVector ActiveRight = FVector::RightVector;
	FVector2D LatchedStickDir = FVector2D::ZeroVector;
	uint64 LastMoveInputFrame = 0;
	bool bHasValidBasis = false;
	bool bIsBasisLatched = false;
	bool bNeedsSmoothCatchup = false;

	// Combat Z-Targeting & SpringArm framing state
	TWeakObjectPtr<UCombatLockOnTargetComponent> ActiveLockTarget;
	TWeakObjectPtr<APawn> CachedSpringArmPawn;
	TWeakObjectPtr<USpringArmComponent> CachedSpringArm;
	float BaseTargetArmLength = 400.0f;
	FVector BaseSocketOffset = FVector::ZeroVector;
	bool bHasCachedSpringArmBase = false;
	bool bSpringArmNeedsRestore = false;
	bool bCenterSnapActive = false;
};
