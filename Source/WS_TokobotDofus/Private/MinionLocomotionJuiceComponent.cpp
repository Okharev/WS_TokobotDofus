#include "MinionLocomotionJuiceComponent.h"
#include "SquashStretchComponent.h"
#include "MinionAgentInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

UMinionLocomotionJuiceComponent::UMinionLocomotionJuiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMinionLocomotionJuiceComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (const ACharacter* OwnerChar = Cast<ACharacter>(Owner))
	{
		TargetMesh = OwnerChar->GetMesh();
	}
	else
	{
		TargetMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (TargetMesh)
	{
		BaseMeshRelQuat = TargetMesh->GetRelativeRotation().Quaternion();
	}

	CachedSquashComp = Owner->FindComponentByClass<USquashStretchComponent>();
	PrevYaw = Owner->GetActorRotation().Yaw;

	// Randomize starting waddle phase so squad members never sway in unison
	WaddlePhase = FMath::FRandRange(0.0f, 2.0f * PI);
}

void UMinionLocomotionJuiceComponent::SetLocomotionJuiceEnabled(bool bEnabled)
{
	if (bJuiceEnabled == bEnabled)
	{
		return;
	}

	bJuiceEnabled = bEnabled;
	if (!bJuiceEnabled)
	{
		CurrentBankRoll = 0.0f;
		CurrentLeanPitch = 0.0f;
		if (bApplyToOwnerMeshRotation && IsValid(TargetMesh))
		{
			TargetMesh->SetRelativeRotation(BaseMeshRelQuat);
		}
	}
}

void UMinionLocomotionJuiceComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AActor* Owner = GetOwner();
	if (!Owner || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float ClampedDt = FMath::Min(DeltaTime, 0.05f);

	bool bAllowTilt = bJuiceEnabled;
	if (bAllowTilt)
	{
		if (const IMinionAgentInterface* Agent = Cast<IMinionAgentInterface>(Owner))
		{
			const EMinionState State = Agent->GetMinionState();
			bAllowTilt = (State == EMinionState::Idle ||
						  State == EMinionState::Following ||
						  State == EMinionState::EvadingPlayer);
		}
	}

	if (!bAllowTilt)
	{
		CurrentBankRoll = FMath::FInterpTo(CurrentBankRoll, 0.0f, ClampedDt, TiltInterpSpeed * 1.5f);
		CurrentLeanPitch = FMath::FInterpTo(CurrentLeanPitch, 0.0f, ClampedDt, TiltInterpSpeed * 1.5f);
		if (bApplyToOwnerMeshRotation && IsValid(TargetMesh))
		{
			const FQuat TiltQuat = FRotator(CurrentLeanPitch, 0.0f, CurrentBankRoll).Quaternion();
			TargetMesh->SetRelativeRotation(TiltQuat * BaseMeshRelQuat);
		}
		SmoothedVel2D = FVector::ZeroVector;
		PrevSmoothedVel2D = FVector::ZeroVector;
		SmoothedYawRate = 0.0f;
		RecentPeakSpeed = 0.0f;
		PrevYaw = Owner->GetActorRotation().Yaw;
		return;
	}

	// 1. Low-pass filter velocity to strip Detour Crowd 1-frame micro-corrections
	const FVector RawVel = Owner->GetVelocity();
	const FVector RawVel2D(RawVel.X, RawVel.Y, 0.0f);
	SmoothedVel2D = FMath::VInterpTo(SmoothedVel2D, RawVel2D, ClampedDt, InputFilterSpeed);

	const float Speed2D = SmoothedVel2D.Size();
	const float SpeedAlpha = FMath::Clamp(Speed2D / FMath::Max(ReferenceMaxSpeed, 1.0f), 0.0f, 1.3f);

	// 2. Filtered Yaw Rate with Deadzone (ignores straight-line crowd jitter, banks on real turns)
	const float CurrentYaw = Owner->GetActorRotation().Yaw;
	const float RawYawRate = FMath::Clamp(
		FMath::FindDeltaAngleDegrees(PrevYaw, CurrentYaw) / ClampedDt,
		-540.0f,
		540.0f);
	PrevYaw = CurrentYaw;

	SmoothedYawRate = FMath::FInterpTo(SmoothedYawRate, RawYawRate, ClampedDt, InputFilterSpeed);

	float EffectiveYawRate = 0.0f;
	if (FMath::Abs(SmoothedYawRate) > BankYawRateDeadzone)
	{
		EffectiveYawRate = FMath::Sign(SmoothedYawRate) * (FMath::Abs(SmoothedYawRate) - BankYawRateDeadzone);
	}

	// Only bank noticeably once actually moving past a slow shuffle
	const float BankSpeedGate = FMath::GetMappedRangeValueClamped(FVector2D(0.35f, 0.9f), FVector2D(0.0f, 1.0f), SpeedAlpha);
	const float TargetBank = FMath::Clamp(
		EffectiveYawRate * BankSensitivity * BankSpeedGate,
		-MaxBankRollDegrees,
		MaxBankRollDegrees);

	// 3. Smooth Acceleration / Braking Pitch Lean
	const FVector SmoothAccel2D = (SmoothedVel2D - PrevSmoothedVel2D) / ClampedDt;
	PrevSmoothedVel2D = SmoothedVel2D;

	const FVector Forward2D = Owner->GetActorForwardVector().GetSafeNormal2D();
	const float ForwardAccel = FVector::DotProduct(SmoothAccel2D, Forward2D);

	float TargetPitch = 0.0f;
	if (ForwardAccel >= -150.0f)
	{
		// Steady run lean proportional to speed (no jittery accel spikes)
		const float RunLean = FMath::GetMappedRangeValueClamped(FVector2D(0.25f, 1.0f), FVector2D(0.0f, 0.75f), SpeedAlpha);
		const float PushLean = FMath::Clamp(FMath::Max(ForwardAccel, 0.0f) / FMath::Max(ReferenceAcceleration, 1.0f), 0.0f, 0.35f);
		TargetPitch = -FMath::Clamp(RunLean + PushLean, 0.0f, 1.0f) * MaxForwardLeanDegrees;
	}
	else
	{
		const float BrakeNorm = FMath::Clamp((-ForwardAccel - 150.0f) / FMath::Max(ReferenceAcceleration, 1.0f), 0.0f, 1.0f);
		TargetPitch = BrakeNorm * MaxBrakeLeanBackDegrees;
	}

	// 4. Hard Brake Detection (only when coming down from a real sprint, never mid-crowd-shuffle)
	RecentPeakSpeed = (Speed2D > RecentPeakSpeed)
		? Speed2D
		: FMath::FInterpTo(RecentPeakSpeed, Speed2D, ClampedDt, 2.5f);

	BrakeCooldown = FMath::Max(0.0f, BrakeCooldown - ClampedDt);
	const bool bRealStopFromSprint = (RecentPeakSpeed > ReferenceMaxSpeed * 0.75f) &&
		(ForwardAccel < HardBrakeAccelThreshold) &&
		(Speed2D < ReferenceMaxSpeed * 0.45f);

	if (bRealStopFromSprint && BrakeCooldown <= 0.0f)
	{
		BrakeCooldown = 0.6f;
		RecentPeakSpeed = Speed2D;
		OnHardBrake.Broadcast(Forward2D);
		if (bTriggerSquashOnHardBrake && CachedSquashComp)
		{
			CachedSquashComp->TriggerImpactAftershock(-3.5f, FVector::UpVector, -0.06f);
		}
	}

	// 5. Optional Waddle (off by default when MaxWaddleRollDegrees == 0)
	float WaddleOffset = 0.0f;
	if (SpeedAlpha > 0.15f && MaxWaddleRollDegrees > KINDA_SMALL_NUMBER)
	{
		WaddlePhase = FMath::Fmod(WaddlePhase + (ClampedDt * WaddleFrequency * FMath::Clamp(SpeedAlpha, 0.5f, 1.25f)), 2.0f * PI);
		WaddleOffset = FMath::Sin(WaddlePhase) * MaxWaddleRollDegrees * FMath::Min(SpeedAlpha, 1.0f);
	}

	CurrentBankRoll = FMath::FInterpTo(CurrentBankRoll, TargetBank + WaddleOffset, ClampedDt, TiltInterpSpeed);
	CurrentLeanPitch = FMath::FInterpTo(CurrentLeanPitch, TargetPitch, ClampedDt, TiltInterpSpeed);

	if (bApplyToOwnerMeshRotation && IsValid(TargetMesh))
	{
		const FQuat TiltQuat = FRotator(CurrentLeanPitch, 0.0f, CurrentBankRoll).Quaternion();
		TargetMesh->SetRelativeRotation(TiltQuat * BaseMeshRelQuat);
	}
}