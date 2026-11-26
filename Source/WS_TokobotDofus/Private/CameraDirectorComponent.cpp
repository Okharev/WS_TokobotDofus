// CameraDirectorComponent.cpp
#include "CameraDirectorComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CameraZoneActor.h"
#include "CombatLockOnTargetComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"

UCameraDirectorComponent::UCameraDirectorComponent()
{
	// Tick stays disabled during normal exploration and only runs during lock-on, center-snap, or arm restore.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCameraDirectorComponent::PushZone(ACameraZoneActor* Zone)
{
	if (!Zone)
	{
		return;
	}

	// Move to top of recency order if re-entered
	ZoneStack.Remove(Zone);
	ZoneStack.Add(Zone);

	EvaluateStack();
}

void UCameraDirectorComponent::PopZone(ACameraZoneActor* Zone)
{
	if (!Zone)
	{
		return;
	}

	ZoneStack.Remove(Zone);
	EvaluateStack(Zone);
}

void UCameraDirectorComponent::ClearAllZones(float BlendOutTime)
{
	ZoneStack.Empty();
	CurrentActiveZone.Reset();

	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, BlendOutTime, VTBlend_Cubic, 2.0f, false);
		}
	}

	UpdateTickState();
}

ACameraZoneActor* UCameraDirectorComponent::GetTopPriorityZone()
{
	// Prune any destroyed/streamed-out zones first
	ZoneStack.RemoveAll([](const TWeakObjectPtr<ACameraZoneActor>& Ptr) { return !Ptr.IsValid(); });

	ACameraZoneActor* BestZone = nullptr;
	for (const TWeakObjectPtr<ACameraZoneActor>& Entry : ZoneStack)
	{
		ACameraZoneActor* Candidate = Entry.Get();
		// >= ensures newer zones win ties at equal priority
		if (!BestZone || Candidate->Priority >= BestZone->Priority)
		{
			BestZone = Candidate;
		}
	}
	return BestZone;
}

void UCameraDirectorComponent::EvaluateStack(const ACameraZoneActor* ExitingZone)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}

	ACameraZoneActor* NextZone = GetTopPriorityZone();
	if (NextZone == CurrentActiveZone.Get())
	{
		return;
	}

	// Latch the outgoing movement basis if the player is actively holding a direction during the cut
	const bool bWasMoving = bHasValidBasis && (GFrameCounter - LastMoveInputFrame) <= 2;
	if (bLatchBasisOnCameraSwitch && bWasMoving && !ActiveLockTarget.IsValid())
	{
		bIsBasisLatched = true;
		bNeedsSmoothCatchup = false;
	}

	CurrentActiveZone = NextZone;

	if (NextZone)
	{
		bCenterSnapActive = false;
		NextZone->NotifyActivated(PC->GetPawn());
		PC->SetViewTarget(NextZone, NextZone->BlendIn.ToTransitionParams());
	}
	else if (APawn* Pawn = PC->GetPawn())
	{
		FViewTargetTransitionParams OutParams;
		if (ExitingZone)
		{
			OutParams = ExitingZone->BlendOut.ToTransitionParams();
		}
		else
		{
			OutParams.BlendTime = 0.5f;
			OutParams.BlendFunction = VTBlend_Cubic;
			OutParams.BlendExp = 2.0f;
		}

		PC->SetViewTarget(Pawn, OutParams);
	}

	UpdateTickState();
}

void UCameraDirectorComponent::SetCombatLockTarget(UCombatLockOnTargetComponent* NewTarget)
{
	const bool bWasLocked = ActiveLockTarget.IsValid();
	ActiveLockTarget = NewTarget;

	if (IsValid(NewTarget))
	{
		bCenterSnapActive = false;
		bSpringArmNeedsRestore = false;
		bIsBasisLatched = false;
		bNeedsSmoothCatchup = false;
	}
	else if (bWasLocked)
	{
		bSpringArmNeedsRestore = true;

		// If the player unlocks mid-strafe while holding the stick, blend smoothly back to live camera basis
		const bool bWasMoving = bHasValidBasis && (GFrameCounter - LastMoveInputFrame) <= 2;
		if (bWasMoving)
		{
			bIsBasisLatched = false;
			bNeedsSmoothCatchup = true;
		}
	}

	UpdateTickState();
}

void UCameraDirectorComponent::RequestCameraCenterSnap()
{
	if (IsInFixedCameraZone() || ActiveLockTarget.IsValid())
	{
		return;
	}

	bCenterSnapActive = true;
	UpdateTickState();
}

void UCameraDirectorComponent::NotifyManualLookInput()
{
	if (bCenterSnapActive)
	{
		bCenterSnapActive = false;
		UpdateTickState();
	}
}

bool UCameraDirectorComponent::EnsureSpringArmCached(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return false;
	}

	if (CachedSpringArmPawn.Get() != Pawn || !CachedSpringArm.IsValid())
	{
		CachedSpringArmPawn = Pawn;
		CachedSpringArm = Pawn->FindComponentByClass<USpringArmComponent>();
		if (CachedSpringArm.IsValid())
		{
			BaseTargetArmLength = CachedSpringArm->TargetArmLength;
			BaseSocketOffset = CachedSpringArm->SocketOffset;
			bHasCachedSpringArmBase = true;
		}
		else
		{
			bHasCachedSpringArmBase = false;
		}
	}

	return CachedSpringArm.IsValid() && bHasCachedSpringArmBase;
}

void UCameraDirectorComponent::UpdateTickState()
{
	const bool bNeedLockDrive = ActiveLockTarget.IsValid() && !IsInFixedCameraZone();
	const bool bNeedSnapDrive = bCenterSnapActive && !IsInFixedCameraZone();
	const bool bNeedsTick = bNeedLockDrive || bNeedSnapDrive || bSpringArmNeedsRestore;
	SetComponentTickEnabled(bNeedsTick);
}

void UCameraDirectorComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}

	if (!IsInFixedCameraZone())
	{
		if (ActiveLockTarget.IsValid())
		{
			UpdateFreeCameraLockOn(DeltaTime, PC, Pawn);
			return;
		}

		if (bCenterSnapActive)
		{
			UpdateCameraCenterSnap(DeltaTime, PC, Pawn);
		}
	}

	if (bSpringArmNeedsRestore)
	{
		RestoreSpringArmToBase(DeltaTime, Pawn);
	}
}

void UCameraDirectorComponent::UpdateFreeCameraLockOn(float DeltaTime, APlayerController* PC, APawn* Pawn)
{
	const UCombatLockOnTargetComponent* TargetComp = ActiveLockTarget.Get();
	if (!IsValid(TargetComp))
	{
		SetCombatLockTarget(nullptr);
		return;
	}

	const FVector PlayerLoc = Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 65.0f);
	const FVector TargetLoc = TargetComp->GetLockWorldLocation();
	const FVector FocusPoint = FMath::Lerp(PlayerLoc, TargetLoc, LockCameraFocusWeight);

	const FVector CamLoc = PC->PlayerCameraManager
		                       ? PC->PlayerCameraManager->GetCameraLocation()
		                       : PlayerLoc;

	// Compute yaw from Player->Target so close-range dueling stays rock-solid, pitch from Cam->FocusPoint
	const FVector PlayerToTarget2D = (TargetLoc - PlayerLoc).GetSafeNormal2D();
	const FRotator CurrentControlRot = PC->GetControlRotation();
	const float DesiredYaw = PlayerToTarget2D.IsNearlyZero()
		                         ? CurrentControlRot.Yaw
		                         : PlayerToTarget2D.Rotation().Yaw;

	const FVector CamToFocus = (FocusPoint - CamLoc).GetSafeNormal();
	float DesiredPitch = CamToFocus.IsNearlyZero() ? LockPitchMax : CamToFocus.Rotation().Pitch;

	// Closer enemies pitch slightly higher overhead to keep both actors readable
	const float Dist = FVector::Dist(PlayerLoc, TargetLoc);
	const float CloseAlpha = 1.0f - FMath::Clamp(Dist / 700.0f, 0.0f, 1.0f);
	DesiredPitch = FMath::Clamp(DesiredPitch - (CloseAlpha * 6.0f), LockPitchMin, LockPitchMax);

	const FRotator DesiredControlRot(DesiredPitch, DesiredYaw, 0.0f);
	const FRotator NewControlRot = FMath::RInterpTo(
		CurrentControlRot,
		DesiredControlRot,
		DeltaTime,
		LockCameraRotInterpSpeed);
	PC->SetControlRotation(NewControlRot);

	// Smoothly apply shoulder offset & boss-scaled arm pull-back on BP_ThirdPersonCharacter's SpringArm
	if (EnsureSpringArmCached(Pawn))
	{
		const float RadiusScale = TargetComp->GetFramingRadiusScale();
		const float DesiredArmLength = BaseTargetArmLength + (LockExtraArmLength * RadiusScale);
		const FVector DesiredSocketOffset = BaseSocketOffset + LockAdditionalSocketOffset;

		CachedSpringArm->TargetArmLength = FMath::FInterpTo(
			CachedSpringArm->TargetArmLength,
			DesiredArmLength,
			DeltaTime,
			LockSpringArmInterpSpeed);

		CachedSpringArm->SocketOffset = FMath::VInterpTo(
			CachedSpringArm->SocketOffset,
			DesiredSocketOffset,
			DeltaTime,
			LockSpringArmInterpSpeed);
	}
}

void UCameraDirectorComponent::UpdateCameraCenterSnap(float DeltaTime, APlayerController* PC, APawn* Pawn)
{
	const FRotator CurrentRot = PC->GetControlRotation();
	const FRotator TargetRot(CenterSnapPitch, Pawn->GetActorRotation().Yaw, 0.0f);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, CenterSnapInterpSpeed);
	PC->SetControlRotation(NewRot);

	const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(NewRot.Yaw, TargetRot.Yaw));
	const float PitchDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(NewRot.Pitch, TargetRot.Pitch));
	if (YawDelta < 1.0f && PitchDelta < 1.0f)
	{
		bCenterSnapActive = false;
		UpdateTickState();
	}
}

void UCameraDirectorComponent::RestoreSpringArmToBase(float DeltaTime, APawn* Pawn)
{
	if (!EnsureSpringArmCached(Pawn))
	{
		bSpringArmNeedsRestore = false;
		UpdateTickState();
		return;
	}

	CachedSpringArm->TargetArmLength = FMath::FInterpTo(
		CachedSpringArm->TargetArmLength,
		BaseTargetArmLength,
		DeltaTime,
		LockSpringArmInterpSpeed);

	CachedSpringArm->SocketOffset = FMath::VInterpTo(
		CachedSpringArm->SocketOffset,
		BaseSocketOffset,
		DeltaTime,
		LockSpringArmInterpSpeed);

	const bool bArmDone = FMath::IsNearlyEqual(CachedSpringArm->TargetArmLength, BaseTargetArmLength, 1.0f);
	const bool bOffsetDone = CachedSpringArm->SocketOffset.Equals(BaseSocketOffset, 1.0f);

	if (bArmDone && bOffsetDone)
	{
		CachedSpringArm->TargetArmLength = BaseTargetArmLength;
		CachedSpringArm->SocketOffset = BaseSocketOffset;
		bSpringArmNeedsRestore = false;
		UpdateTickState();
	}
}

void UCameraDirectorComponent::NotifyMoveInputReleased()
{
	bIsBasisLatched = false;
	bNeedsSmoothCatchup = false;
}

void UCameraDirectorComponent::ComputeScreenBasis(const FRotator& CamRot, FVector& OutForward, FVector& OutRight) const
{
	// Derive Right from Camera Y first, then cross with World Up.
	// This stays completely gimbal-safe even if a puzzle camera points straight down (-90 pitch).
	const FRotationMatrix CamMatrix(CamRot);
	FVector Right = CamMatrix.GetUnitAxis(EAxis::Y);
	Right.Z = 0.f;

	if (!Right.Normalize())
	{
		Right = FVector::RightVector;
	}

	// In UE's left-handed Z-up space, Cross(Right, Up) = Forward
	const FVector Forward = FVector::CrossProduct(Right, FVector::UpVector).GetSafeNormal();

	OutForward = Forward;
	OutRight = Right;
}

FVector UCameraDirectorComponent::GetWorldMoveDirection(FVector2D MoveInput)
{
	if (MoveInput.SizeSquared() < 0.01f)
	{
		NotifyMoveInputReleased();
		return FVector::ZeroVector;
	}

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->PlayerCameraManager)
	{
		return FVector::ZeroVector;
	}

	// --- Z-TARGET ORBIT BASIS (Ocarina-style circular strafing in Free 3P and Fixed Camera Zones) ---
	if (bUseTargetOrbitMovement && ActiveLockTarget.IsValid())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			const FVector ToTarget = (ActiveLockTarget->GetLockWorldLocation() - Pawn->GetActorLocation()).
				GetSafeNormal2D();
			if (!ToTarget.IsNearlyZero())
			{
				ActiveForward = ToTarget;
				ActiveRight = FVector::CrossProduct(FVector::UpVector, ActiveForward).GetSafeNormal();
				LastMoveInputFrame = GFrameCounter;
				bHasValidBasis = true;
				bIsBasisLatched = false;
				bNeedsSmoothCatchup = false;

				const FVector WorldDir = (ActiveRight * MoveInput.X) + (ActiveForward * MoveInput.Y);
				return WorldDir.GetClampedToMaxSize(1.0f);
			}
		}
	}

	FVector LiveForward, LiveRight;
	ComputeScreenBasis(PC->PlayerCameraManager->GetCameraRotation(), LiveForward, LiveRight);

	// Treat a 2+ frame gap in input as a fresh press (works even if Completed wasn't bound)
	const bool bFreshPress = !bHasValidBasis || (GFrameCounter - LastMoveInputFrame) > 2;
	LastMoveInputFrame = GFrameCounter;
	bHasValidBasis = true;

	const FVector2D InputDir = MoveInput.GetSafeNormal();
	const bool bInLookAtZone = CurrentActiveZone.IsValid() && CurrentActiveZone->LookAt.bLookAtPlayer;

	if (bFreshPress)
	{
		ActiveForward = LiveForward;
		ActiveRight = LiveRight;
		LatchedStickDir = InputDir;
		bNeedsSmoothCatchup = false;
		bIsBasisLatched = (bInLookAtZone && bLatchBasisDuringLookAt);
	}
	else if (bIsBasisLatched)
	{
		const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(StickBreakAngleDegrees));
		if (FVector2D::DotProduct(InputDir, LatchedStickDir) < CosThreshold)
		{
			// Player intentionally steered in a new direction: unlatch and blend smoothly to the live view
			bIsBasisLatched = false;
			bNeedsSmoothCatchup = true;
		}
	}
	else
	{
		LatchedStickDir = InputDir;

		if (bNeedsSmoothCatchup)
		{
			const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
			const float CurrentYaw = ActiveForward.Rotation().Yaw;
			const float TargetYaw = LiveForward.Rotation().Yaw;
			const float NewYaw = FMath::RInterpTo(
				FRotator(0.f, CurrentYaw, 0.f),
				FRotator(0.f, TargetYaw, 0.f),
				DeltaTime,
				BasisCatchupSpeed
			).Yaw;

			ActiveForward = FRotator(0.f, NewYaw, 0.f).Vector();
			ActiveRight = FVector::CrossProduct(FVector::UpVector, ActiveForward).GetSafeNormal();

			if (FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(NewYaw, TargetYaw), 0.5f))
			{
				bNeedsSmoothCatchup = false;
			}
		}
		else
		{
			ActiveForward = LiveForward;
			ActiveRight = LiveRight;
		}
	}

	// X = Right/Left, Y = Forward/Back (standard UE5 Enhanced Input IA_Move layout)
	const FVector WorldDir = (ActiveRight * MoveInput.X) + (ActiveForward * MoveInput.Y);
	return WorldDir.GetClampedToMaxSize(1.0f);
}
