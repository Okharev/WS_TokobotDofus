// MinionTriggerRelayComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelActivatableInterface.h"
#include "MinionTriggerRelayComponent.generated.h"

class UMinionThrowTargetComponent;

UENUM(BlueprintType)
enum class EMinionTriggerMode : uint8
{
	HoldThreshold         UMETA(DisplayName = "Hold While Threshold Met"),
	OneShot               UMETA(DisplayName = "One-Shot Permanent Unlock"),
	Toggle                UMETA(DisplayName = "Toggle On Impact / Threshold"),
	ProportionalToLatched UMETA(DisplayName = "Proportional (Latched / Max)")
};

UCLASS(ClassGroup = (MinionInteraction), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UMinionTriggerRelayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMinionTriggerRelayComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Useful for checkpoint/save restoration without simulating minion throws. */
	UFUNCTION(BlueprintCallable, Category = "MinionTrigger")
	void ForceTriggerState(bool bNewActive, float NormalizedValue, bool bInstant = true);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionTrigger")
	EMinionTriggerMode TriggerMode = EMinionTriggerMode::HoldThreshold;

	/** If true in OneShot mode, disables targeting and pops latched minions back to the squad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionTrigger", meta = (EditCondition = "TriggerMode == EMinionTriggerMode::OneShot"))
	bool bConsumeTargetOnOneShot = true;

	/** Optional delay before popping minions off a consumed OneShot switch so the arrival reads visually. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionTrigger", meta = (EditCondition = "TriggerMode == EMinionTriggerMode::OneShot && bConsumeTargetOnOneShot", ClampMin = "0.0", Units = "s"))
	float ReleaseDelayAfterOneShot = 0.25f;

	/** Direct viewport eyedropper links to doors, bridges, or logic gates. Soft refs stay World Partition safe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MinionTrigger", meta = (MustImplement = "/Script/WS_TOKOBOTDOFUS.LevelActivatableInterface"))
	TArray<TSoftObjectPtr<AActor>> LinkedTargets;

private:
	UFUNCTION()
	void HandleThresholdChanged(bool bThresholdMet);

	UFUNCTION()
	void HandleMinionImpact(AActor* MinionActor, AActor* InstigatorActor);

	UFUNCTION()
	void HandleLatchedCountChanged(AActor* MinionActor, int32 CurrentLatchedCount, int32 MaxSlots);

	void DispatchSignal(bool bActive, float NormalizedValue, bool bInstant = false);
	void ConsumeAndReleaseTarget();

	UPROPERTY(Transient)
	TObjectPtr<UMinionThrowTargetComponent> CachedThrowTarget = nullptr;

	bool bCurrentActive = false;
	bool bHasFiredOneShot = false;
	FTimerHandle ReleaseDelayHandle;
};