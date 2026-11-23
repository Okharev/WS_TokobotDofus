#include "MinionThrowTargetComponent.h"
#include "MinionThrowTargetSubsystem.h"
#include "MinionAgentInterface.h"
#include "HitReactableInterface.h"
#include "HeartHealthComponent.h"
#include "HitFeedbackSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

UMinionThrowTargetComponent::UMinionThrowTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMinionThrowTargetComponent::BeginPlay()
{
	Super::BeginPlay();

	EnsureSlotArraySized();

	if (UWorld* World = GetWorld())
	{
		if (UMinionThrowTargetSubsystem* Sub = World->GetSubsystem<UMinionThrowTargetSubsystem>())
		{
			Sub->RegisterTarget(this);
		}
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (UHeartHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HealthComp->OnDeath.AddUniqueDynamic(this, &UMinionThrowTargetComponent::HandleOwnerDeath);
		}
	}
}

void UMinionThrowTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		for (FMinionSlotEntry& Entry : SlotEntries)
		{
			World->GetTimerManager().ClearTimer(Entry.TimedLatchHandle);
		}

		if (UMinionThrowTargetSubsystem* Sub = World->GetSubsystem<UMinionThrowTargetSubsystem>())
		{
			Sub->UnregisterTarget(this);
		}
	}

	if (AActor* OwnerActor = GetOwner())
	{
		if (UHeartHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HealthComp->OnDeath.RemoveDynamic(this, &UMinionThrowTargetComponent::HandleOwnerDeath);
		}
	}

	ReleaseAllMinions(220.0f, true);

	Super::EndPlay(EndPlayReason);
}

void UMinionThrowTargetComponent::EnsureSlotArraySized()
{
	const int32 DesiredCount = FMath::Max(MaxMinionSlots, 1);
	if (SlotEntries.Num() != DesiredCount)
	{
		SlotEntries.SetNum(DesiredCount);
	}
}

bool UMinionThrowTargetComponent::CanAcceptMinion() const
{
	if (!bIsTargetable || !IsValid(GetOwner()))
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

	return PeekNextAvailableSlotIndex() != INDEX_NONE;
}

void UMinionThrowTargetComponent::SetTargetable(bool bNewTargetable)
{
	bIsTargetable = bNewTargetable;
	if (!bIsTargetable)
	{
		NotifyFocusState(false, false);
	}
}

int32 UMinionThrowTargetComponent::GetLatchedMinionCount() const
{
	int32 Count = 0;
	for (const FMinionSlotEntry& Entry : SlotEntries)
	{
		if (Entry.Minion.IsValid() && Entry.bIsLatched)
		{
			++Count;
		}
	}
	return Count;
}

int32 UMinionThrowTargetComponent::GetReservedAndLatchedCount() const
{
	int32 Count = 0;
	for (const FMinionSlotEntry& Entry : SlotEntries)
	{
		if (Entry.Minion.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

int32 UMinionThrowTargetComponent::PeekNextAvailableSlotIndex() const
{
	const int32 MaxCount = FMath::Max(MaxMinionSlots, 1);
	for (int32 i = 0; i < MaxCount; ++i)
	{
		if (!SlotEntries.IsValidIndex(i) || !SlotEntries[i].Minion.IsValid())
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FVector UMinionThrowTargetComponent::GetSlotWorldLocation(int32 SlotIndex) const
{
	return GetSlotWorldTransform(SlotIndex).GetLocation();
}

FTransform UMinionThrowTargetComponent::GetSlotWorldTransform(int32 SlotIndex) const
{
	FVector LocalOffset = FVector::ZeroVector;
	if (LocalSlotOffsets.IsValidIndex(SlotIndex))
	{
		LocalOffset = LocalSlotOffsets[SlotIndex];
	}
	else if (MaxMinionSlots > 1 && AutoSlotRingRadius > KINDA_SMALL_NUMBER)
	{
		const float AngleRad = (2.0f * PI * static_cast<float>(SlotIndex)) / static_cast<float>(MaxMinionSlots);
		LocalOffset = FVector(FMath::Cos(AngleRad) * AutoSlotRingRadius, FMath::Sin(AngleRad) * AutoSlotRingRadius, 0.0f);
	}

	const FTransform CompWorld = GetComponentTransform();
	return FTransform(CompWorld.GetRotation(), CompWorld.TransformPosition(LocalOffset), FVector::OneVector);
}

bool UMinionThrowTargetComponent::TryReserveSlot(AActor* IncomingMinion, int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;
	if (!IsValid(IncomingMinion) || !CanAcceptMinion())
	{
		return false;
	}

	EnsureSlotArraySized();

	for (int32 i = 0; i < SlotEntries.Num(); ++i)
	{
		if (!SlotEntries[i].Minion.IsValid())
		{
			SlotEntries[i].Minion = IncomingMinion;
			SlotEntries[i].bIsLatched = false;
			OutSlotIndex = i;
			return true;
		}
	}

	return false;
}

void UMinionThrowTargetComponent::CancelReservation(AActor* IncomingMinion)
{
	if (!IncomingMinion)
	{
		return;
	}

	const int32 PrevLatched = GetLatchedMinionCount();
	bool bWasLatched = false;

	for (FMinionSlotEntry& Entry : SlotEntries)
	{
		if (Entry.Minion.Get() == IncomingMinion)
		{
			bWasLatched = Entry.bIsLatched;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(Entry.TimedLatchHandle);
			}
			Entry.Minion.Reset();
			Entry.bIsLatched = false;
			break;
		}
	}

	if (bWasLatched)
	{
		OnMinionDetached.Broadcast(IncomingMinion, GetLatchedMinionCount(), MaxMinionSlots);
		EvaluateThresholdAndSlow(PrevLatched);
	}
}

void UMinionThrowTargetComponent::NotifyMinionArrived(
	AActor* MinionActor,
	int32 SlotIndex,
	const FVector& FlightDirection,
	AActor* InstigatorActor)
{
	if (!IsValid(MinionActor))
	{
		return;
	}

	IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor);
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();

	OnMinionImpact.Broadcast(MinionActor, InstigatorActor);

	// 1. Optional combat damage & BOTW HitStop feedback (reuses existing pipeline)
	bool bTargetKilled = false;
	if (bApplyCombatHitOnArrival && IsValid(OwnerActor) && World)
	{
		const FVector FlatDir = FlightDirection.GetSafeNormal2D();
		const FVector KnockDir = FlatDir.IsNearlyZero() ? GetForwardVector().GetSafeNormal2D() : FlatDir;

		FCombatHitPayload Payload;
		Payload.InstigatorActor = InstigatorActor;
		Payload.DamageCauser = MinionActor;
		Payload.TargetActor = OwnerActor;
		Payload.ImpactPoint = GetSlotWorldLocation(SlotIndex);
		Payload.ImpactNormal = -FlightDirection.GetSafeNormal();
		Payload.KnockbackDirection = KnockDir;
		Payload.KnockbackVelocity = (KnockDir * ThrowHitFeedback.KnockbackHorizontalStrength) +
			FVector(0.0f, 0.0f, ThrowHitFeedback.KnockbackVerticalStrength);
		Payload.HeartDamage = ThrowHitFeedback.HeartDamage;
		Payload.WhipChainIndex = 0;
		Payload.StaggerDuration = ThrowHitFeedback.StaggerDuration;
		Payload.AppliedHitStopDuration = ThrowHitFeedback.HitStopDuration;

		FCombatHitResponse Response;
		bool bHandled = false;

		if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(OwnerActor))
		{
			Response = Reactable->ReceiveCombatHit(Payload);
			bHandled = true;
		}
		else if (UHeartHealthComponent* HealthComp = OwnerActor->FindComponentByClass<UHeartHealthComponent>())
		{
			Response = HealthComp->ApplyCombatHit(Payload);
			bHandled = true;
		}

		if (bHandled && Response.bHitAccepted)
		{
			Payload.RemainingHearts = Response.RemainingHearts;
			Payload.MaxHearts = Response.MaxHearts;
			Payload.bIsKillingBlow = Response.bWasKillingBlow;
			Payload.AppliedHitStopDuration = Response.bWasKillingBlow
				? ThrowHitFeedback.KillHitStopDuration
				: ThrowHitFeedback.HitStopDuration;

			bTargetKilled = Response.bWasKillingBlow;

			if (UHitFeedbackSubsystem* FeedbackSub = World->GetSubsystem<UHitFeedbackSubsystem>())
			{
				FeedbackSub->TriggerCombatFeedback(Payload, ThrowHitFeedback);
			}
		}
	}

	// 2. Bounce off immediately if configured as ImpactAndBounce OR if the hit killed the owner
	const bool bShouldLatch = !bTargetKilled &&
		bIsTargetable &&
		(ImpactBehavior != EMinionThrowImpactBehavior::ImpactAndBounce) &&
		SlotEntries.IsValidIndex(SlotIndex) &&
		SlotEntries[SlotIndex].Minion.Get() == MinionActor;

	if (!bShouldLatch)
	{
		CancelReservation(MinionActor);
		if (MinionInterface)
		{
			const FVector BackDir = (-FlightDirection).GetSafeNormal2D();
			const FVector SafeBack = BackDir.IsNearlyZero() ? GetForwardVector().GetSafeNormal2D() : BackDir;
			const FVector BounceVel = (SafeBack * BounceBackSpeed) + FVector(0.0f, 0.0f, BounceUpSpeed);
			MinionInterface->DetachFromThrowTarget(BounceVel, false);
		}
		return;
	}

	// 3. Latch minion cleanly into its slot
	const int32 PrevLatched = GetLatchedMinionCount();
	SlotEntries[SlotIndex].bIsLatched = true;

	if (MinionInterface)
	{
		MinionInterface->ConfirmLatchedToThrowTarget(this, SlotIndex);
	}

	if (ImpactBehavior == EMinionThrowImpactBehavior::LatchTimed && World)
	{
		FTimerDelegate ExpireDelegate;
		ExpireDelegate.BindUObject(this, &UMinionThrowTargetComponent::HandleTimedLatchExpired, SlotIndex);
		World->GetTimerManager().SetTimer(SlotEntries[SlotIndex].TimedLatchHandle, ExpireDelegate, TimedLatchDuration, false);
	}

	OnMinionLatched.Broadcast(MinionActor, GetLatchedMinionCount(), MaxMinionSlots);
	EvaluateThresholdAndSlow(PrevLatched);
}

void UMinionThrowTargetComponent::ReleaseMinion(AActor* MinionActor, const FVector& PopVelocity, bool bSnapWithoutPop)
{
	if (!IsValid(MinionActor))
	{
		return;
	}

	const int32 PrevLatched = GetLatchedMinionCount();
	bool bWasLatched = false;
	bool bFoundInSlots = false;

	for (FMinionSlotEntry& Entry : SlotEntries)
	{
		if (Entry.Minion.Get() == MinionActor)
		{
			bFoundInSlots = true;
			bWasLatched = Entry.bIsLatched;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(Entry.TimedLatchHandle);
			}
			Entry.Minion.Reset();
			Entry.bIsLatched = false;
			break;
		}
	}

	if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
	{
		FVector FinalPop = PopVelocity;
		if (!bSnapWithoutPop && FinalPop.IsNearlyZero())
		{
			FVector OutDir = (MinionActor->GetActorLocation() - GetComponentLocation()).GetSafeNormal2D();
			if (OutDir.IsNearlyZero())
			{
				OutDir = GetForwardVector().GetSafeNormal2D();
			}
			FinalPop = (OutDir * BounceBackSpeed) + FVector(0.0f, 0.0f, BounceUpSpeed * 0.85f);
		}
		MinionInterface->DetachFromThrowTarget(FinalPop, bSnapWithoutPop);
	}

	if (bFoundInSlots && bWasLatched)
	{
		OnMinionDetached.Broadcast(MinionActor, GetLatchedMinionCount(), MaxMinionSlots);
		EvaluateThresholdAndSlow(PrevLatched);
	}
}

void UMinionThrowTargetComponent::ReleaseAllMinions(float RadialPopSpeed, bool bSnapWithoutPop)
{
	TArray<AActor*, TInlineAllocator<8>> ToRelease;
	for (const FMinionSlotEntry& Entry : SlotEntries)
	{
		if (Entry.Minion.IsValid())
		{
			ToRelease.Add(Entry.Minion.Get());
		}
	}

	const int32 Count = ToRelease.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		FVector PopVel = FVector::ZeroVector;
		if (!bSnapWithoutPop)
		{
			const float Angle = (Count > 0) ? (2.0f * PI * static_cast<float>(i) / static_cast<float>(Count)) : 0.0f;
			const FVector RadialDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			PopVel = (RadialDir * RadialPopSpeed) + FVector(0.0f, 0.0f, BounceUpSpeed * 0.85f);
		}
		ReleaseMinion(ToRelease[i], PopVel, bSnapWithoutPop);
	}
}

void UMinionThrowTargetComponent::HandleOwnerDeath(const FCombatHitPayload& KillingHitPayload)
{
	SetTargetable(false);
	ReleaseAllMinions(320.0f, false);
}

void UMinionThrowTargetComponent::HandleTimedLatchExpired(int32 SlotIndex)
{
	if (SlotEntries.IsValidIndex(SlotIndex) && SlotEntries[SlotIndex].Minion.IsValid())
	{
		ReleaseMinion(SlotEntries[SlotIndex].Minion.Get(), FVector::ZeroVector, false);
	}
}

void UMinionThrowTargetComponent::EvaluateThresholdAndSlow(int32 PreviousLatchedCount)
{
	const int32 NewLatchedCount = GetLatchedMinionCount();
	const bool bNowMet = NewLatchedCount >= RequiredMinionsForThreshold;

	if (bNowMet != bLastThresholdMet)
	{
		bLastThresholdMet = bNowMet;
		OnThresholdStateChanged.Broadcast(bNowMet);
	}

	if (bApplyMovementSlowToOwner)
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
			{
				if (PreviousLatchedCount == 0 && NewLatchedCount > 0 && !bCachedBaseWalkSpeed)
				{
					CachedOwnerBaseWalkSpeed = MoveComp->MaxWalkSpeed;
					bCachedBaseWalkSpeed = true;
				}

				if (bCachedBaseWalkSpeed)
				{
					if (NewLatchedCount == 0)
					{
						MoveComp->MaxWalkSpeed = CachedOwnerBaseWalkSpeed;
						bCachedBaseWalkSpeed = false;
					}
					else
					{
						const float Multiplier = FMath::Clamp(
							1.0f - (static_cast<float>(NewLatchedCount) * SlowFractionPerMinion),
							MinWalkSpeedMultiplier,
							1.0f);
						MoveComp->MaxWalkSpeed = CachedOwnerBaseWalkSpeed * Multiplier;
					}
				}
			}
		}
	}
}

void UMinionThrowTargetComponent::NotifyFocusState(bool bIsLocked, bool bInCandidateCone)
{
	if (bIsLocked != bLastLockedState || bInCandidateCone != bLastInConeState)
	{
		bLastLockedState = bIsLocked;
		bLastInConeState = bInCandidateCone;
		OnFocusChanged.Broadcast(bIsLocked, bInCandidateCone);
	}
}