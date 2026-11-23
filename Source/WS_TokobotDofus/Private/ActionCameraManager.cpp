// ActionCameraManager.cpp
#include "ActionCameraManager.h"
#include "ActionCameraConfig.h"
#include "CameraDirectorComponent.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<bool> CVarDebugCameraWhiskers(
	TEXT("Camera.DebugWhiskers"),
	false,
	TEXT("Draw multi-feeler camera whiskers and center collision probe."),
	ECVF_Cheat
);

void AActionCameraManager::BeginPlay()
{
	Super::BeginPlay();
	if (Config)
	{
		CurrentArmLength = Config->DefaultArmLength;
	}
}

void AActionCameraManager::InitializeFor(APlayerController* PC)
{
	Super::InitializeFor(PC);
	CachedDirector = PC ? PC->FindComponentByClass<UCameraDirectorComponent>() : nullptr;
}

bool AActionCameraManager::IsInFixedCameraZone() const
{
	return CachedDirector.IsValid() && CachedDirector->IsInFixedCameraZone();
}

void AActionCameraManager::NotifyManualLookInput(float YawDelta, float PitchDelta)
{
	// Ignore right-stick/mouse look while inside an ACameraZoneActor so orbit doesn't drift off-screen
	if (!Config || IsInFixedCameraZone() || (FMath::IsNearlyZero(YawDelta) && FMath::IsNearlyZero(PitchDelta)))
	{
		return;
	}

	bIsRecentering = false;
	LastManualLookTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	OrbitRotation.Yaw = FRotator::NormalizeAxis(OrbitRotation.Yaw + YawDelta);
	OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + PitchDelta, Config->MinPitch, Config->MaxPitch);
}

void AActionCameraManager::TriggerRecenter()
{
	if (!LockedTarget.IsValid() && !IsInFixedCameraZone())
	{
		bIsRecentering = true;
	}
}

void AActionCameraManager::SetLockedTarget(AActor* NewTarget)
{
	LockedTarget = NewTarget;
	bIsRecentering = false;
}

void AActionCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	const APawn* Pawn = PCOwner ? PCOwner->GetPawn() : nullptr;

	// CRITICAL BRIDGE: When ViewTarget or PendingViewTarget is an ACameraZoneActor,
	// delegate to Super so ACameraZoneActor::CalcCamera runs and blends natively via VTBlend_Cubic.
	if (!Pawn || !Config || OutVT.Target != Pawn)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	const float ClampedDt = FMath::Min(DeltaTime, 0.05f);

	const float TargetBlend = LockedTarget.IsValid() ? 1.f : 0.f;
	LockOnBlendAlpha = FMath::FInterpTo(LockOnBlendAlpha, TargetBlend, ClampedDt, 8.f);

	FActionCameraState FreeState;
	EvaluateFreeOrbit(Pawn, ClampedDt, FreeState);

	FActionCameraState FinalState = FreeState;

	if (LockedTarget.IsValid() || LockOnBlendAlpha > KINDA_SMALL_NUMBER)
	{
		FActionCameraState LockState = FreeState;
		if (LockedTarget.IsValid())
		{
			EvaluateZTarget(Pawn, LockedTarget.Get(), ClampedDt, LockState);
			// Keep free orbit synced while locked so releasing Z-Target never snaps backward
			OrbitRotation = FMath::RInterpTo(OrbitRotation, LockState.ViewRotation, ClampedDt, 10.f);
		}

		const float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, LockOnBlendAlpha);
		FinalState.PivotLocation = FMath::Lerp(FreeState.PivotLocation, LockState.PivotLocation, SmoothAlpha);
		FinalState.ViewRotation = FQuat::Slerp(FreeState.ViewRotation.Quaternion(), LockState.ViewRotation.Quaternion(), SmoothAlpha).Rotator();
		FinalState.ArmLength = FMath::Lerp(FreeState.ArmLength, LockState.ArmLength, SmoothAlpha);
		FinalState.FOV = FMath::Lerp(FreeState.FOV, LockState.FOV, SmoothAlpha);
		FinalState.LocalSocketOffset = FMath::Lerp(FreeState.LocalSocketOffset, LockState.LocalSocketOffset, SmoothAlpha);
	}

	FVector Anchor = FinalState.PivotLocation + FRotationMatrix(FinalState.ViewRotation).TransformVector(FinalState.LocalSocketOffset);

	const bool bAllowWhiskerSteer = !LockedTarget.IsValid() && LockOnBlendAlpha < 0.05f && !bIsRecentering;
	const float PredictiveMaxDist = EvaluateWhiskers(Pawn, Anchor, FinalState.ViewRotation, FinalState.ArmLength, ClampedDt, bAllowWhiskerSteer);

	if (bAllowWhiskerSteer)
	{
		FinalState.ViewRotation = OrbitRotation;
		Anchor = FinalState.PivotLocation + FRotationMatrix(FinalState.ViewRotation).TransformVector(FinalState.LocalSocketOffset);
	}

	const FVector DesiredLoc = Anchor - (FinalState.ViewRotation.Vector() * FinalState.ArmLength);

	OutVT.POV.Location = ResolveCameraCollision(Pawn, Anchor, DesiredLoc, PredictiveMaxDist, ClampedDt);
	OutVT.POV.Rotation = FinalState.ViewRotation;
	OutVT.POV.FOV = FinalState.FOV;
}

void AActionCameraManager::EvaluateFreeOrbit(const APawn* Pawn, float DeltaTime, FActionCameraState& OutState)
{
	if (bIsRecentering)
	{
		const FRotator DesiredRot(-15.f, Pawn->GetActorRotation().Yaw, 0.f);
		OrbitRotation = FMath::RInterpTo(OrbitRotation, DesiredRot, DeltaTime, 12.f);
		if (OrbitRotation.Equals(DesiredRot, 1.5f))
		{
			bIsRecentering = false;
		}
	}
	else
	{
		ApplyRespectfulAutoFollow(Pawn, DeltaTime);
	}

	OutState.PivotLocation = Pawn->GetActorLocation();
	OutState.ViewRotation = OrbitRotation;
	OutState.ArmLength = Config->DefaultArmLength;
	OutState.FOV = Config->DefaultFOV;
	OutState.LocalSocketOffset = Config->FreeSocketOffset;
}

void AActionCameraManager::ApplyRespectfulAutoFollow(const APawn* Pawn, float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World) return;

	const double TimeSinceManualLook = World->GetTimeSeconds() - LastManualLookTime;
	if (TimeSinceManualLook < Config->ManualLookSuppressTime) return;

	FVector PlanarVel = Pawn->GetVelocity();
	PlanarVel.Z = 0.f;
	const float Speed = PlanarVel.Size();
	if (Speed < Config->MinSpeedForAutoFollow) return;

	const FVector MoveDir = PlanarVel / Speed;
	const FRotator PlanarCamRot(0.f, OrbitRotation.Yaw, 0.f);
	const FVector CamForward = PlanarCamRot.Vector();
	const FVector CamRight = FRotationMatrix(PlanarCamRot).GetScaledAxis(EAxis::Y);

	// Do not spin behind player when running directly toward the camera
	if (FVector::DotProduct(MoveDir, CamForward) < Config->TowardCameraDeadzoneDot) return;

	const float LateralStrength = FMath::Abs(FVector::DotProduct(MoveDir, CamRight));
	const float ResumeRamp = FMath::Clamp(static_cast<float>(TimeSinceManualLook - Config->ManualLookSuppressTime) / 0.5f, 0.f, 1.f);

	const float YawDelta = FRotator::NormalizeAxis(MoveDir.Rotation().Yaw - OrbitRotation.Yaw);
	const float MaxYawStep = Config->AutoFollowMaxYawRate * LateralStrength * ResumeRamp * DeltaTime;

	OrbitRotation.Yaw = FRotator::NormalizeAxis(OrbitRotation.Yaw + FMath::Clamp(YawDelta, -MaxYawStep, MaxYawStep));
}

void AActionCameraManager::EvaluateZTarget(const APawn* Pawn, const AActor* Target, float DeltaTime, FActionCameraState& OutState)
{
	const FVector PlayerPos = Pawn->GetActorLocation();
	const FVector TargetPos = Target->GetActorLocation();
	const float Separation = FVector::Dist(PlayerPos, TargetPos);

	const FVector FocalPoint = FMath::Lerp(PlayerPos, TargetPos, 0.35f);
	const FVector LookDir = (FocalPoint - (PlayerPos + FVector(0.f, 0.f, 75.f))).GetSafeNormal();
	FRotator LockRot = LookDir.Rotation();
	LockRot.Pitch = FMath::Clamp(LockRot.Pitch - 8.f, -35.f, 15.f);

	OutState.PivotLocation = PlayerPos;
	OutState.ViewRotation = LockRot;
	OutState.ArmLength = FMath::GetMappedRangeValueClamped(FVector2D(250.f, 1200.f), FVector2D(340.f, 540.f), Separation);
	OutState.FOV = 74.f;
	OutState.LocalSocketOffset = FVector(0.f, 55.f, 75.f);
}

float AActionCameraManager::EvaluateWhiskers(
	const APawn* Pawn,
	const FVector& Anchor,
	const FRotator& ViewRot,
	float DesiredArmLength,
	float DeltaTime,
	bool bAllowYawSteer)
{
	const UWorld* World = GetWorld();
	if (!World || Config->Whiskers.IsEmpty()) return DesiredArmLength;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ActionCameraWhiskers), false, Pawn);
	const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(Config->WhiskerProbeRadius);
	const FVector BaseArmDir = -ViewRot.Vector();

	float YawNudgeAccum = 0.f;
	float LeftClosestDist = DesiredArmLength;
	float RightClosestDist = DesiredArmLength;
	bool bLeftHit = false;
	bool bRightHit = false;

	const bool bDrawDebug = CVarDebugCameraWhiskers.GetValueOnGameThread();

	for (const FCameraWhiskerDef& Whisker : Config->Whiskers)
	{
		const FVector WhiskerDir = FRotator(0.f, Whisker.YawOffsetDeg, 0.f).RotateVector(BaseArmDir);
		const float ProbeLen = DesiredArmLength * Whisker.LengthScale;
		const FVector ProbeEnd = Anchor + (WhiskerDir * ProbeLen);

		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, Anchor, ProbeEnd, FQuat::Identity, ECC_Camera, ProbeShape, Params);

		if (bHit)
		{
			const float Proximity = 1.f - FMath::Clamp(Hit.Distance / FMath::Max(ProbeLen, 1.f), 0.f, 1.f);
			YawNudgeAccum -= FMath::Sign(Whisker.YawOffsetDeg) * Proximity * Whisker.SteeringWeight;

			if (Whisker.YawOffsetDeg > 0.f)
			{
				bLeftHit = true;
				LeftClosestDist = FMath::Min(LeftClosestDist, Hit.Distance);
			}
			else
			{
				bRightHit = true;
				RightClosestDist = FMath::Min(RightClosestDist, Hit.Distance);
			}
		}

#if ENABLE_DRAW_DEBUG
		if (bDrawDebug)
		{
			DrawDebugLine(World, Anchor, bHit ? Hit.Location : ProbeEnd, bHit ? FColor::Red : FColor::Green, false, -1.f, 0, 1.5f);
		}
#endif
	}

	const double TimeSinceManualLook = World->GetTimeSeconds() - LastManualLookTime;
	const float PlanarSpeed = Pawn->GetVelocity().Size2D();

	// Only steer yaw while moving and outside manual look cooldown (never drift while idle)
	if (bAllowYawSteer &&
		PlanarSpeed >= Config->MinSpeedForWhiskerSteer &&
		TimeSinceManualLook >= (Config->ManualLookSuppressTime * 0.5f) &&
		!FMath::IsNearlyZero(YawNudgeAccum))
	{
		const float ClampedTorque = FMath::Clamp(YawNudgeAccum, -1.f, 1.f);
		const float SpeedScale = FMath::Clamp(PlanarSpeed / 400.f, 0.25f, 1.f);
		const float YawStep = ClampedTorque * Config->WhiskerMaxYawNudgeRate * SpeedScale * DeltaTime;
		OrbitRotation.Yaw = FRotator::NormalizeAxis(OrbitRotation.Yaw + YawStep);
	}

	// Symmetric corridor squeeze: bilateral hits cancel yaw torque and smoothly tighten arm length
	if (bLeftHit && bRightHit)
	{
		const float BilateralAvgDist = 0.5f * (LeftClosestDist + RightClosestDist);
		return FMath::Lerp(DesiredArmLength, FMath::Max(BilateralAvgDist, Config->MinArmLength), Config->CorridorSqueezeAlpha);
	}

	return DesiredArmLength;
}

FVector AActionCameraManager::ResolveCameraCollision(
	const APawn* Pawn,
	const FVector& Anchor,
	const FVector& DesiredLoc,
	float PredictiveMaxDist,
	float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World) return DesiredLoc;

	const FVector ArmDir = (DesiredLoc - Anchor).GetSafeNormal();
	const float DesiredDist = FVector::Dist(Anchor, DesiredLoc);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ActionCameraCollision), false, Pawn);
	const bool bBlocked = World->SweepSingleByChannel(
		Hit, Anchor, DesiredLoc, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Config->CenterProbeRadius), Params
	);

#if ENABLE_DRAW_DEBUG
	if (CVarDebugCameraWhiskers.GetValueOnGameThread())
	{
		DrawDebugLine(World, Anchor, bBlocked ? Hit.Location : DesiredLoc, bBlocked ? FColor::Orange : FColor::Cyan, false, -1.f, 0, 2.5f);
	}
#endif

	const float CenterLimit = bBlocked ? FMath::Max(Hit.Distance - Config->WallPullInPadding, Config->MinArmLength) : DesiredDist;
	const float TargetDist = FMath::Min(CenterLimit, PredictiveMaxDist);
	const double Now = World->GetTimeSeconds();

	if (TargetDist < CurrentArmLength)
	{
		// Instant pull-in; refresh hysteresis timer
		LastCompressionTime = Now;
		CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetDist, DeltaTime, Config->PullInInterpSpeed);
	}
	else if ((Now - LastCompressionTime) >= Config->PullOutHysteresisDelay)
	{
		// Delayed, gentle pull-out prevents pole-popping
		CurrentArmLength = FMath::FInterpTo(CurrentArmLength, TargetDist, DeltaTime, Config->PullOutInterpSpeed);
	}

	return Anchor + (ArmDir * CurrentArmLength);
}