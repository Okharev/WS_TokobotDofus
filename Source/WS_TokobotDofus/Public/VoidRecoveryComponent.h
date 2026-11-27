// VoidRecoveryComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoidRecoveryComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UCameraDirectorComponent;
class UMinionSquadComponent;

/**
 * Optional per-trigger overrides passed by AVoidHazardVolume (or Blueprints) when initiating void recovery.
 */
USTRUCT(BlueprintType)
struct FVoidFallRequest
{
	GENERATED_BODY()

	/** Number of hearts deducted upon respawn (0 = free teleport). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery", meta = (ClampMin = "0"))
	int32 HeartPenalty = 1;

	/** Color of the screen fade (black for pits, deep blue for drowning, orange for lava). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery")
	FLinearColor FadeColor = FLinearColor::Black;

	/** If true, forces teleport to OverrideRespawnTransform instead of the dynamic safe-ground buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery")
	bool bUseOverrideRespawn = false;

	/** Optional OoT-style room-entrance respawn override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery",
		meta = (EditCondition = "bUseOverrideRespawn"))
	FTransform OverrideRespawnTransform = FTransform::Identity;

	/** If true, detaches minions currently latched onto puzzle interactables when resetting the squad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery")
	bool bDetachMinionsFromInteractables = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoidFallStartedSignature, const FVoidFallRequest&, Request);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVoidRecoveryCompletedSignature, FVector, RespawnLocation,
                                             bool, bWasFatal);

/**
 * Composition-based void-fall failsafe for Ocarina of Time / Pikmin style games.
 * Zero Tick overhead: uses a lightweight timer to sample safe NavMesh-validated ground
 * and an event-driven falling watchdog for Kill-Z protection.
 */
UCLASS(ClassGroup=(Gameplay), meta=(BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UVoidRecoveryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVoidRecoveryComponent();

	// ~Begin Blueprint Events
	/** Fires the moment the player enters a void hazard (use for fall scream SFX / drop VFX). */
	UPROPERTY(BlueprintAssignable, Category = "VoidRecovery|Events")
	FOnVoidFallStartedSignature OnVoidFallStarted;

	/** Fires right as the screen fades back in on safe ground (use for respawn anim / invuln flash). */
	UPROPERTY(BlueprintAssignable, Category = "VoidRecovery|Events")
	FOnVoidRecoveryCompletedSignature OnVoidRecoveryCompleted;
	// ~End Blueprint Events

	// ~Begin Public API
	/** Initiates the full fall-watch -> fade -> teleport -> squad reset -> heart penalty -> fade-in sequence. */
	UFUNCTION(BlueprintCallable, Category = "VoidRecovery")
	bool TriggerVoidFall(const FVoidFallRequest& Request);

	/** Sets a level/room checkpoint fallback used if no dynamic safe ground has been recorded yet. */
	UFUNCTION(BlueprintCallable, Category = "VoidRecovery")
	void SetRoomFallbackTransform(const FTransform& NewFallbackTransform);

	/** Manually forces a safe-ground snapshot (e.g., when entering a room or finishing a cutscene). */
	UFUNCTION(BlueprintCallable, Category = "VoidRecovery")
	void RecordManualSafeTransform(const FTransform& SafeTransform);

	UFUNCTION(BlueprintPure, Category = "VoidRecovery")
	bool IsRecoveringFromVoid() const { return bIsRecovering; }

	UFUNCTION(BlueprintPure, Category = "VoidRecovery")
	FTransform GetBestSafeTransform() const;
	// ~End Public API

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Safe Ground Sampling ---
	/** How often (seconds) to sample grounded safe positions while exploring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling",
		meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float SafeSampleInterval = 0.25f;

	/** Number of recent safe transforms kept in the ring buffer (fallback if latest becomes blocked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 MaxSafeHistoryCount = 4;

	/** Minimum 2D distance (cm) moved before pushing a new entry into the ring buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling", meta = (ClampMin = "10.0"))
	float MinDistanceBetweenSamples = 140.0f;

	/** Radius (cm) around the player checked by 4 cardinal ground traces to avoid saving cliff-edge slivers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling", meta = (ClampMin = "10.0"))
	float LedgeSafetyMargin = 55.0f;

	/** Max downward drop (cm) allowed on the 4 cardinal ledge-margin traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling", meta = (ClampMin = "5.0"))
	float MaxLedgeDropTolerance = 45.0f;

	/** Require the safe spot to project cleanly onto the NavMesh (ensures minions can stand there too). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling")
	bool bRequireNavMeshForSafeGround = true;

	/** Reject moving platforms / dynamic lifts as safe ground so we never teleport into empty air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling")
	bool bRejectMovingPlatforms = true;

	/** Actors or Components carrying this tag are ignored by safe-ground recording (e.g. crumbling floors). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Sampling")
	FName UnsafeSurfaceTag = FName("NoSafeRespawn");

	// --- Kill-Z / Abyss Failsafe (when player misses a VoidHazardVolume) ---
	/** Automatically triggers void recovery if the player falls this far below their last safe Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|KillZGuard")
	bool bEnableFallbackDropGuard = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|KillZGuard",
		meta = (EditCondition = "bEnableFallbackDropGuard", ClampMin = "200.0"))
	float MaxFallDistanceBelowSafeZ = 1800.0f;

	/** Hard world Z floor failsafe regardless of last safe ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|KillZGuard",
		meta = (EditCondition = "bEnableFallbackDropGuard"))
	float AbsoluteWorldMinZ = -10000.0f;

	/** Default request used when triggered by the automatic Kill-Z guard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|KillZGuard")
	FVoidFallRequest DefaultKillZRequest;

	// --- Recovery Timing & Camera Framing ---
	/** Delay before fading starts (or while fading) to let the player read the fall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Timing", meta = (ClampMin = "0.0"))
	float FadeOutDelay = 0.15f;

	/** Duration of the fade to black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Timing", meta = (ClampMin = "0.05"))
	float FadeOutDuration = 0.35f;

	/** Hold duration at full black so overlaps, SpringArm lag flush, and streaming settle cleanly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Timing", meta = (ClampMin = "0.05"))
	float BlackHoldDuration = 0.15f;

	/** Duration of the fade back in from black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Timing", meta = (ClampMin = "0.05"))
	float FadeInDuration = 0.35f;

	/** If true, freezes Free-3P camera at the rim and pitches down to watch the player drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Camera")
	bool bWatchFallingPlayerInFreeCam = true;

	/** Pitch angle (degrees, negative = looking slightly down) snapped behind the player on Free-3P respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Camera",
		meta = (ClampMin = "-45.0", ClampMax = "10.0"))
	float RespawnCameraPitch = -14.0f;

	/** Small upward Z offset (cm) applied on teleport to prevent floor-penetration snags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Respawn")
	float RespawnZLift = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidRecovery|Debug")
	bool bDrawDebugSafeSpots = false;

private:
	void SampleSafeGround();
	bool ValidateLocationAsSafeGround(const FVector& CandidateActorLoc, FVector& OutAdjustedLoc) const;
	bool IsTransformClearForRespawn(const FTransform& Candidate) const;

	UFUNCTION()
	void OnOwnerMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	void PollFallDepthGuard();

	// State machine callbacks
	void Phase_StartFadeOut();
	void Phase_ExecuteTeleportAtBlack();
	void Phase_FinishAndFadeIn();

	/** Routes heart penalty through IHitReactableInterface / UHeartHealthComponent. Returns true if fatal. */
	bool ApplyVoidHeartPenalty(int32 HeartDamage);

	UCameraDirectorComponent* ResolveCameraDirector() const;
	APlayerController* ResolvePlayerController() const;

	TWeakObjectPtr<ACharacter> OwnerCharacter;
	TWeakObjectPtr<UCharacterMovementComponent> OwnerMoveComp;
	TWeakObjectPtr<UMinionSquadComponent> CachedSquadComp;

	/** Most recent valid safe transforms (newest at the end). */
	TArray<FTransform> SafeTransformHistory;
	FTransform RoomFallbackTransform = FTransform::Identity;
	bool bHasRoomFallback = false;

	bool bIsRecovering = false;
	bool bLastRecoveryWasFatal = false;
	FVoidFallRequest ActiveRequest;
	FVector LastRespawnLocation = FVector::ZeroVector;

	FTimerHandle SafeSampleTimerHandle;
	FTimerHandle FallGuardTimerHandle;
	FTimerHandle RecoveryPhaseTimerHandle;
};