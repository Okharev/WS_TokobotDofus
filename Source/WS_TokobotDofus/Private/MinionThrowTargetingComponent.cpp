#include "MinionThrowTargetingComponent.h"
#include "MinionThrowTargetSubsystem.h"
#include "MinionThrowTargetComponent.h"
#include "MinionSquadComponent.h"
#include "MinionAgentInterface.h"
#include "CombatLockOnComponent.h"
#include "CombatLockOnTargetComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Engine/World.h"

UMinionThrowTargetingComponent::UMinionThrowTargetingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMinionThrowTargetingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		CachedSquadComponent = OwnerActor->FindComponentByClass<UMinionSquadComponent>();
		CachedLockOnComponent = OwnerActor->FindComponentByClass<UCombatLockOnComponent>();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			EvaluationTimerHandle,
			this,
			&UMinionThrowTargetingComponent::EvaluateTargets,
			EvaluationInterval,
			true);
	}

	UpdateDebugTickState();
}

void UMinionThrowTargetingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}

	if (LockedTarget.IsValid())
	{
		LockedTarget->NotifyFocusState(false, false);
		LockedTarget.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UMinionThrowTargetingComponent::UpdateDebugTickState()
{
	const bool bNeedsTick = bDebugDrawTargeting || bDebugDrawThrowArc;
	SetComponentTickEnabled(bNeedsTick);
}

UMinionThrowTargetComponent* UMinionThrowTargetingComponent::ResolveCombatLockedThrowTarget() const
{
	if (!bPrioritizeCombatLockTarget || !CachedLockOnComponent)
	{
		return nullptr;
	}

	if (const UCombatLockOnTargetComponent* CombatTarget = CachedLockOnComponent->GetLockedTarget())
	{
		return CombatTarget->ResolveLinkedThrowTarget();
	}

	return nullptr;
}

bool UMinionThrowTargetingComponent::GetPlayerViewPoint(FVector& OutViewLoc, FVector& OutViewForward) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		if (const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				OutViewLoc = PC->PlayerCameraManager->GetCameraLocation();
				OutViewForward = PC->PlayerCameraManager->GetCameraRotation().Vector();
				return true;
			}

			FRotator ViewRot;
			PC->GetPlayerViewPoint(OutViewLoc, ViewRot);
			OutViewForward = ViewRot.Vector();
			return true;
		}
	}

	OutViewLoc = OwnerActor->GetActorLocation();
	OutViewForward = OwnerActor->GetActorForwardVector();
	return true;
}

FVector UMinionThrowTargetingComponent::GetLaunchOriginWorld() const
{
	if (const AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->GetActorTransform().TransformPosition(PlayerLaunchLocalOffset);
	}
	return FVector::ZeroVector;
}

bool UMinionThrowTargetingComponent::HasLineOfSightToTarget(
	const FVector& TraceStart,
	const UMinionThrowTargetComponent* TargetComp) const
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor || !TargetComp)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MinionThrowLOS), false, OwnerActor);
	Params.AddIgnoredActor(OwnerActor);
	if (AActor* TargetOwner = TargetComp->GetOwner())
	{
		Params.AddIgnoredActor(TargetOwner);
	}
	if (CachedSquadComponent)
	{
		for (AActor* Minion : CachedSquadComponent->GetActiveMinions())
		{
			if (IsValid(Minion))
			{
				Params.AddIgnoredActor(Minion);
			}
		}
	}

	FHitResult Hit;
	const FVector TraceEnd = TargetComp->GetComponentLocation();
	return !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, LineOfSightChannel, Params);
}

void UMinionThrowTargetingComponent::EvaluateTargets()
{
	UpdateDebugTickState();

	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	UMinionThrowTargetSubsystem* Sub = World->GetSubsystem<UMinionThrowTargetSubsystem>();
	if (!Sub)
	{
		return;
	}

	FVector CamLoc, CamForward;
	if (!GetPlayerViewPoint(CamLoc, CamForward))
	{
		return;
	}

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	const FVector LaunchOrigin = GetLaunchOriginWorld();
	const float CosHalfCone = FMath::Cos(FMath::DegreesToRadians(AimConeHalfAngleDegrees));
	const UMinionThrowTargetComponent* CombatLockedThrowTarget = ResolveCombatLockedThrowTarget();

	TArray<TWeakObjectPtr<UMinionThrowTargetComponent>> PrevCandidates = MoveTemp(ActiveConeCandidates);
	ActiveConeCandidates.Reset();
	OccludedConeCandidates.Reset();

	TArray<FCandidateScore, TInlineAllocator<16>> ScoredCandidates;

	for (const TWeakObjectPtr<UMinionThrowTargetComponent>& WeakTarget : Sub->GetRegisteredTargets())
	{
		UMinionThrowTargetComponent* TargetComp = WeakTarget.Get();
		if (!IsValid(TargetComp) || !TargetComp->CanAcceptMinion())
		{
			continue;
		}

		const bool bIsCombatLockedTarget = (TargetComp == CombatLockedThrowTarget);
		const FVector TargetLoc = TargetComp->GetComponentLocation();
		const float EffectiveMaxRange = FMath::Min(MaxGlobalThrowRange, TargetComp->GetMaxTargetRange());
		const float DistFromPlayerSq = FVector::DistSquared(PlayerLoc, TargetLoc);
		if (DistFromPlayerSq > FMath::Square(EffectiveMaxRange))
		{
			continue;
		}

		const FVector CamToTargetDir = (TargetLoc - CamLoc).GetSafeNormal();
		const float Dot = FVector::DotProduct(CamForward, CamToTargetDir);

		if (Dot < CosHalfCone && !(bIsCombatLockedTarget && bAllowLockedTargetOutsideCone))
		{
			continue;
		}

		const float DistFromPlayer = FMath::Sqrt(DistFromPlayerSq);
		const float AngleNorm = bIsCombatLockedTarget
			                        ? 1.0f
			                        : FMath::Clamp(
				                        (Dot - CosHalfCone) / FMath::Max(1.0f - CosHalfCone, KINDA_SMALL_NUMBER), 0.0f,
				                        1.0f);
		const float DistNorm = 1.0f - FMath::Clamp(DistFromPlayer / FMath::Max(EffectiveMaxRange, 1.0f), 0.0f, 1.0f);
		const float PriorityNorm = FMath::Clamp(TargetComp->GetPriorityWeight() * 0.5f, 0.0f, 1.0f);

		float Score = (0.65f * AngleNorm) + (0.25f * DistNorm) + (0.10f * PriorityNorm);
		if (TargetComp == LockedTarget.Get())
		{
			Score += LockHysteresisBonus;
		}
		if (bIsCombatLockedTarget)
		{
			Score += CombatLockScoreBonus;
		}

		ScoredCandidates.Add({TargetComp, Score, DistFromPlayer});
	}

	ScoredCandidates.Sort([](const FCandidateScore& A, const FCandidateScore& B)
	{
		return A.Score > B.Score;
	});

	UMinionThrowTargetComponent* BestTarget = nullptr;
	float BestScore = 0.0f;

	// Deferred LOS check: validate top-scored candidates first
	for (const FCandidateScore& Candidate : ScoredCandidates)
	{
		UMinionThrowTargetComponent* TargetComp = Candidate.Target.Get();
		if (!TargetComp)
		{
			continue;
		}

		if (HasLineOfSightToTarget(LaunchOrigin, TargetComp))
		{
			ActiveConeCandidates.Add(TargetComp);
			if (!BestTarget)
			{
				BestTarget = TargetComp;
				BestScore = Candidate.Score;
			}
		}
		else
		{
			OccludedConeCandidates.Add(TargetComp);
		}
	}

	// Clear focus on any previous candidate no longer in cone
	for (const TWeakObjectPtr<UMinionThrowTargetComponent>& OldWeak : PrevCandidates)
	{
		if (UMinionThrowTargetComponent* OldComp = OldWeak.Get())
		{
			if (!ActiveConeCandidates.Contains(OldWeak))
			{
				OldComp->NotifyFocusState(false, false);
			}
		}
	}

	// Update active cone candidates
	for (const TWeakObjectPtr<UMinionThrowTargetComponent>& CandWeak : ActiveConeCandidates)
	{
		if (UMinionThrowTargetComponent* CandComp = CandWeak.Get())
		{
			CandComp->NotifyFocusState(CandComp == BestTarget, true);
		}
	}

	UMinionThrowTargetComponent* PrevLocked = LockedTarget.Get();
	if (BestTarget != PrevLocked)
	{
		if (IsValid(PrevLocked) && !ActiveConeCandidates.Contains(PrevLocked))
		{
			PrevLocked->NotifyFocusState(false, false);
		}

		LockedTarget = BestTarget;
		LockedTargetScore = BestScore;
		OnLockedTargetChanged.Broadcast(BestTarget, PrevLocked);
	}
	else
	{
		LockedTargetScore = BestScore;
	}
}

bool UMinionThrowTargetingComponent::TryThrowMinionAtLockedTarget()
{
	// Ensure lock state is up to date on tap
	if (!LockedTarget.IsValid() || !LockedTarget->CanAcceptMinion())
	{
		EvaluateTargets();
	}

	UMinionThrowTargetComponent* TargetComp = LockedTarget.Get();
	if (!IsValid(TargetComp) || !TargetComp->CanAcceptMinion())
	{
		return false;
	}

	if (!CachedSquadComponent)
	{
		CachedSquadComponent = GetOwner() ? GetOwner()->FindComponentByClass<UMinionSquadComponent>() : nullptr;
	}
	if (!CachedSquadComponent)
	{
		return false;
	}

	const FVector LaunchOrigin = GetLaunchOriginWorld();
	AActor* ChosenMinion = CachedSquadComponent->FindBestThrowableMinion(LaunchOrigin, MaxMinionGrabRadius);
	if (!IsValid(ChosenMinion))
	{
		return false;
	}

	IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(ChosenMinion);
	if (!MinionInterface)
	{
		return false;
	}

	int32 ReservedSlot = INDEX_NONE;
	if (!TargetComp->TryReserveSlot(ChosenMinion, ReservedSlot))
	{
		return false;
	}

	if (!MinionInterface->LaunchAtThrowTarget(
		TargetComp,
		ReservedSlot,
		LaunchOrigin,
		ThrowFlightSpeed,
		ThrowArcHeight,
		GetOwner()))
	{
		TargetComp->CancelReservation(ChosenMinion);
		return false;
	}

	OnMinionThrown.Broadcast(ChosenMinion, TargetComp);

	// Re-score immediately so reticle advances if that target is now full
	EvaluateTargets();
	return true;
}

bool UMinionThrowTargetingComponent::GetThrowArcPreviewPoints(TArray<FVector>& OutPoints, int32 NumSegments) const
{
	OutPoints.Reset();

	const UMinionThrowTargetComponent* TargetComp = LockedTarget.Get();
	if (!IsValid(TargetComp) || !TargetComp->CanAcceptMinion())
	{
		return false;
	}

	const int32 NextSlot = TargetComp->PeekNextAvailableSlotIndex();
	if (NextSlot == INDEX_NONE)
	{
		return false;
	}

	const FVector StartPos = GetLaunchOriginWorld();
	const FVector EndPos = TargetComp->GetSlotWorldLocation(NextSlot);
	const int32 SafeSegments = FMath::Max(NumSegments, 4);

	OutPoints.Reserve(SafeSegments + 1);
	for (int32 i = 0; i <= SafeSegments; ++i)
	{
		const float Alpha = static_cast<float>(i) / static_cast<float>(SafeSegments);
		FVector Point = FMath::Lerp(StartPos, EndPos, Alpha);
		Point.Z += 4.0f * ThrowArcHeight * Alpha * (1.0f - Alpha);
		OutPoints.Add(Point);
	}

	return true;
}

void UMinionThrowTargetingComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bDebugDrawTargeting)
	{
		// 1. Occluded targets in view cone (Red)
		for (const TWeakObjectPtr<UMinionThrowTargetComponent>& WeakOcc : OccludedConeCandidates)
		{
			if (const UMinionThrowTargetComponent* OccComp = WeakOcc.Get())
			{
				DrawDebugSphere(World, OccComp->GetComponentLocation(), 20.0f, 8, FColor::Red, false, -1.0f, 0, 1.5f);
			}
		}

		// 2. Visible non-locked candidates in view cone (Yellow)
		for (const TWeakObjectPtr<UMinionThrowTargetComponent>& WeakCand : ActiveConeCandidates)
		{
			const UMinionThrowTargetComponent* CandComp = WeakCand.Get();
			if (!CandComp || CandComp == LockedTarget.Get())
			{
				continue;
			}
			DrawDebugSphere(World, CandComp->GetComponentLocation(), 26.0f, 10, FColor::Yellow, false, -1.0f, 0, 1.5f);
		}

		// 3. Currently Locked Target (Green/Cyan reticle + text info)
		if (const UMinionThrowTargetComponent* TargetComp = LockedTarget.Get())
		{
			const FVector TargetLoc = TargetComp->GetComponentLocation();
			const int32 NextSlot = TargetComp->PeekNextAvailableSlotIndex();
			const FVector SlotLoc = (NextSlot != INDEX_NONE) ? TargetComp->GetSlotWorldLocation(NextSlot) : TargetLoc;

			DrawDebugSphere(World, TargetLoc, 36.0f, 12, FColor::Green, false, -1.0f, 0, 3.0f);
			DrawDebugBox(World, SlotLoc, FVector(16.0f), TargetComp->GetComponentQuat(), FColor::Cyan, false, -1.0f, 0,
			             2.5f);

			const float DistMeters = FVector::Dist(GetLaunchOriginWorld(), TargetLoc) * 0.01f;
			const FString Label = FString::Printf(
				TEXT("LOCKED: %s (%s)\nSlots: %d/%d | %.1fm | Score: %.2f"),
				*GetNameSafe(TargetComp->GetOwner()),
				*TargetComp->GetName(),
				TargetComp->GetReservedAndLatchedCount(),
				TargetComp->GetMaxMinionSlots(),
				DistMeters,
				LockedTargetScore);

			DrawDebugString(World, TargetLoc + FVector(0.0f, 0.0f, 55.0f), Label, nullptr, FColor::Green, 0.0f, true,
			                1.0f);
		}
	}

	// 4. Live Parabolic Throw Arc Preview
	if (bDebugDrawThrowArc && LockedTarget.IsValid())
	{
		TArray<FVector> ArcPoints;
		if (GetThrowArcPreviewPoints(ArcPoints, DebugArcSegments))
		{
			const bool bHasThrowableMinion = CachedSquadComponent &&
				IsValid(CachedSquadComponent->FindBestThrowableMinion(GetLaunchOriginWorld(), MaxMinionGrabRadius));

			const FColor ArcColor = bHasThrowableMinion ? FColor::Cyan : FColor::Orange;

			for (int32 i = 0; i < ArcPoints.Num() - 1; ++i)
			{
				DrawDebugLine(World, ArcPoints[i], ArcPoints[i + 1], ArcColor, false, -1.0f, 0, 2.5f);
			}

			// Launch origin & landing impact markers
			DrawDebugSphere(World, ArcPoints[0], 10.0f, 8, ArcColor, false, -1.0f, 0, 1.5f);
			DrawDebugSphere(World, ArcPoints.Last(), 14.0f, 10, ArcColor, false, -1.0f, 0, 2.0f);
		}
	}
}
