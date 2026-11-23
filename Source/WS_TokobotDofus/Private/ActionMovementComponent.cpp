// ActionMovementComponent.cpp
#include "ActionMovementComponent.h"
#include "ActionMovementConfig.h"
#include "Components/CapsuleComponent.h"

UActionMovementComponent::UActionMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UActionMovementComponent::RequestJump()
{
	if (!GetWorld() || !Config) return;

	// Allow active stances (e.g., CarryRush Momentum Jump) to intercept Jump before standard jumping runs
	if (OnJumpIntercept.IsBound() && OnJumpIntercept.Execute())
	{
		LastJumpRequestTime = -10.0;
		return;
	}

	LastJumpRequestTime = GetWorld()->GetTimeSeconds();

	// Execute immediately if grounded or within Coyote window
	if (bIsGrounded || (LastJumpRequestTime - LastGroundedTime) <= Config->CoyoteTime)
	{
		ExecuteJump();
	}
}

void UActionMovementComponent::ReleaseJump()
{
	// Variable jump height cut only applies to player-initiated vertical jumps (not launches/bounces)
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
	bIsJumping = false; // Prevent Jump-release cut from cancelling squad launches or wall bounces
	bPreserveMomentumInAir = bPreserveAirMomentum;
	bVelocityOverrideActive = false;
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
	LastGroundedTime = -10.0;      // Consume coyote window
	LastJumpRequestTime = -10.0;   // Consume jump buffer

	OnGroundedStateChanged.Broadcast(false);
}

void UActionMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PawnOwner || !UpdatedComponent || !Config || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// Clamp frame hitches to keep swept collision deterministic
	const float SubDt = FMath::Min(DeltaTime, 0.05f);
	const FVector InputDir = ConsumeInputVector().GetClampedToMaxSize(1.f);

	FHitResult GroundHit;
	ProbeGround(GroundHit);

	IntegratePlanarVelocity(SubDt, InputDir);
	IntegrateVerticalVelocity(SubDt);
	MoveWithStepUpAndSlide(Velocity * SubDt);
	UpdatePawnRotation(SubDt, InputDir);
}

void UActionMovementComponent::ProbeGround(FHitResult& OutGroundHit)
{
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	if (!Capsule || !GetWorld()) return;

	const FVector Start = Capsule->GetComponentLocation();
	// Deeper probe when grounded keeps the pawn glued to descending ramps without micro-hopping
	const float ProbeDist = bIsGrounded ? 18.f : 4.f;
	const FVector End = Start - FVector(0.f, 0.f, ProbeDist);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ActionGroundProbe), false, PawnOwner);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius() * 0.92f,
		Capsule->GetScaledCapsuleHalfHeight()
	);

	const bool bHit = GetWorld()->SweepSingleByChannel(
		OutGroundHit, Start, End, FQuat::Identity, ECC_Visibility, Shape, Params
	);

	const float MinWalkableZ = FMath::Cos(FMath::DegreesToRadians(Config->MaxWalkableSlopeAngle));
	const bool bValidFloor = bHit && OutGroundHit.ImpactNormal.Z >= MinWalkableZ && Velocity.Z <= 10.f;

	const bool bWasGrounded = bIsGrounded;
	bIsGrounded = bValidFloor;

	if (bIsGrounded)
	{
		GroundNormal = OutGroundHit.ImpactNormal;
		LastGroundedTime = GetWorld()->GetTimeSeconds();
		bIsJumping = false;

		if (!bWasGrounded)
		{
			Velocity.Z = 0.f;
			bPreserveMomentumInAir = false;
			OnGroundedStateChanged.Broadcast(true);

			// Fire buffered jump immediately on touchdown
			if ((LastGroundedTime - LastJumpRequestTime) <= Config->JumpBufferWindow)
			{
				ExecuteJump();
			}
		}
	}
	else if (bWasGrounded)
	{
		GroundNormal = FVector::UpVector;
		OnGroundedStateChanged.Broadcast(false);
	}
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
				? (FVector::VectorPlaneProject(OverridePlanarVelocity, GroundNormal).GetSafeNormal() * Speed)
				: FVector::ZeroVector;
		}
		else
		{
			Velocity.X = OverridePlanarVelocity.X;
			Velocity.Y = OverridePlanarVelocity.Y;
		}
		return;
	}

	FVector PlanarVel(Velocity.X, Velocity.Y, 0.f);

	// 2. Drag-free momentum preservation in mid-air (e.g., CarryRush Momentum Jump)
	if (!bIsGrounded && bPreserveMomentumInAir)
	{
		// Preserve launch speed while allowing subtle steering without clamping down to MaxRunSpeed
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

	// 3. Standard modern OoT locomotion
	const bool bIsStrafing = LockedTarget.IsValid();
	const float TargetSpeed = bIsStrafing ? Config->StrafeSpeed : Config->MaxRunSpeed;

	const bool bInApexHang = !bIsGrounded && FMath::Abs(Velocity.Z) < Config->ApexHangSpeedThreshold;
	float Accel = bIsGrounded
		? Config->GroundAcceleration
		: (Config->GroundAcceleration * 0.65f * (bInApexHang ? Config->ApexAirControlBoost : 1.f));

	if (!InputDir.IsNearlyZero())
	{
		const FVector DesiredVel = InputDir * TargetSpeed;

		// Pivot boost when reversing direction on ground eliminates sluggish arc turns
		if (bIsGrounded && !PlanarVel.IsNearlyZero())
		{
			const float Alignment = FVector::DotProduct(PlanarVel.GetSafeNormal(), InputDir.GetSafeNormal());
			if (Alignment < 0.f)
			{
				Accel *= Config->PivotTurnBoost;
			}
		}

		PlanarVel = FMath::VInterpConstantTo(PlanarVel, DesiredVel, DeltaTime, Accel);
	}
	else if (bIsGrounded)
	{
		PlanarVel = FMath::VInterpConstantTo(PlanarVel, FVector::ZeroVector, DeltaTime, Config->GroundDeceleration);
	}

	// Project onto walkable slope so uphill/downhill travel maintains constant speed
	if (bIsGrounded)
	{
		const float Speed = PlanarVel.Size();
		Velocity = (Speed > KINDA_SMALL_NUMBER)
			? (FVector::VectorPlaneProject(PlanarVel, GroundNormal).GetSafeNormal() * Speed)
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

	// Asymmetric gravity: crisp launch, subtle apex hang, weighty descent
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

void UActionMovementComponent::MoveWithStepUpAndSlide(const FVector& DeltaMove)
{
	FHitResult Hit;
	SafeMoveUpdatedComponent(DeltaMove, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (!Hit.IsValidBlockingHit())
	{
		return;
	}

	// Attempt step-up when grounded and encountering a low vertical lip (not during a high-speed Rush Charge wall hit)
	const bool bIsVerticalSurface = FMath::Abs(Hit.ImpactNormal.Z) < 0.5f;
	if (bIsGrounded && !bVelocityOverrideActive && bIsVerticalSurface)
	{
		const FVector PreStepLoc = UpdatedComponent->GetComponentLocation();
		const FVector StepUpDelta(0.f, 0.f, Config->MaxStepHeight);

		FHitResult StepUpHit;
		SafeMoveUpdatedComponent(StepUpDelta, UpdatedComponent->GetComponentQuat(), true, StepUpHit);

		const FVector RemainingPlanar = FVector(DeltaMove.X, DeltaMove.Y, 0.f) * (1.f - Hit.Time);
		FHitResult ForwardHit;
		SafeMoveUpdatedComponent(RemainingPlanar, UpdatedComponent->GetComponentQuat(), true, ForwardHit);

		// Only commit step-up if we cleared the lip horizontally and landed on a walkable top surface
		if (!ForwardHit.IsValidBlockingHit())
		{
			FHitResult StepDownHit;
			SafeMoveUpdatedComponent(-StepUpDelta, UpdatedComponent->GetComponentQuat(), true, StepDownHit);

			const float MinWalkableZ = FMath::Cos(FMath::DegreesToRadians(Config->MaxWalkableSlopeAngle));
			if (StepDownHit.IsValidBlockingHit() && StepDownHit.ImpactNormal.Z >= MinWalkableZ)
			{
				return;
			}
		}

		// Step-up failed (too tall or unwalkable top): revert cleanly before wall event + tangent slide
		UpdatedComponent->SetWorldLocation(PreStepLoc, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Broadcast wall impact to listeners (e.g., CarryRush wall crash bounce)
	if (bIsVerticalSurface)
	{
		OnBlockingWallHit.Broadcast(Hit.GetActor(), Hit);

		// If a listener launched the pawn away from the wall, skip sliding along the old DeltaMove
		if (!bIsGrounded && !bVelocityOverrideActive)
		{
			return;
		}
	}

	SlideAlongSurface(DeltaMove, 1.f - Hit.Time, Hit.Normal, Hit, true);
}

void UActionMovementComponent::UpdatePawnRotation(float DeltaTime, const FVector& InputDir)
{
	// Let active stances (like CarryRush) own facing direction while overriding velocity
	if (bVelocityOverrideActive)
	{
		return;
	}

	const FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
	FRotator TargetRot = CurrentRot;

	if (LockedTarget.IsValid())
	{
		// Z-Targeting: continuously orient toward locked enemy
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