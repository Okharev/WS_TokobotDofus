// ActivationAggregatorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LevelActivatableInterface.h"
#include "ActivationAggregatorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAggregateStateChangedSignature, bool, bIsActivated, bool, bInstant);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAggregateProgressChangedSignature, float, NormalizedAlpha, bool, bInstant);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeactivationGraceStartedSignature, float, GraceDuration);

UCLASS(ClassGroup = (MinionInteraction), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UActivationAggregatorComponent : public UActorComponent, public ILevelActivatableInterface
{
	GENERATED_BODY()

public:
	UActivationAggregatorComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ILevelActivatableInterface
	virtual void ReceiveActivationSignal_Implementation(const FLevelActivationPayload& Payload) override;

	UFUNCTION(BlueprintPure, Category = "Activation")
	bool IsActivated() const { return bIsActivated; }

	UFUNCTION(BlueprintPure, Category = "Activation")
	float GetCurrentProgress() const { return CurrentProgress; }

	UPROPERTY(BlueprintAssignable, Category = "Activation|Events")
	FOnAggregateStateChangedSignature OnAggregateStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Activation|Events")
	FOnAggregateProgressChangedSignature OnAggregateProgressChanged;

	/** Bind warning rumble/flashing VFX here when DeactivationGraceDelay > 0. */
	UPROPERTY(BlueprintAssignable, Category = "Activation|Events")
	FOnDeactivationGraceStartedSignature OnDeactivationGraceStarted;

protected:
	/** Number of distinct active sources required to activate (1 = OR, N = AND). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (ClampMin = "1"))
	int32 RequiredActiveSources = 1;

	/** Once activated, stay permanently open even if minions are recalled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation")
	bool bLockOnceActivated = false;

	/** Delay before deactivating when inputs drop (ideal for bridges so players/minions aren't dropped instantly). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (ClampMin = "0.0", Units = "s"))
	float DeactivationGraceDelay = 0.0f;

private:
	struct FSourceEntry
	{
		bool bActive = false;
		float NormalizedValue = 0.0f;
	};

	void EvaluateAggregate(bool bInstant);
	void CommitStateChange(bool bNewActivated, bool bInstant);

	TMap<TWeakObjectPtr<UObject>, FSourceEntry> SourceStates;
	bool bIsActivated = false;
	bool bLockedOpen = false;
	float CurrentProgress = 0.0f;
	FTimerHandle GraceTimerHandle;
};