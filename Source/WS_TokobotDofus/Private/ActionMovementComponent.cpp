// ActionMovementComponent.cpp
#include "ActionMovementComponent.h"
#include "ActionMovementConfig.h"
#include "Components/CapsuleComponent.h"

namespace ActionMovementConstants
{
	// Capsule glides on a 2cm cushion so bottom-hemisphere seams never snag on ramps
	constexpr float FloorHoverMargin = 2.0f;
	// Shrinks sweep half-height upward by 4cm while keeping identical radius -> exact gap at every slope angle
	constexpr float FloorSweepShrink = 4.0f;
	constexpr int32 MaxSlideIterations = 4;
}

UActionMovementComponent::UActionMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UActionMovementComponent::IsWalkableFloor(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit() || !Config)
	{
		return false;
	}
	const float MinWalkableZ = FMath::Cos(FMath::DegreesToRadians(Config->MaxWalkableSlopeAngle));
	return Hit.ImpactNormal.Z >= MinWalkableZ;
}

FVector UActionMovementComponent::ProjectDirectionOntoSlope(const FVector& HorizontalDir, const FVector& SlopeNormal)
{
	const FVector Dir2D(HorizontalDir.X, HorizontalDir.Y, 0.f);
	if (Dir2D.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	if (SlopeNormal.Z <= 0.1f)
	{
		return Dir2D.GetSafeNormal();
	}

	// Solve Dx*Nx + Dy*Ny + Dz*Nz = 0 for Dz so the vector lies strictly in the slope plane
	// while keeping the exact horizontal (X, Y) heading intact.
	const float SlopeZ = -((Dir2D.X * SlopeNormal.X) + (Dir2D.Y * SlopeNormal.Y)) / SlopeNormal.Z;
	return FVector(Dir2D.X, Dir2D.Y, SlopeZ).GetSafeNormal();
}

void UActionMovementComponent::RequestJump()
{
	if (!GetWorld() || !Config) return;

	if (OnJumpIntercept.IsBound() && OnJumpIntercept.Execute())
	{
		LastJumpRequestTime = -10.0;
		return;
	}

	LastJumpRequestTime = GetWorld()->GetTimeSeconds();

	if (bIsGrounded || (LastJumpRequestTime - LastGroundedTime) <= Config->CoyoteTime)
	{
		ExecuteJump();
	}
}

void UActionMovementComponent::ReleaseJump()
{
	if (bIsJumping && Velocity.Z > 0.f && Config)
	{
		Velocity.Z *= Config->JumpCutVelocityScale;
		bIsJumping = false;
	}
}

void UActionMovementComponent::SetLockedTarget(AActor* NewTarget)
{
	LockedTarget = NewTarget;
}

void UActionMovementComponent::SetVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride)
{
	bVelocityOverrideActive = bEnableOverride;
	OverridePlanarVelocity = bEnableOverride ? FVector(PlanarVelocity.X, PlanarVelocity.Y, 0.f) : FVector::ZeroVector;
}

void UActionMovementComponent::LaunchPawn(const FVector& LaunchVelocity, bool bPreserveAirMomentum)
{
	Velocity = LaunchVelocity;
	bIsGrounded = false;
	bIsJumping = false;
	bPreserveMomentumInAir = bPreserveAirMomentum;
	bVelocityOverrideActive = false;
	bLaunchedDuringMove = true;
	LastGroundedTime = -10.0;
	LastJumpRequestTime = -10.0;

	OnGroundedStateChanged.Broadcast(false);
}

void UActionMovementComponent::ExecuteJump()
{
	if (!Config) return;

	Velocity.Z = Config->GetJumpInitialVelocity();
	bIsGrounded = false;
	bIsJumping = true;
	bPreserveMomentumInAir = false;
	bLaunchedDuringMove = true;
	LastGroundedTime = -10.0;
	LastJumpRequestTime = -10.0;

	OnGroundedStateChanged.Broadcast(false);
}

void UActionMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PawnOwner || !UpdatedComponent || !Config || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	const float SubDt = FMath::Min(DeltaTime, 0.05f);
	const FVector InputDir = ConsumeInputVector().GetClampedToMaxSize(1.f);
	const bool bWasGroundedBeforeMove = bIsGrounded;

	IntegratePlanarVelocity(SubDt, InputDir);
	IntegrateVerticalVelocity(SubDt);
	MoveWithCollideAndSlide(Velocity * SubDt);

	ProbeAndSnapToFloor(bWasGroundedBeforeMove);

	UpdatePawnRotation(SubDt, InputDir);
}

void UActionMovementComponent::IntegratePlanarVelocity(float DeltaTime, const FVector& InputDir)
{
	// 1. Direct velocity override (e.g., CarryRush Charge)
	if (bVelocityOverrideActive)
	{
		if (bIsGrounded)
		{
			const float Speed = OverridePlanarVelocity.Size();
			Velocity = (Speed > KINDA_SMALL_NUMBER)
				? (ProjectDirectionOntoSlope(OverridePlanarVelocity, GroundNormal) * Speed)
				: FVector::ZeroVector;
		}
		else
		{
			Velocity.X = OverridePlanarVelocity.X;
			Velocity.Y = OverridePlanarVelocity.Y;
		}
		return;
	}

	// Reconstruct true surface speed along the current horizontal heading (zero cosine loss on slopes)
	FVector PlanarVel(Velocity.X, Velocity.Y, 0.f);
	if (bIsGrounded && !PlanarVel.IsNearlyZero())
	{
		PlanarVel = PlanarVel.GetSafeNormal() * Velocity.Size();
	}

	// 2. Drag-free air momentum preservation (e.g., CarryRush Momentum Jump)
	if (!bIsGrounded && bPreserveMomentumInAir)
	{
		if (!InputDir.IsNearlyZero())
		{
			const float CurrentSpeed = FMath::Max(PlanarVel.Size(), Config->MaxRunSpeed);
			const FVector SteeredDir = FMath::VInterpNormalRotationTo(
				PlanarVel.IsNearlyZero() ? InputDir : PlanarVel.GetSafeNormal(),
				InputDir.GetSafeNormal(),
				DeltaTime,
				45.f
			);
			PlanarVel = SteeredDir * CurrentSpeed;
		}
		Velocity.X = PlanarVel.X;
		Velocity.Y = PlanarVel.Y;
		return;
	}

	// 3. Standard locomotion with distinct Ground/Air Accel & Decel + optional Slope modifiers
	const bool bIsStrafing = LockedTarget.IsValid();
	float TargetSpeed = bIsStrafing ? Config->StrafeSpeed : Config->MaxRunSpeed;

	// Compute directional slope grade: +1 = full downhill, 0 = flat or pure sideways, -1 = full uphill
	float SlopeGradeAlpha = 0.f;
	if (bIsGrounded && !InputDir.IsNearlyZero())
	{
		const FVector InputHeading = InputDir.GetSafeNormal2D();
		const float MaxSlopeSin = FMath::Max(FMath::Sin(FMath::DegreesToRadians(Config->MaxWalkableSlopeAngle)), 0.1f);
		const float RawGrade = FVector::DotProduct(InputHeading, FVector(GroundNormal.X, GroundNormal.Y, 0.f));
		SlopeGradeAlpha = FMath::Clamp(RawGrade / MaxSlopeSin, -1.f, 1.f);

		const float SlopeSpeedMult = (SlopeGradeAlpha >= 0.f)
			? FMath::Lerp(1.f, Config->DownhillSpeedMultiplier, SlopeGradeAlpha)
			: FMath::Lerp(1.f, Config->UphillSpeedMultiplier, -SlopeGradeAlpha);

		TargetSpeed *= SlopeSpeedMult;
	}

	if (!InputDir.IsNearlyZero())
	{
		const FVector DesiredVel = InputDir * TargetSpeed;
		const float CurrentSpeed = PlanarVel.Size();
		const float DesiredSpeed = DesiredVel.Size();

		const bool bIsSlowingDown = CurrentSpeed > (DesiredSpeed + 5.f);
		const bool bInApexHang = !bIsGrounded && FMath::Abs(Velocity.Z) < Config->ApexHangSpeedThreshold;

		float Rate = 0.f;
		if (bIsGrounded)
		{
			Rate = bIsSlowingDown ? Config->GroundDeceleration : Config->GroundAcceleration;
			Rate = FMath::Max(Rate + (SlopeGradeAlpha * Config->SlopeAccelInfluence), 200.f);

			// Boost turn rate when reversing direction so pivots stay crisp even with weightier acceleration
			if (CurrentSpeed > 10.f)
			{
				const float Alignment = FVector::DotProduct(PlanarVel / CurrentSpeed, InputDir.GetSafeNormal2D());
				if (Alignment < 0.f)
				{
					Rate *= Config->PivotTurnBoost;
				}
			}
		}
		else
		{
			Rate = bIsSlowingDown ? Config->AirDeceleration : Config->AirAcceleration;
			if (bInApexHang && !bIsSlowingDown)
			{
				Rate *= Config->ApexAirControlBoost;
			}
		}

		PlanarVel = FMath::VInterpConstantTo(PlanarVel, DesiredVel, DeltaTime, Rate);
	}
	else
	{
		// Stick released: apply GroundDeceleration or AirDeceleration
		const float DecelRate = bIsGrounded ? Config->GroundDeceleration : Config->AirDeceleration;
		if (DecelRate > 0.f)
		{
			PlanarVel = FMath::VInterpConstantTo(PlanarVel, FVector::ZeroVector, DeltaTime, DecelRate);
		}
	}

	if (bIsGrounded)
	{
		const float SurfaceSpeed = PlanarVel.Size();
		Velocity = (SurfaceSpeed > KINDA_SMALL_NUMBER)
			? (ProjectDirectionOntoSlope(PlanarVel, GroundNormal) * SurfaceSpeed)
			: FVector::ZeroVector;
	}
	else
	{
		Velocity.X = PlanarVel.X;
		Velocity.Y = PlanarVel.Y;
	}
}

void UActionMovementComponent::IntegrateVerticalVelocity(float DeltaTime)
{
	if (bIsGrounded) return;

	float Gravity = Config->GetRiseGravity();
	if (FMath::Abs(Velocity.Z) < Config->ApexHangSpeedThreshold)
	{
		Gravity *= Config->ApexGravityMultiplier;
	}
	else if (Velocity.Z < 0.f)
	{
		Gravity *= Config->FallGravityMultiplier;
	}

	Velocity.Z = FMath::Max(Velocity.Z - Gravity * DeltaTime, -Config->MaxFallSpeed);
}

void UActionMovementComponent::MoveWithCollideAndSlide(const FVector& DeltaMove)
{
	if (DeltaMove.IsNearlyZero())
	{
		return;
	}

	bLaunchedDuringMove = false;
	FVector RemainingMove = DeltaMove;
	const FQuat Rot = UpdatedComponent->GetComponentQuat();

	for (int32 Iter = 0; Iter < ActionMovementConstants::MaxSlideIterations && !RemainingMove.IsNearlyZero(); ++Iter)
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(RemainingMove, Rot, true, Hit);

		if (!Hit.IsValidBlockingHit())
		{
			break;
		}

		if (Hit.bStartPenetrating)
		{
			const FVector Adjustment = GetPenetrationAdjustment(Hit);
			if (!ResolvePenetration(Adjustment, Hit, Rot))
			{
				break;
			}
			continue;
		}

		const float RemainingFraction = FMath::Max(0.f, 1.f - Hit.Time);
		const FVector LeftoverMove = RemainingMove * RemainingFraction;

		const bool bHitWalkable = IsWalkableFloor(Hit);
		const bool bIsWall = !bHitWalkable && (Hit.ImpactNormal.Z > -0.5f);

		if (bIsGrounded && !bVelocityOverrideActive && bIsWall && TryStepUp(LeftoverMove, Hit))
		{
			break;
		}

		if (bIsWall)
		{
			OnBlockingWallHit.Broadcast(Hit.GetActor(), Hit);
			if (bLaunchedDuringMove)
			{
				break;
			}
		}

		if (bIsGrounded && bHitWalkable)
		{
			// Concave slope transition (e.g., flat -> uphill or downhill -> flat): keep exact heading & 3D speed
			const float RemainingDist = LeftoverMove.Size();
			GroundNormal = Hit.ImpactNormal;
			const FVector SlopeDir = ProjectDirectionOntoSlope(LeftoverMove, GroundNormal);
			RemainingMove = !SlopeDir.IsNearlyZero()
				? (SlopeDir * RemainingDist)
				: (FVector::VectorPlaneProject(LeftoverMove, Hit.ImpactNormal).GetSafeNormal() * RemainingDist);
		}
		else if (bIsGrounded && bIsWall)
		{
			// Grounded slide along a wall: strip inward horizontal wall component, then keep motion tangent to GroundNormal
			const FVector WallNormal2D = FVector(Hit.Normal.X, Hit.Normal.Y, 0.f).GetSafeNormal();
			const float VelIntoWall = FVector::DotProduct(Velocity, WallNormal2D);
			if (VelIntoWall < 0.f)
			{
				Velocity -= WallNormal2D * VelIntoWall;
			}

			const FVector Leftover2D(LeftoverMove.X, LeftoverMove.Y, 0.f);
			const FVector Slide2D = FVector::VectorPlaneProject(Leftover2D, WallNormal2D);
			const float SlideDist = Slide2D.Size();
			RemainingMove = (SlideDist > KINDA_SMALL_NUMBER)
				? (ProjectDirectionOntoSlope(Slide2D, GroundNormal) * SlideDist)
				: FVector::ZeroVector;
		}
		else
		{
			// Airborne wall/ceiling slide: strip inward normal velocity so capsule glides and falls freely
			const float VelIntoSurface = FVector::DotProduct(Velocity, Hit.Normal);
			if (VelIntoSurface < 0.f)
			{
				Velocity -= Hit.Normal * VelIntoSurface;
			}
			RemainingMove = FVector::VectorPlaneProject(LeftoverMove, Hit.Normal);
		}
	}
}

bool UActionMovementComponent::TryStepUp(const FVector& RemainingMove, const FHitResult& WallHit)
{
	const FVector PreStepLoc = UpdatedComponent->GetComponentLocation();
	const FQuat Rot = UpdatedComponent->GetComponentQuat();
	const FVector StepUpDelta(0.f, 0.f, Config->MaxStepHeight);

	FHitResult StepUpHit;
	SafeMoveUpdatedComponent(StepUpDelta, Rot, true, StepUpHit);

	const FVector PlanarStep(RemainingMove.X, RemainingMove.Y, 0.f);
	if (PlanarStep.IsNearlyZero())
	{
		UpdatedComponent->SetWorldLocation(PreStepLoc, false, nullptr, ETeleportType::TeleportPhysics);
		return false;
	}

	FHitResult ForwardHit;
	SafeMoveUpdatedComponent(PlanarStep, Rot, true, ForwardHit);

	if (!ForwardHit.IsValidBlockingHit())
	{
		FHitResult StepDownHit;
		SafeMoveUpdatedComponent(-StepUpDelta, Rot, true, StepDownHit);

		if (IsWalkableFloor(StepDownHit))
		{
			GroundNormal = StepDownHit.ImpactNormal;
			return true;
		}
	}

	UpdatedComponent->SetWorldLocation(PreStepLoc, false, nullptr, ETeleportType::TeleportPhysics);
	return false;
}

void UActionMovementComponent::ProbeAndSnapToFloor(bool bWasGroundedBeforeMove)
{
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	const UWorld* World = GetWorld();
	if (!Capsule || !World) return;

	if (!bWasGroundedBeforeMove && Velocity.Z > 10.f)
	{
		bIsGrounded = false;
		GroundNormal = FVector::UpVector;
		return;
	}

	const float RealRadius = Capsule->GetScaledCapsuleRadius();
	const float RealHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// Tiny 0.25cm radial trim avoids grazing vertical walls while preserving exact slope contact geometry
	const float SweepRadius = FMath::Max(RealRadius - 0.25f, 1.f);
	const float SweepShrink = FMath::Min(ActionMovementConstants::FloorSweepShrink, FMath::Max(RealHalfHeight - SweepRadius - 1.f, 0.f));
	const float SweepHalfHeight = RealHalfHeight - SweepShrink;

	const FVector Start = Capsule->GetComponentLocation();
	const float MaxSnapDist = bWasGroundedBeforeMove
		? FMath::Max(Config->MaxStepHeight, 28.f)
		: (ActionMovementConstants::FloorHoverMargin + 1.5f);
	const FVector End = Start - FVector(0.f, 0.f, SweepShrink + MaxSnapDist);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ActionGroundProbe), false, PawnOwner);
	FCollisionResponseParams ResponseParams(Capsule->GetCollisionResponseToChannels());
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(SweepRadius, SweepHalfHeight);

	FHitResult FloorHit;
	const bool bHit = World->SweepSingleByChannel(
		FloorHit,
		Start,
		End,
		FQuat::Identity,
		Capsule->GetCollisionObjectType(),
		Shape,
		QueryParams,
		ResponseParams
	);

	const bool bValidFloor = bHit && !FloorHit.bStartPenetrating && IsWalkableFloor(FloorHit);

	if (bValidFloor)
	{
		// Maintain exact 2cm hover margin in both directions (pulls down on downhill crests, lifts cleanly off ramp seams)
		const float ActualFloorGap = FloorHit.Distance - SweepShrink;
		const float AdjustZ = ActionMovementConstants::FloorHoverMargin - ActualFloorGap;
		if (FMath::Abs(AdjustZ) > 0.1f)
		{
			FHitResult SnapHit;
			SafeMoveUpdatedComponent(FVector(0.f, 0.f, AdjustZ), UpdatedComponent->GetComponentQuat(), true, SnapHit);
		}

		bIsGrounded = true;
		GroundNormal = FloorHit.ImpactNormal;
		LastGroundedTime = World->GetTimeSeconds();
		bIsJumping = false;

		if (!bWasGroundedBeforeMove)
		{
			Velocity.Z = 0.f;
			bPreserveMomentumInAir = false;
			OnGroundedStateChanged.Broadcast(true);

			if ((LastGroundedTime - LastJumpRequestTime) <= Config->JumpBufferWindow)
			{
				ExecuteJump();
			}
		}
	}
	else
	{
		bIsGrounded = false;
		GroundNormal = FVector::UpVector;

		if (bWasGroundedBeforeMove)
		{
			Velocity.Z = FMath::Max(Velocity.Z, 0.f);
			OnGroundedStateChanged.Broadcast(false);
		}
	}
}

void UActionMovementComponent::UpdatePawnRotation(float DeltaTime, const FVector& InputDir)
{
	if (bVelocityOverrideActive)
	{
		return;
	}

	const FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
	FRotator TargetRot = CurrentRot;

	if (LockedTarget.IsValid())
	{
		FVector ToTarget = LockedTarget->GetActorLocation() - UpdatedComponent->GetComponentLocation();
		ToTarget.Z = 0.f;
		if (!ToTarget.IsNearlyZero())
		{
			TargetRot = ToTarget.Rotation();
		}
	}
	else if (!InputDir.IsNearlyZero())
	{
		TargetRot = InputDir.Rotation();
	}

	const float InterpSpeed = LockedTarget.IsValid() ? 22.f : 14.f;
	UpdatedComponent->SetWorldRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, InterpSpeed));
}