#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SquashStretchComponent.generated.h"

class USceneComponent;

/**
 * Reusable 2nd-order damped spring for volume-preserving squash & stretch and impact aftershock.
 * Auto-sleeps its Tick when settled (zero idle CPU cost).
 */
UCLASS(ClassGroup = (Juice), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API USquashStretchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USquashStretchComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	/** Sets continuous speed-based elongation along an owner-local axis (e.g., ForwardVector during flight). */
	UFUNCTION(BlueprintCallable, Category = "Juice|SquashStretch")
	void SetSpeedStretch(float CurrentSpeed, const FVector& OwnerLocalAxis = FVector(1.0f, 0.0f, 0.0f));

	/** Clears continuous speed stretch so the spring rebounds cleanly to rest (1,1,1). */
	UFUNCTION(BlueprintCallable, Category = "Juice|SquashStretch")
	void ClearContinuousStretch();

	/**
	 * Fires a sudden-stop squash & spring aftershock.
	 * Negative ImpulseStrength = squash into surface then rebound-stretch; Positive = pop-stretch then compress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Juice|SquashStretch")
	void TriggerImpactAftershock(
		float ImpulseVelocity = -9.5f,
		const FVector& OwnerLocalAxis = FVector(1.0f, 0.0f, 0.0f),
		float ImmediateSquashOffset = -0.22f);

	/** Immediately restores base scale and disables tick. */
	UFUNCTION(BlueprintCallable, Category = "Juice|SquashStretch")
	void ResetToRest();

protected:
	/** Speed (cm/s) at which velocity stretch begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|SpeedStretch")
	float StretchMinSpeed = 650.0f;

	/** Speed (cm/s) at which velocity stretch hits MaxSpeedStretchAmount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|SpeedStretch")
	float StretchMaxSpeed = 2400.0f;

	/** Maximum additive elongation along flight axis (0.45 = 1.45x length, ~0.83x width/height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|SpeedStretch",
		meta = (ClampMin = "0.0", ClampMax = "1.2"))
	float MaxSpeedStretchAmount = 0.48f;

	/** Spring stiffness (higher = snappier wobble). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Spring", meta = (ClampMin = "10.0"))
	float SpringStiffness = 260.0f;

	/** Spring damping (lower = more jiggle oscillations after sudden stop; ~14-18 gives 2-3 crisp bounces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Spring", meta = (ClampMin = "1.0"))
	float SpringDamping = 14.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Clamp", meta = (ClampMin = "0.25", ClampMax = "0.95"))
	float MinAxisScale = 0.52f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|Clamp", meta = (ClampMin = "1.05", ClampMax = "2.5"))
	float MaxAxisScale = 1.65f;

	/** Optional Custom Primitive Data start index (writes Deformation at Index, Velocity at Index+1) if layering WPO bend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Juice|ShaderBridge")
	int32 CustomPrimitiveDataStartIndex = INDEX_NONE;

private:
	USceneComponent* ResolveTargetComponent();
	void ApplyVolumePreservingScale(USceneComponent* TargetComp);

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> CachedTargetComponent = nullptr;

	FVector BaseRelativeScale = FVector::OneVector;
	bool bBaseScaleCached = false;

	FVector ActiveOwnerLocalAxis = FVector::ForwardVector;
	float CurrentDeformation = 0.0f;
	float TargetDeformation = 0.0f;
	float SpringVelocity = 0.0f;
	bool bContinuousMode = false;
};
