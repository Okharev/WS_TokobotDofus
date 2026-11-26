// HeartHealthComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatFeedbackTypes.h"
#include "HeartHealthComponent.generated.h"

class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHeartsChangedSignature, int32, CurrentHearts, int32, MaxHearts, int32,
                                               DeltaHearts);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHitReceivedSignature, const FCombatHitPayload&, HitPayload);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaggerChangedSignature, bool, bIsStaggered);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActorDeathSignature, const FCombatHitPayload&, KillingHitPayload);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UHeartHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeartHealthComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// --- Designer & Animation Events ---
	UPROPERTY(BlueprintAssignable, Category = "Hearts|Events")
	FOnHeartsChangedSignature OnHeartsChanged;

	/** Fires immediately on valid hit. Bind SFX, Niagara hit sparks, and Hit-React AnimMontages here. */
	UPROPERTY(BlueprintAssignable, Category = "Hearts|Events")
	FOnHitReceivedSignature OnHitReceived;

	/** Fires when entering/exiting stagger. Use to pause AI attacks or swap AnimBP posture. */
	UPROPERTY(BlueprintAssignable, Category = "Hearts|Events")
	FOnStaggerChangedSignature OnStaggerChanged;

	/** Fires once when CurrentHearts reaches 0. */
	UPROPERTY(BlueprintAssignable, Category = "Hearts|Events")
	FOnActorDeathSignature OnDeath;

	// --- Combat API ---
	UFUNCTION(BlueprintCallable, Category = "Hearts")
	FCombatHitResponse ApplyCombatHit(const FCombatHitPayload& IncomingPayload);

	UFUNCTION(BlueprintCallable, Category = "Hearts")
	void HealHearts(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Hearts")
	int32 GetCurrentHearts() const { return CurrentHearts; }

	UFUNCTION(BlueprintPure, Category = "Hearts")
	int32 GetMaxHearts() const { return MaxHearts; }

	UFUNCTION(BlueprintPure, Category = "Hearts")
	bool IsAlive() const { return !bIsDead && CurrentHearts > 0; }

	UFUNCTION(BlueprintPure, Category = "Hearts")
	bool IsStaggered() const { return bIsStaggered; }

	/** Local jitter offset during HitStop. Hovering actors (like BirdEnemy) add this to their mesh location. */
	UFUNCTION(BlueprintPure, Category = "Hearts|Feedback")
	FVector GetCurrentMeshShakeOffset() const { return CurrentMeshShakeOffset; }

	/** Set to false on actors that drive Mesh RelativeLocation every tick (e.g., ABirdEnemyCharacter) so they add GetCurrentMeshShakeOffset() themselves. */
	void SetAutoApplyMeshShake(bool bAutoApply) { bAutoApplyMeshShake = bAutoApply; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|Config", meta = (ClampMin = "1"))
	int32 MaxHearts = 3;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hearts|Runtime")
	int32 CurrentHearts = 3;

	/** Brief i-frames (seconds) preventing multiple links of the same whip swing from double-hitting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|Config", meta = (ClampMin = "0.0", Units = "s"))
	float InvulnerabilityDuration = 0.25f;

	/** Scalar parameter name in the enemy's Material for the BOTW white/red impact flash (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|VisualFeedback")
	FName HitFlashMaterialParam = FName("HitFlashAlpha");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|VisualFeedback",
		meta = (ClampMin = "0.0", Units = "s"))
	float HitFlashDuration = 0.18f;

	/** Local vibration amplitude (cm) applied to the mesh during HitStop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|VisualFeedback",
		meta = (ClampMin = "0.0", Units = "cm"))
	float HitStopMeshShakeAmplitude = 5.5f;

	/** Multiplier on incoming knockback (0.0 = immovable boss, 1.0 = normal, 1.3 = light bird). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|Knockback", meta = (ClampMin = "0.0"))
	float KnockbackResistanceScale = 1.0f;

	/** Automatically offsets the owner's SkeletalMesh during HitStop. Disable if owner manually sets Mesh RelativeLocation on Tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hearts|VisualFeedback")
	bool bAutoApplyMeshShake = true;

private:
	UFUNCTION()
	void HandleHitStopEnded();

	void ReleasePendingKnockback();
	void EndStagger();
	USkeletalMeshComponent* ResolveOwnerMesh() const;

	bool bIsDead = false;
	bool bIsStaggered = false;
	double LastHitWorldTime = -100.0;

	bool bHasPendingKnockback = false;
	FVector PendingKnockbackVelocity = FVector::ZeroVector;

	bool bIsMeshShaking = false;
	double ShakeEndRealTime = 0.0;
	FVector CachedMeshBaseRelLoc = FVector::ZeroVector;
	FVector CurrentMeshShakeOffset = FVector::ZeroVector;

	bool bIsFlashing = false;
	double FlashEndRealTime = 0.0;

	FTimerHandle StaggerTimerHandle;
};
