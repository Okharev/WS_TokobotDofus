// VoidRecoveryComponent.cpp
#include "VoidRecoveryComponent.h"

#include "CameraDirectorComponent.h"
#include "CombatFeedbackTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HeartHealthComponent.h"
#include "HitReactableInterface.h"
#include "MinionSquadComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

UVoidRecoveryComponent::UVoidRecoveryComponent()
{
	// Event + Timer driven only. Zero per-frame Tick cost.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVoidRecoveryComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	OwnerMoveComp = OwnerCharacter->GetCharacterMovement();
	CachedSquadComp = OwnerCharacter->FindComponentByClass<UMinionSquadComponent>();

	// Seed room fallback with spawn transform so we ALWAYS have a valid target
	RoomFallbackTransform = OwnerCharacter->GetActorTransform();
	bHasRoomFallback = true;
	SafeTransformHistory.Add(RoomFallbackTransform);

	// Listen for falling transitions to activate the Kill-Z guard only while airborne
	OwnerCharacter->MovementModeChangedDelegate.AddDynamic(
		this, &UVoidRecoveryComponent::OnOwnerMovementModeChanged);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SafeSampleTimerHandle,
			this,
			&UVoidRecoveryComponent::SampleSafeGround,
			SafeSampleInterval,
			true);
	}
}

void UVoidRecoveryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SafeSampleTimerHandle);
		World->GetTimerManager().ClearTimer(FallGuardTimerHandle);
		World->GetTimerManager().ClearTimer(RecoveryPhaseTimerHandle);
	}

	if (OwnerCharacter.IsValid())
	{
		OwnerCharacter->MovementModeChangedDelegate.RemoveDynamic(
			this, &UVoidRecoveryComponent::OnOwnerMovementModeChanged);

		if (bIsRecovering)
		{
			if (APlayerController* PC = ResolvePlayerController())
			{
				OwnerCharacter->EnableInput(PC);
				if (PC->PlayerCameraManager)
				{
					PC->PlayerCameraManager->StopCameraFade();
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UVoidRecoveryComponent::SetRoomFallbackTransform(const FTransform& NewFallbackTransform)
{
	RoomFallbackTransform = NewFallbackTransform;
	bHasRoomFallback = true;
}

void UVoidRecoveryComponent::RecordManualSafeTransform(const FTransform& SafeTransform)
{
	SafeTransformHistory.Add(SafeTransform);
	while (SafeTransformHistory.Num() > MaxSafeHistoryCount)
	{
		SafeTransformHistory.RemoveAt(0);
	}
}

// ============================================================================
// SAFE-GROUND RECORDING (Ledge-Margin + NavMesh + Dynamic-Base Guard)
// ============================================================================

void UVoidRecoveryComponent::SampleSafeGround()
{
	if (bIsRecovering || !OwnerCharacter.IsValid() || !OwnerMoveComp.IsValid())
	{
		return;
	}

	// 1. Only record while firmly walking on solid ground
	if (OwnerMoveComp->MovementMode != MOVE_Walking || OwnerMoveComp->IsFalling())
	{
		return;
	}

	// 2. Reject moving platforms/elevators and tagged hazard floors
	const UPrimitiveComponent* MoveBase = OwnerCharacter->GetMovementBase();
	if (MoveBase)
	{
		if (bRejectMovingPlatforms && MovementBaseUtility::UseRelativeLocation(MoveBase))
		{
			return;
		}

		if (!UnsafeSurfaceTag.IsNone())
		{
			if (MoveBase->ComponentHasTag(UnsafeSurfaceTag))
			{
				return;
			}
			if (const AActor* BaseActor = MoveBase->GetOwner(); BaseActor && BaseActor->ActorHasTag(UnsafeSurfaceTag))
			{
				return;
			}
		}
	}

	// 3. Validate NavMesh presence + 4-way ledge margin so we don't save cliff edges
	FVector ValidatedLoc = FVector::ZeroVector;
	if (!ValidateLocationAsSafeGround(OwnerCharacter->GetActorLocation(), ValidatedLoc))
	{
		return;
	}

	// Keep pure yaw rotation so we never respawn tilted
	const FRotator CleanYawRot(0.0f, OwnerCharacter->GetActorRotation().Yaw, 0.0f);
	const FTransform NewSafeTransform(CleanYawRot, ValidatedLoc);

	if (SafeTransformHistory.IsEmpty())
	{
		SafeTransformHistory.Add(NewSafeTransform);
	}
	else
	{
		const float DistSq2D = FVector::DistSquared2D(
			SafeTransformHistory.Last().GetLocation(),
			ValidatedLoc);

		if (DistSq2D >= FMath::Square(MinDistanceBetweenSamples))
		{
			SafeTransformHistory.Add(NewSafeTransform);
			while (SafeTransformHistory.Num() > MaxSafeHistoryCount)
			{
				SafeTransformHistory.RemoveAt(0);
			}
		}
		else
		{
			// Refresh the newest sample in-place with updated facing
			SafeTransformHistory.Last() = NewSafeTransform;
		}
	}

	if (bDrawDebugSafeSpots && GetWorld())
	{
		DrawDebugSphere(GetWorld(), ValidatedLoc, 22.0f, 8, FColor::Green, false, SafeSampleInterval * 1.2f);
	}
}

bool UVoidRecoveryComponent::ValidateLocationAsSafeGround(
	const FVector& CandidateActorLoc,
	FVector& OutAdjustedLoc) const
{
	UWorld* World = GetWorld();
	const ACharacter* Char = OwnerCharacter.Get();
	const UCapsuleComponent* Capsule = Char ? Char->GetCapsuleComponent() : nullptr;
	if (!World || !Char || !Capsule)
	{
		return false;
	}

	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float FeetZ = CandidateActorLoc.Z - HalfHeight;

	// A. Optional NavMesh projection (guarantees minions & player share valid walkable topology)
	FVector NavCheckedLoc = CandidateActorLoc;
	if (bRequireNavMeshForSafeGround)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
		if (NavSys && NavData)
		{
			FNavLocation Projected;
			if (!NavSys->ProjectPointToNavigation(
				CandidateActorLoc,
				Projected,
				FVector(60.0f, 60.0f, HalfHeight + 40.0f),
				NavData))
			{
				return false;
			}
			// Keep the NavMesh Z + capsule half-height so we respawn cleanly flush with the floor
			NavCheckedLoc = FVector(CandidateActorLoc.X, CandidateActorLoc.Y, Projected.Location.Z + HalfHeight);
		}
	}

	// B. 4-way cardinal ledge margin check: ensures all 4 points around the player have solid floor underneath
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VoidSafeGroundLedgeCheck), false, Char);
	if (CachedSquadComp.IsValid())
	{
		for (AActor* Minion : CachedSquadComp->GetActiveMinions())
		{
			if (IsValid(Minion))
			{
				TraceParams.AddIgnoredActor(Minion);
			}
		}
	}

	static const FVector2D CardinalOffsets[4] = {
		FVector2D(1.0f, 0.0f),
		FVector2D(-1.0f, 0.0f),
		FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, -1.0f)
	};

	const float TraceStartZ = FeetZ + 30.0f;
	const float TraceEndZ = FeetZ - MaxLedgeDropTolerance;

	for (const FVector2D& Dir : CardinalOffsets)
	{
		const FVector Start(
			CandidateActorLoc.X + (Dir.X * LedgeSafetyMargin),
			CandidateActorLoc.Y + (Dir.Y * LedgeSafetyMargin),
			TraceStartZ);
		const FVector End(Start.X, Start.Y, TraceEndZ);

		FHitResult Hit;
		const bool bHitFloor = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, TraceParams);
		if (!bHitFloor || !Hit.bBlockingHit)
		{
			return false;
		}

		// Reject steep slopes that are not walkable
		if (OwnerMoveComp.IsValid() && Hit.ImpactNormal.Z < OwnerMoveComp->GetWalkableFloorZ())
		{
			return false;
		}

		// Reject tagged unsafe surfaces under the margin check too
		if (!UnsafeSurfaceTag.IsNone())
		{
			if (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(UnsafeSurfaceTag))
			{
				return false;
			}
			if (Hit.GetActor() && Hit.GetActor()->ActorHasTag(UnsafeSurfaceTag))
			{
				return false;
			}
		}
	}

	OutAdjustedLoc = NavCheckedLoc;
	return true;
}

bool UVoidRecoveryComponent::IsTransformClearForRespawn(const FTransform& Candidate) const
{
	UWorld* World = GetWorld();
	const ACharacter* Char = OwnerCharacter.Get();
	const UCapsuleComponent* Capsule = Char ? Char->GetCapsuleComponent() : nullptr;
	if (!World || !Char || !Capsule)
	{
		return true;
	}

	// Verify static world geometry hasn't moved/closed over this safe spot (e.g., a shut dungeon gate)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VoidRespawnClearance), false, Char);
	const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius() * 0.9f,
		Capsule->GetScaledCapsuleHalfHeight() * 0.9f);

	const FVector TestLoc = Candidate.GetLocation() + FVector(0.0f, 0.0f, RespawnZLift);
	return !World->OverlapBlockingTestByChannel(TestLoc, FQuat::Identity, ECC_WorldStatic, CapsuleShape, Params);
}

FTransform UVoidRecoveryComponent::GetBestSafeTransform() const
{
	// Walk backward from newest to oldest safe spot and pick the first unobstructed one
	for (int32 i = SafeTransformHistory.Num() - 1; i >= 0; --i)
	{
		if (IsTransformClearForRespawn(SafeTransformHistory[i]))
		{
			return SafeTransformHistory[i];
		}
	}

	if (!SafeTransformHistory.IsEmpty())
	{
		return SafeTransformHistory.Last();
	}

	if (bHasRoomFallback)
	{
		return RoomFallbackTransform;
	}

	return OwnerCharacter.IsValid() ? OwnerCharacter->GetActorTransform() : FTransform::Identity;
}

// ============================================================================
// EVENT-DRIVEN KILL-Z WATCHDOG (Only active while MOVE_Falling)
// ============================================================================

void UVoidRecoveryComponent::OnOwnerMovementModeChanged(
	ACharacter* Character,
	EMovementMode PrevMovementMode,
	uint8 PreviousCustomMode)
{
	if (!bEnableFallbackDropGuard || !OwnerMoveComp.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (OwnerMoveComp->MovementMode == MOVE_Falling)
	{
		if (!World->GetTimerManager().IsTimerActive(FallGuardTimerHandle))
		{
			World->GetTimerManager().SetTimer(
				FallGuardTimerHandle,
				this,
				&UVoidRecoveryComponent::PollFallDepthGuard,
				0.1f,
				true);
		}
	}
	else
	{
		World->GetTimerManager().ClearTimer(FallGuardTimerHandle);
	}
}

void UVoidRecoveryComponent::PollFallDepthGuard()
{
	if (bIsRecovering || !OwnerCharacter.IsValid())
	{
		return;
	}

	const float CurrentZ = OwnerCharacter->GetActorLocation().Z;
	const float RefSafeZ = GetBestSafeTransform().GetLocation().Z;

	if (CurrentZ <= AbsoluteWorldMinZ || (RefSafeZ - CurrentZ) >= MaxFallDistanceBelowSafeZ)
	{
		TriggerVoidFall(DefaultKillZRequest);
	}
}

// ============================================================================
// 3-PHASE VOID RECOVERY SEQUENCE
// ============================================================================

bool UVoidRecoveryComponent::TriggerVoidFall(const FVoidFallRequest& Request)
{
	if (bIsRecovering || !OwnerCharacter.IsValid())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	bIsRecovering = true;
	bLastRecoveryWasFatal = false;
	ActiveRequest = Request;

	World->GetTimerManager().ClearTimer(FallGuardTimerHandle);

	// 1. Disable player movement input immediately so they can't air-steer or trigger spells mid-pit
	if (APlayerController* PC = ResolvePlayerController())
	{
		OwnerCharacter->DisableInput(PC);
	}

	// 2. Abort any active Whip chains or CarryRush momentum immediately
	if (CachedSquadComp.IsValid())
	{
		CachedSquadComp->StopCarryRushCharge(true);
		if (CachedSquadComp->GetSquadStance() != EMinionSquadStance::Follow)
		{
			CachedSquadComp->SetSquadStance(EMinionSquadStance::Follow);
		}
	}

	// 3. Camera Phase 1: Freeze Free-3P at the rim (watching the drop) OR lock active CameraZoneActor
	if (UCameraDirectorComponent* Director = ResolveCameraDirector())
	{
		Director->BeginVoidFallCamera(bWatchFallingPlayerInFreeCam);
	}

	OnVoidFallStarted.Broadcast(ActiveRequest);

	if (FadeOutDelay > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().SetTimer(
			RecoveryPhaseTimerHandle,
			this,
			&UVoidRecoveryComponent::Phase_StartFadeOut,
			FadeOutDelay,
			false);
	}
	else
	{
		Phase_StartFadeOut();
	}

	return true;
}

void UVoidRecoveryComponent::Phase_StartFadeOut()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (APlayerController* PC = ResolvePlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(
				0.0f,
				1.0f,
				FadeOutDuration,
				ActiveRequest.FadeColor,
				false,
				true /* bHoldWhenFinished */);
		}
	}

	World->GetTimerManager().SetTimer(
		RecoveryPhaseTimerHandle,
		this,
		&UVoidRecoveryComponent::Phase_ExecuteTeleportAtBlack,
		FadeOutDuration,
		false);
}

void UVoidRecoveryComponent::Phase_ExecuteTeleportAtBlack()
{
	UWorld* World = GetWorld();
	ACharacter* Char = OwnerCharacter.Get();
	if (!World || !Char)
	{
		bIsRecovering = false;
		return;
	}

	// 1. Resolve target transform (override vs. safe buffer)
	const FTransform DestTransform = ActiveRequest.bUseOverrideRespawn
		                                 ? ActiveRequest.OverrideRespawnTransform
		                                 : GetBestSafeTransform();

	const FVector DestLocation = DestTransform.GetLocation() + FVector(0.0f, 0.0f, RespawnZLift);
	const FRotator DestRotation(0.0f, DestTransform.Rotator().Yaw, 0.0f);
	LastRespawnLocation = DestLocation;

	// 2. Kill all falling momentum before teleporting
	if (OwnerMoveComp.IsValid())
	{
		OwnerMoveComp->StopMovementImmediately();
		OwnerMoveComp->SetMovementMode(MOVE_Walking);
	}

	// 3. Hard teleport with physics reset; triggers overlap updates for CameraZoneActors at the destination
	Char->SetActorLocationAndRotation(
		DestLocation,
		DestRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	Char->UpdateOverlaps();

	// 4. Reset Minion Squad cleanly behind the player
	if (CachedSquadComp.IsValid())
	{
		CachedSquadComp->ResetMinionsToPlayerCheckpoint(ActiveRequest.bDetachMinionsFromInteractables);
	}

	// 5. Camera Phase 2: Unlock zone stack, hard-cut camera behind the black screen, flush lag, notify renderer cut
	if (UCameraDirectorComponent* Director = ResolveCameraDirector())
	{
		const FRotator RespawnControlRot(RespawnCameraPitch, DestRotation.Yaw, 0.0f);
		Director->EndVoidFallCameraAndSnapToPlayer(RespawnControlRot);
	}

	// 6. Apply heart penalty ON SOLID GROUND so if it's a killing blow, death anims & camera are properly framed
	if (ActiveRequest.HeartPenalty > 0)
	{
		bLastRecoveryWasFatal = ApplyVoidHeartPenalty(ActiveRequest.HeartPenalty);
	}

	// Hold black briefly (1+ frames) so SpringArm lag flush & Lumen/TSR camera cut settle cleanly
	World->GetTimerManager().SetTimer(
		RecoveryPhaseTimerHandle,
		this,
		&UVoidRecoveryComponent::Phase_FinishAndFadeIn,
		BlackHoldDuration,
		false);
}

void UVoidRecoveryComponent::Phase_FinishAndFadeIn()
{
	// Re-enable SpringArm camera lag now that it has ticked once at the teleported location
	if (UCameraDirectorComponent* Director = ResolveCameraDirector())
	{
		Director->RestoreSpringArmLagAfterTeleport();
	}

	if (APlayerController* PC = ResolvePlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(
				1.0f,
				0.0f,
				FadeInDuration,
				ActiveRequest.FadeColor,
				false,
				false);
		}

		// Only restore player control if the void penalty did not kill them
		if (!bLastRecoveryWasFatal && OwnerCharacter.IsValid())
		{
			OwnerCharacter->EnableInput(PC);
		}
	}

	bIsRecovering = false;
	OnVoidRecoveryCompleted.Broadcast(LastRespawnLocation, bLastRecoveryWasFatal);
}

bool UVoidRecoveryComponent::ApplyVoidHeartPenalty(int32 HeartDamage)
{
	ACharacter* Char = OwnerCharacter.Get();
	if (!Char || HeartDamage <= 0)
	{
		return false;
	}

	// Matches your existing FCombatHitPayload contract from MinionSquadComponent
	FCombatHitPayload Payload;
	Payload.InstigatorActor = Char;
	Payload.DamageCauser = Char;
	Payload.TargetActor = Char;
	Payload.AttackType = FName("VoidFall");
	Payload.ImpactPoint = Char->GetActorLocation();
	Payload.ImpactNormal = FVector::UpVector;
	Payload.KnockbackDirection = FVector::ZeroVector;
	Payload.KnockbackVelocity = FVector::ZeroVector;
	Payload.HeartDamage = HeartDamage;
	Payload.StaggerDuration = 0.0f;
	Payload.AppliedHitStopDuration = 0.0f;

	// NOTE: If your HeartHealthComponent rejects hits while combat i-frames are active
	// (e.g., an enemy knocked the player off a ledge), this naturally grants i-frame forgiveness.
	// If you want void falls to ALWAYS deduct a heart even during i-frames, expose an
	// ApplyEnvironmentalDamage(int32 Hearts) bypass on UHeartHealthComponent and call it here.
	if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(Char))
	{
		const FCombatHitResponse Response = Reactable->ReceiveCombatHit(Payload);
		return Response.bWasKillingBlow;
	}

	if (UHeartHealthComponent* HealthComp = Char->FindComponentByClass<UHeartHealthComponent>())
	{
		const FCombatHitResponse Response = HealthComp->ApplyCombatHit(Payload);
		return Response.bWasKillingBlow;
	}

	return false;
}

APlayerController* UVoidRecoveryComponent::ResolvePlayerController() const
{
	return OwnerCharacter.IsValid() ? Cast<APlayerController>(OwnerCharacter->GetController()) : nullptr;
}

UCameraDirectorComponent* UVoidRecoveryComponent::ResolveCameraDirector() const
{
	if (APlayerController* PC = ResolvePlayerController())
	{
		return PC->FindComponentByClass<UCameraDirectorComponent>();
	}
	return nullptr;
}