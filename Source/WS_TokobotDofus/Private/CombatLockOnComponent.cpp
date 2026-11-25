#include "CombatLockOnComponent.h"
#include "CombatLockOnSubsystem.h"
#include "CombatLockOnTargetComponent.h"
#include "CameraDirectorComponent.h"
#include "MinionSquadComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "Engine/World.h"

UCombatLockOnComponent::UCombatLockOnComponent()
{
	// Tick is enabled ONLY while actively locked on (zero idle overhead).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCombatLockOnComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		CachedCharacter = Cast<ACharacter>(OwnerActor);
		CachedMovement = CachedCharacter ? CachedCharacter->GetCharacterMovement() : nullptr;
		CachedSquadComponent = OwnerActor->FindComponentByClass<UMinionSquadComponent>();
	}
}

void UCombatLockOnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ValidationTimerHandle);
		World->GetTimerManager().ClearTimer(AutoTransferTimerHandle);
	}

	SetLockedTargetInternal(nullptr);

	Super::EndPlay(EndPlayReason);
}

APlayerController* UCombatLockOnComponent::GetOwningPlayerController() const
{
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(OwnerPawn->GetController());
	}
	return nullptr;
}

UCameraDirectorComponent* UCombatLockOnComponent::GetCameraDirector() const
{
	if (APlayerController* PC = GetOwningPlayerController())
	{
		return PC->FindComponentByClass<UCameraDirectorComponent>();
	}
	return nullptr;
}

bool UCombatLockOnComponent::GetViewPoint(FVector& OutViewLoc, FVector& OutViewForward) const
{
	if (const APlayerController* PC = GetOwningPlayerController())
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

	if (const AActor* OwnerActor = GetOwner())
	{
		OutViewLoc = OwnerActor->GetActorLocation();
		OutViewForward = OwnerActor->GetActorForwardVector();
		return true;
	}

	return false;
}

bool UCombatLockOnComponent::HasLineOfSightToTarget(const UCombatLockOnTargetComponent* TargetComp) const
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor || !TargetComp)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatLockOnLOS), false, OwnerActor);
	Params.AddIgnoredActor(OwnerActor);
	if (AActor* TargetOwner = TargetComp->GetOwner())
	{
		Params.AddIgnoredActor(TargetOwner);
	}

	// Ignore player's minions so throwing/swarming never breaks Z-Target LOS
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

	const FVector TraceStart = OwnerActor->GetActorLocation() + FVector(0.0f, 0.0f, 65.0f);
	const FVector TraceEnd = TargetComp->GetLockWorldLocation();

	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, LineOfSightChannel, Params);
}

bool UCombatLockOnComponent::ToggleOrAcquireLock()
{
	if (!LockedTarget.IsValid())
	{
		return AcquireLock();
	}

	switch (RepressBehavior)
	{
	case ELockOnRepressBehavior::ToggleOff:
		ReleaseLock();
		return false;

	case ELockOnRepressBehavior::CycleThenRelease:
		// Visit each valid enemy around the player once; release after the full sweep
		if (CycleNextTarget(/*bOnlyUnvisited=*/ true))
		{
			return true;
		}
		ReleaseLock();
		return false;

	case ELockOnRepressBehavior::CycleAndWrap:
		// Keep sweeping clockwise around the player indefinitely
		if (CycleNextTarget(/*bOnlyUnvisited=*/ false))
		{
			return true;
		}
		if (bReleaseIfNoOtherCycleTarget)
		{
			ReleaseLock();
			return false;
		}
		return true;
	}

	return LockedTarget.IsValid();
}

bool UCombatLockOnComponent::AcquireLock()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoTransferTimerHandle);
	}

	UCombatLockOnTargetComponent* BestTarget = FindBestAcquisitionTarget();
	if (BestTarget)
	{
		SetLockedTargetInternal(BestTarget);
		return true;
	}

	// Classic Ocarina of Time behavior: tapping Z with no enemy snaps camera behind player
	if (UCameraDirectorComponent* Director = GetCameraDirector())
	{
		Director->RequestCameraCenterSnap();
	}
	OnRequestCameraCenterSnap.Broadcast();
	return false;
}

bool UCombatLockOnComponent::CycleNextTarget(bool bOnlyUnvisited)
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	UCombatLockOnTargetComponent* CurrentTarget = LockedTarget.Get();
	if (!World || !OwnerActor || !CurrentTarget)
	{
		return false;
	}

	UCombatLockOnSubsystem* Sub = World->GetSubsystem<UCombatLockOnSubsystem>();
	if (!Sub)
	{
		return false;
	}

	// Prune stale weak pointers from visited list
	VisitedCycleTargets.RemoveAllSwap([](const TWeakObjectPtr<UCombatLockOnTargetComponent>& Entry)
	{
		return !Entry.IsValid();
	});

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	FVector ReferenceDir = (CurrentTarget->GetLockWorldLocation() - PlayerLoc).GetSafeNormal2D();
	if (ReferenceDir.IsNearlyZero())
	{
		ReferenceDir = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	}

	const float GlobalSearchLimit = (CycleSearchRadius > KINDA_SMALL_NUMBER) ? CycleSearchRadius : MaxGlobalLockRange;

	struct FCycleCandidate
	{
		UCombatLockOnTargetComponent* Target = nullptr;
		float SweepAngle = 0.0f; // [0, 2*PI) around player from ReferenceDir
		float DistSq = 0.0f;
	};
	TArray<FCycleCandidate, TInlineAllocator<16>> Candidates;

	for (const TWeakObjectPtr<UCombatLockOnTargetComponent>& WeakCandidate : Sub->GetRegisteredTargets())
	{
		UCombatLockOnTargetComponent* Candidate = WeakCandidate.Get();
		if (!IsValid(Candidate) || Candidate == CurrentTarget || !Candidate->CanBeLockedOn())
		{
			continue;
		}

		if (bOnlyUnvisited && VisitedCycleTargets.Contains(WeakCandidate))
		{
			continue;
		}

		const FVector CandLoc = Candidate->GetLockWorldLocation();
		const float EffectiveRange = FMath::Min(GlobalSearchLimit, Candidate->GetMaxLockDistance());
		const float DistSq = FVector::DistSquared(PlayerLoc, CandLoc);
		if (DistSq > FMath::Square(EffectiveRange))
		{
			continue;
		}

		const FVector ToCand2D = (CandLoc - PlayerLoc).GetSafeNormal2D();
		if (ToCand2D.IsNearlyZero())
		{
			continue;
		}

		// In UE's left-handed Z-up space: Cross(ReferenceDir, ToCand2D).Z > 0 is clockwise from above
		const float Dot = FVector::DotProduct(ReferenceDir, ToCand2D);
		const float CrossZ = FVector::CrossProduct(ReferenceDir, ToCand2D).Z;
		float Angle = FMath::Atan2(bCycleClockwise ? CrossZ : -CrossZ, Dot); // (-PI, PI]
		if (Angle <= 0.0f)
		{
			Angle += 2.0f * PI; // Wrap to (0, 2*PI] so 0 is strictly ahead in sweep direction
		}

		Candidates.Add({ Candidate, Angle, DistSq });
	}

	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Sort by sweep angle around player; break near-identical angle ties by closest distance
	Candidates.Sort([](const FCycleCandidate& A, const FCycleCandidate& B)
	{
		if (FMath::IsNearlyEqual(A.SweepAngle, B.SweepAngle, 0.03f)) // ~1.7 degrees
		{
			return A.DistSq < B.DistSq;
		}
		return A.SweepAngle < B.SweepAngle;
	});

	// Deferred LOS check: pick the first visible enemy in clockwise order
	for (const FCycleCandidate& Entry : Candidates)
	{
		if (HasLineOfSightToTarget(Entry.Target))
		{
			SetLockedTargetInternal(Entry.Target);
			return true;
		}
	}

	return false;
}

void UCombatLockOnComponent::ReleaseLock()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoTransferTimerHandle);
	}

	SetLockedTargetInternal(nullptr);
}

bool UCombatLockOnComponent::HandleLookInput(FVector2D LookInput)
{
	const float InputMag = LookInput.Size();

	if (!LockedTarget.IsValid())
	{
		// If player manually rotates camera during a Z-tap center snap, yield immediately
		if (InputMag > 0.05f)
		{
			if (UCameraDirectorComponent* Director = GetCameraDirector())
			{
				Director->NotifyManualLookInput();
			}
		}
		return false;
	}

	// While locked on, consume look input and convert stick flicks into target switches
	if (InputMag < SwitchResetThreshold)
	{
		bSwitchArmed = true;
	}
	else if (bSwitchArmed && InputMag >= SwitchInputThreshold)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if ((Now - LastSwitchTime) >= SwitchCooldown)
		{
			if (TrySwitchTarget(LookInput))
			{
				LastSwitchTime = Now;
				bSwitchArmed = false;
			}
		}
	}

	return true;
}

bool UCombatLockOnComponent::TrySwitchTarget(FVector2D StickInputDir)
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	APlayerController* PC = GetOwningPlayerController();
	UCombatLockOnTargetComponent* CurrentTarget = LockedTarget.Get();

	if (!World || !OwnerActor || !PC || !CurrentTarget || StickInputDir.SizeSquared() < 0.04f)
	{
		return false;
	}

	UCombatLockOnSubsystem* Sub = World->GetSubsystem<UCombatLockOnSubsystem>();
	if (!Sub)
	{
		return false;
	}

	FVector2D CurrentScreenPos;
	if (!PC->ProjectWorldLocationToScreen(CurrentTarget->GetLockWorldLocation(), CurrentScreenPos, true))
	{
		return false;
	}

	// Screen space has +X right, +Y down; standard gamepad stick has +Y up
	FVector2D ScreenInputDir = StickInputDir.GetSafeNormal();
	if (!bInvertSwitchVerticalAxis)
	{
		ScreenInputDir.Y = -ScreenInputDir.Y;
	}

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	UCombatLockOnTargetComponent* BestSwitchTarget = nullptr;
	float BestMetric = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<UCombatLockOnTargetComponent>& WeakCandidate : Sub->GetRegisteredTargets())
	{
		UCombatLockOnTargetComponent* Candidate = WeakCandidate.Get();
		if (!IsValid(Candidate) || Candidate == CurrentTarget || !Candidate->CanBeLockedOn())
		{
			continue;
		}

		const float EffectiveRange = FMath::Min(MaxGlobalLockRange, Candidate->GetMaxLockDistance());
		if (FVector::DistSquared(PlayerLoc, Candidate->GetLockWorldLocation()) > FMath::Square(EffectiveRange))
		{
			continue;
		}

		FVector2D CandScreenPos;
		if (!PC->ProjectWorldLocationToScreen(Candidate->GetLockWorldLocation(), CandScreenPos, true))
		{
			continue;
		}

		const FVector2D ScreenDelta = CandScreenPos - CurrentScreenPos;
		const float ScreenDist = ScreenDelta.Size();
		if (ScreenDist < 5.0f)
		{
			continue;
		}

		const FVector2D CandScreenDir = ScreenDelta / ScreenDist;
		const float Alignment = FVector2D::DotProduct(ScreenInputDir, CandScreenDir);

		// Must lie within roughly +/-60 degrees of the flick direction in screen space
		if (Alignment < 0.45f)
		{
			continue;
		}

		if (!HasLineOfSightToTarget(Candidate))
		{
			continue;
		}

		// Lower metric wins: favors candidates closely aligned with stick direction and near current target
		const float AnglePenalty = 1.0f + ((1.0f - Alignment) * 2.5f);
		const float Metric = ScreenDist * AnglePenalty;

		if (Metric < BestMetric)
		{
			BestMetric = Metric;
			BestSwitchTarget = Candidate;
		}
	}

	if (BestSwitchTarget)
	{
		SetLockedTargetInternal(BestSwitchTarget);
		return true;
	}

	return false;
}

UCombatLockOnTargetComponent* UCombatLockOnComponent::FindBestAcquisitionTarget(
	float MaxRadiusOverride,
	bool bRequireViewCone) const
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return nullptr;
	}

	UCombatLockOnSubsystem* Sub = World->GetSubsystem<UCombatLockOnSubsystem>();
	if (!Sub)
	{
		return nullptr;
	}

	FVector CamLoc, CamForward;
	if (!GetViewPoint(CamLoc, CamForward))
	{
		return nullptr;
	}

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	const float GlobalLimit = (MaxRadiusOverride > 0.0f) ? MaxRadiusOverride : MaxGlobalLockRange;
	const float CosHalfCone = FMath::Cos(FMath::DegreesToRadians(AcquisitionConeHalfAngleDegrees));

	struct FScoredCandidate
	{
		UCombatLockOnTargetComponent* Target = nullptr;
		float Score = 0.0f;
	};
	TArray<FScoredCandidate, TInlineAllocator<16>> Candidates;

	for (const TWeakObjectPtr<UCombatLockOnTargetComponent>& WeakTarget : Sub->GetRegisteredTargets())
	{
		UCombatLockOnTargetComponent* TargetComp = WeakTarget.Get();
		if (!IsValid(TargetComp) || !TargetComp->CanBeLockedOn())
		{
			continue;
		}

		const FVector TargetLoc = TargetComp->GetLockWorldLocation();
		const float EffectiveRange = FMath::Min(GlobalLimit, TargetComp->GetMaxLockDistance());
		const float DistSq = FVector::DistSquared(PlayerLoc, TargetLoc);
		if (DistSq > FMath::Square(EffectiveRange))
		{
			continue;
		}

		const float Dist = FMath::Sqrt(DistSq);
		const FVector CamToTargetDir = (TargetLoc - CamLoc).GetSafeNormal();
		const float Dot = FVector::DotProduct(CamForward, CamToTargetDir);

		const bool bInCloseFallback = Dist <= CloseProximityFallbackRadius;
		if (bRequireViewCone && Dot < CosHalfCone && !bInCloseFallback)
		{
			continue;
		}

		const float AngleNorm = FMath::Clamp((Dot - CosHalfCone) / FMath::Max(1.0f - CosHalfCone, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
		const float DistNorm = 1.0f - FMath::Clamp(Dist / FMath::Max(EffectiveRange, 1.0f), 0.0f, 1.0f);
		const float PriorityNorm = FMath::Clamp(TargetComp->GetPriorityWeight() * 0.5f, 0.0f, 1.0f);

		const float Score = (0.55f * AngleNorm) + (0.35f * DistNorm) + (0.10f * PriorityNorm);
		Candidates.Add({ TargetComp, Score });
	}

	Candidates.Sort([](const FScoredCandidate& A, const FScoredCandidate& B)
	{
		return A.Score > B.Score;
	});

	// Deferred LOS check: return the highest-scored visible target
	for (const FScoredCandidate& Entry : Candidates)
	{
		if (HasLineOfSightToTarget(Entry.Target))
		{
			return Entry.Target;
		}
	}

	return nullptr;
}

void UCombatLockOnComponent::SetLockedTargetInternal(UCombatLockOnTargetComponent* NewTarget)
{
	UCombatLockOnTargetComponent* PrevTarget = LockedTarget.Get();
	if (NewTarget == PrevTarget)
	{
		return;
	}

	// 1. Clean up previous target bindings
	if (IsValid(PrevTarget))
	{
		PrevTarget->OnTargetInvalidated.Remove(TargetInvalidatedHandle);
		TargetInvalidatedHandle.Reset();
		PrevTarget->NotifyLockFocusState(false);
	}

	LockedTarget = NewTarget;
	TimeOccluded = 0.0f;

	UWorld* World = GetWorld();

	// 2. Bind new target & toggle movement / tick / timer states
	if (IsValid(NewTarget))
	{
		VisitedCycleTargets.AddUnique(NewTarget);

		TargetInvalidatedHandle = NewTarget->OnTargetInvalidated.AddUObject(
			this, &UCombatLockOnComponent::HandleTargetInvalidated);
		NewTarget->NotifyLockFocusState(true);

		if (CachedMovement && !bHasSavedMovementState)
		{
			bSavedOrientRotationToMovement = CachedMovement->bOrientRotationToMovement;
			bHasSavedMovementState = true;
		}
		if (CachedMovement)
		{
			CachedMovement->bOrientRotationToMovement = false;
		}

		if (World)
		{
			World->GetTimerManager().SetTimer(
				ValidationTimerHandle,
				this,
				&UCombatLockOnComponent::ValidateCurrentLock,
				ValidationInterval,
				true);
		}

		SetComponentTickEnabled(true);
	}
	else
	{
		VisitedCycleTargets.Reset();

		if (World)
		{
			World->GetTimerManager().ClearTimer(ValidationTimerHandle);
		}

		if (CachedMovement && bHasSavedMovementState)
		{
			CachedMovement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
			bHasSavedMovementState = false;
		}

		SetComponentTickEnabled(false);
	}

	// 3. Notify CameraDirectorComponent (drives free 3P camera, zone framing, and orbit movement)
	if (UCameraDirectorComponent* Director = GetCameraDirector())
	{
		Director->SetCombatLockTarget(NewTarget);
	}

	OnLockTargetChanged.Broadcast(NewTarget, PrevTarget);
}

void UCombatLockOnComponent::ValidateCurrentLock()
{
	UCombatLockOnTargetComponent* TargetComp = LockedTarget.Get();
	AActor* OwnerActor = GetOwner();
	if (!IsValid(TargetComp) || !TargetComp->CanBeLockedOn() || !OwnerActor)
	{
		ReleaseLock();
		return;
	}

	// Distance check with hysteresis buffer
	const float BreakDist = FMath::Min(MaxGlobalLockRange, TargetComp->GetMaxLockDistance()) + BreakDistanceBuffer;
	const float DistSq = FVector::DistSquared(OwnerActor->GetActorLocation(), TargetComp->GetLockWorldLocation());
	if (DistSq > FMath::Square(BreakDist))
	{
		ReleaseLock();
		return;
	}

	// Occlusion grace period check
	if (HasLineOfSightToTarget(TargetComp))
	{
		TimeOccluded = 0.0f;
	}
	else
	{
		TimeOccluded += ValidationInterval;
		if (TimeOccluded >= OcclusionGraceDuration)
		{
			ReleaseLock();
		}
	}
}

void UCombatLockOnComponent::HandleTargetInvalidated(UCombatLockOnTargetComponent* InvalidatedTarget, bool bWasKilled)
{
	if (InvalidatedTarget != LockedTarget.Get())
	{
		return;
	}

	SetLockedTargetInternal(nullptr);

	if (bWasKilled && bAutoTransferOnKill)
	{
		if (UWorld* World = GetWorld())
		{
			if (AutoTransferDelay > KINDA_SMALL_NUMBER)
			{
				World->GetTimerManager().SetTimer(
					AutoTransferTimerHandle,
					this,
					&UCombatLockOnComponent::ExecuteAutoTransfer,
					AutoTransferDelay,
					false);
			}
			else
			{
				ExecuteAutoTransfer();
			}
		}
	}
}

void UCombatLockOnComponent::ExecuteAutoTransfer()
{
	if (LockedTarget.IsValid())
	{
		return;
	}

	// Search nearby enemies regardless of narrow cone so group fights keep flowing
	if (UCombatLockOnTargetComponent* NextTarget = FindBestAcquisitionTarget(AutoTransferRadius, false))
	{
		SetLockedTargetInternal(NextTarget);
	}
}

FVector UCombatLockOnComponent::GetLockedTargetLocation() const
{
	return LockedTarget.IsValid() ? LockedTarget->GetLockWorldLocation() : FVector::ZeroVector;
}

float UCombatLockOnComponent::GetStrafeAngleDegrees() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return 0.0f;
	}

	const FVector Vel2D = FVector(OwnerActor->GetVelocity().X, OwnerActor->GetVelocity().Y, 0.0f);
	if (Vel2D.SizeSquared() < 100.0f) // < 10 cm/s
	{
		return 0.0f;
	}

	const FVector MoveDir = Vel2D.GetSafeNormal();
	const FVector Forward = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	const float Dot = FVector::DotProduct(Forward, MoveDir);
	const float CrossZ = FVector::CrossProduct(Forward, MoveDir).Z;

	return FMath::RadiansToDegrees(FMath::Atan2(CrossZ, Dot));
}

void UCombatLockOnComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UCombatLockOnTargetComponent* TargetComp = LockedTarget.Get();
	AActor* OwnerActor = GetOwner();
	if (!IsValid(TargetComp) || !OwnerActor)
	{
		SetLockedTargetInternal(nullptr);
		return;
	}

	// Smoothly face the locked enemy (works identically in Free 3P and Fixed Camera Zones)
	if (!bPauseLockFacing)
	{
		const FVector ToTarget = (TargetComp->GetLockWorldLocation() - OwnerActor->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurrentRot = OwnerActor->GetActorRotation();
			const FRotator DesiredRot(CurrentRot.Pitch, ToTarget.Rotation().Yaw, CurrentRot.Roll);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, DesiredRot, DeltaTime, CharacterTurnInterpSpeed);
			OwnerActor->SetActorRotation(NewRot);
		}
	}

	if (bDebugDrawLockOn)
	{
		if (UWorld* World = GetWorld())
		{
			const FVector TargetLoc = TargetComp->GetLockWorldLocation();
			const FColor Color = (TimeOccluded > 0.0f) ? FColor::Orange : FColor::Magenta;
			DrawDebugSphere(World, TargetLoc, 30.0f, 12, Color, false, -1.0f, 0, 2.5f);
			DrawDebugLine(World, OwnerActor->GetActorLocation() + FVector(0.f, 0.f, 65.f), TargetLoc, Color, false, -1.0f, 0, 1.5f);
		}
	}
}