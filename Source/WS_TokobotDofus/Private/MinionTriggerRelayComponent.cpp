// MinionTriggerRelayComponent.cpp
#include "MinionTriggerRelayComponent.h"
#include "MinionThrowTargetComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UMinionTriggerRelayComponent::UMinionTriggerRelayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMinionTriggerRelayComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		CachedThrowTarget = Owner->FindComponentByClass<UMinionThrowTargetComponent>();
	}

	if (!CachedThrowTarget)
	{
		return;
	}

	switch (TriggerMode)
	{
	case EMinionTriggerMode::HoldThreshold:
		CachedThrowTarget->OnThresholdStateChanged.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleThresholdChanged);
		break;

	case EMinionTriggerMode::OneShot:
		CachedThrowTarget->OnThresholdStateChanged.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleThresholdChanged);
		CachedThrowTarget->OnMinionImpact.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleMinionImpact);
		break;

	case EMinionTriggerMode::Toggle:
		CachedThrowTarget->OnMinionImpact.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleMinionImpact);
		break;

	case EMinionTriggerMode::ProportionalToLatched:
		CachedThrowTarget->OnMinionLatched.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleLatchedCountChanged);
		CachedThrowTarget->OnMinionDetached.AddUniqueDynamic(this, &UMinionTriggerRelayComponent::HandleLatchedCountChanged);
		break;
	}
}

void UMinionTriggerRelayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReleaseDelayHandle);
	}

	if (CachedThrowTarget)
	{
		CachedThrowTarget->OnThresholdStateChanged.RemoveAll(this);
		CachedThrowTarget->OnMinionImpact.RemoveAll(this);
		CachedThrowTarget->OnMinionLatched.RemoveAll(this);
		CachedThrowTarget->OnMinionDetached.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UMinionTriggerRelayComponent::ForceTriggerState(bool bNewActive, float NormalizedValue, bool bInstant)
{
	bCurrentActive = bNewActive;
	if (TriggerMode == EMinionTriggerMode::OneShot && bNewActive)
	{
		bHasFiredOneShot = true;
		if (bConsumeTargetOnOneShot && CachedThrowTarget)
		{
			CachedThrowTarget->SetTargetable(false);
		}
	}

	DispatchSignal(bCurrentActive, NormalizedValue, bInstant);
}

void UMinionTriggerRelayComponent::HandleThresholdChanged(bool bThresholdMet)
{
	if (TriggerMode == EMinionTriggerMode::HoldThreshold)
	{
		bCurrentActive = bThresholdMet;
		DispatchSignal(bCurrentActive, bCurrentActive ? 1.0f : 0.0f);
		return;
	}

	if (TriggerMode == EMinionTriggerMode::OneShot && bThresholdMet && !bHasFiredOneShot)
	{
		bHasFiredOneShot = true;
		bCurrentActive = true;
		DispatchSignal(true, 1.0f);
		ConsumeAndReleaseTarget();
	}
}

void UMinionTriggerRelayComponent::HandleMinionImpact(AActor* MinionActor, AActor* InstigatorActor)
{
	if (TriggerMode == EMinionTriggerMode::Toggle)
	{
		bCurrentActive = !bCurrentActive;
		DispatchSignal(bCurrentActive, bCurrentActive ? 1.0f : 0.0f);
		return;
	}

	// Allows ImpactAndBounce switches (which don't latch/fire threshold) to work as OneShot triggers too.
	if (TriggerMode == EMinionTriggerMode::OneShot && !bHasFiredOneShot)
	{
		if (CachedThrowTarget && CachedThrowTarget->GetMaxMinionSlots() <= 1)
		{
			bHasFiredOneShot = true;
			bCurrentActive = true;
			DispatchSignal(true, 1.0f);
			ConsumeAndReleaseTarget();
		}
	}
}

void UMinionTriggerRelayComponent::HandleLatchedCountChanged(AActor* MinionActor, int32 CurrentLatchedCount, int32 MaxSlots)
{
	const float Denom = static_cast<float>(FMath::Max(MaxSlots, 1));
	const float Alpha = FMath::Clamp(static_cast<float>(CurrentLatchedCount) / Denom, 0.0f, 1.0f);
	bCurrentActive = (CurrentLatchedCount >= MaxSlots);
	DispatchSignal(bCurrentActive, Alpha);
}

void UMinionTriggerRelayComponent::ConsumeAndReleaseTarget()
{
	if (!bConsumeTargetOnOneShot || !CachedThrowTarget)
	{
		return;
	}

	// Immediately disable lock-on so rapid tapping doesn't queue extra minions during the delay.
	CachedThrowTarget->SetTargetable(false);

	if (ReleaseDelayAfterOneShot <= KINDA_SMALL_NUMBER)
	{
		CachedThrowTarget->ReleaseAllMinions();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ReleaseDelayHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (IsValid(CachedThrowTarget))
				{
					CachedThrowTarget->ReleaseAllMinions();
				}
			}),
			ReleaseDelayAfterOneShot,
			false);
	}
}

void UMinionTriggerRelayComponent::DispatchSignal(bool bActive, float NormalizedValue, bool bInstant)
{
	FLevelActivationPayload Payload;
	Payload.SourceObject = this;
	Payload.bActive = bActive;
	Payload.NormalizedValue = FMath::Clamp(NormalizedValue, 0.0f, 1.0f);
	Payload.bInstant = bInstant;

	for (const TSoftObjectPtr<AActor>& TargetSoftPtr : LinkedTargets)
	{
		AActor* TargetActor = TargetSoftPtr.Get();
		if (!IsValid(TargetActor))
		{
			continue;
		}

		// 1. Notify the Actor itself if it implements ILevelActivatableInterface (custom Blueprint gimmicks).
		if (TargetActor->Implements<ULevelActivatableInterface>())
		{
			ILevelActivatableInterface::Execute_ReceiveActivationSignal(TargetActor, Payload);
		}

		// 2. Notify any component implementing the interface (e.g., UActivationAggregatorComponent).
		const TArray<UActorComponent*> ReceiverComps = TargetActor->GetComponentsByInterface(ULevelActivatableInterface::StaticClass());
		for (UActorComponent* Comp : ReceiverComps)
		{
			ILevelActivatableInterface::Execute_ReceiveActivationSignal(Comp, Payload);
		}
	}
}