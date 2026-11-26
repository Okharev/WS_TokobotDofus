// ActivationAggregatorComponent.cpp
#include "ActivationAggregatorComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UActivationAggregatorComponent::UActivationAggregatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UActivationAggregatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GraceTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UActivationAggregatorComponent::ReceiveActivationSignal_Implementation(const FLevelActivationPayload& Payload)
{
	if (bLockedOpen && !Payload.bInstant)
	{
		return;
	}

	UObject* Key = Payload.SourceObject ? Payload.SourceObject.Get() : this;
	SourceStates.Add(Key, { Payload.bActive, FMath::Clamp(Payload.NormalizedValue, 0.0f, 1.0f) });

	EvaluateAggregate(Payload.bInstant);
}

void UActivationAggregatorComponent::EvaluateAggregate(bool bInstant)
{
	int32 ActiveCount = 0;
	float ValueSum = 0.0f;

	for (auto It = SourceStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
			continue;
		}
		if (It.Value().bActive)
		{
			++ActiveCount;
		}
		ValueSum += It.Value().NormalizedValue;
	}

	const float NewProgress = FMath::Clamp(ValueSum / static_cast<float>(FMath::Max(RequiredActiveSources, 1)), 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(NewProgress, CurrentProgress) || bInstant)
	{
		CurrentProgress = NewProgress;
		OnAggregateProgressChanged.Broadcast(CurrentProgress, bInstant);
	}

	const bool bShouldBeActive = (ActiveCount >= RequiredActiveSources);
	if (bShouldBeActive == bIsActivated && !bInstant)
	{
		// If inputs came back during grace period, cancel impending deactivation.
		if (bShouldBeActive)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(GraceTimerHandle);
			}
		}
		return;
	}

	if (!bShouldBeActive && !bInstant && DeactivationGraceDelay > KINDA_SMALL_NUMBER)
	{
		if (UWorld* World = GetWorld())
		{
			if (!World->GetTimerManager().IsTimerActive(GraceTimerHandle))
			{
				OnDeactivationGraceStarted.Broadcast(DeactivationGraceDelay);
				World->GetTimerManager().SetTimer(
					GraceTimerHandle,
					FTimerDelegate::CreateWeakLambda(this, [this]()
					{
						CommitStateChange(false, false);
					}),
					DeactivationGraceDelay,
					false);
			}
		}
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GraceTimerHandle);
	}
	CommitStateChange(bShouldBeActive, bInstant);
}

void UActivationAggregatorComponent::CommitStateChange(bool bNewActivated, bool bInstant)
{
	bIsActivated = bNewActivated;
	if (bIsActivated && bLockOnceActivated)
	{
		bLockedOpen = true;
	}

	OnAggregateStateChanged.Broadcast(bIsActivated, bInstant);
}