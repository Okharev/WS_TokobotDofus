#include "CombatLockOnTargetComponent.h"
#include "CombatLockOnSubsystem.h"
#include "MinionThrowTargetComponent.h"
#include "HeartHealthComponent.h"
#include "Engine/World.h"

UCombatLockOnTargetComponent::UCombatLockOnTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatLockOnTargetComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UCombatLockOnSubsystem* Sub = World->GetSubsystem<UCombatLockOnSubsystem>())
		{
			Sub->RegisterTarget(this);
		}
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (UHeartHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HealthComp->OnDeath.AddUniqueDynamic(this, &UCombatLockOnTargetComponent::HandleOwnerDeath);
		}
	}
}

void UCombatLockOnTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsCurrentlyLocked)
	{
		NotifyLockFocusState(false);
		OnTargetInvalidated.Broadcast(this, false);
	}

	if (UWorld* World = GetWorld())
	{
		if (UCombatLockOnSubsystem* Sub = World->GetSubsystem<UCombatLockOnSubsystem>())
		{
			Sub->UnregisterTarget(this);
		}
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (UHeartHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HealthComp->OnDeath.RemoveDynamic(this, &UCombatLockOnTargetComponent::HandleOwnerDeath);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool UCombatLockOnTargetComponent::CanBeLockedOn() const
{
	if (!bIsLockable || !IsValid(GetOwner()))
	{
		return false;
	}

	if (const UHeartHealthComponent* HealthComp = GetOwner()->FindComponentByClass<UHeartHealthComponent>())
	{
		if (!HealthComp->IsAlive())
		{
			return false;
		}
	}

	return true;
}

void UCombatLockOnTargetComponent::SetLockable(bool bNewLockable)
{
	if (bIsLockable == bNewLockable)
	{
		return;
	}

	bIsLockable = bNewLockable;
	if (!bIsLockable && bIsCurrentlyLocked)
	{
		NotifyLockFocusState(false);
		OnTargetInvalidated.Broadcast(this, false);
	}
}

FVector UCombatLockOnTargetComponent::GetLockWorldLocation() const
{
	return GetComponentLocation() + LockWorldOffset;
}

UMinionThrowTargetComponent* UCombatLockOnTargetComponent::ResolveLinkedThrowTarget() const
{
	if (IsValid(LinkedThrowTarget) && LinkedThrowTarget->CanAcceptMinion())
	{
		return LinkedThrowTarget;
	}

	// Fallback: inspect all UMinionThrowTargetComponents on the same enemy (supports multi-weakpoint bosses)
	if (const AActor* OwnerActor = GetOwner())
	{
		TInlineComponentArray<UMinionThrowTargetComponent*, 4> ThrowTargets;
		OwnerActor->GetComponents<UMinionThrowTargetComponent>(ThrowTargets);

		UMinionThrowTargetComponent* BestThrowTarget = nullptr;
		float BestPriority = -1.0f;

		for (UMinionThrowTargetComponent* Candidate : ThrowTargets)
		{
			if (IsValid(Candidate) && Candidate->CanAcceptMinion())
			{
				if (!BestThrowTarget || Candidate->GetPriorityWeight() > BestPriority)
				{
					BestThrowTarget = Candidate;
					BestPriority = Candidate->GetPriorityWeight();
				}
			}
		}
		return BestThrowTarget;
	}

	return nullptr;
}

void UCombatLockOnTargetComponent::NotifyLockFocusState(bool bNewIsLocked)
{
	if (bIsCurrentlyLocked != bNewIsLocked)
	{
		bIsCurrentlyLocked = bNewIsLocked;
		OnLockFocusChanged.Broadcast(bIsCurrentlyLocked);
	}
}

void UCombatLockOnTargetComponent::HandleOwnerDeath(const FCombatHitPayload& KillingHitPayload)
{
	bIsLockable = false;
	NotifyLockFocusState(false);
	OnTargetInvalidated.Broadcast(this, true);
}
