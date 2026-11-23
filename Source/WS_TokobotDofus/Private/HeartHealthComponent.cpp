// HeartHealthComponent.cpp
#include "HeartHealthComponent.h"
#include "HitFeedbackSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"

UHeartHealthComponent::UHeartHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UHeartHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentHearts = FMath::Max(MaxHearts, 1);
	bIsDead = false;

	if (UWorld* World = GetWorld())
	{
		if (UHitFeedbackSubsystem* FeedbackSub = World->GetSubsystem<UHitFeedbackSubsystem>())
		{
			FeedbackSub->OnHitStopEnded.AddUniqueDynamic(this, &UHeartHealthComponent::HandleHitStopEnded);
		}
	}
}

void UHeartHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StaggerTimerHandle);
		if (UHitFeedbackSubsystem* FeedbackSub = World->GetSubsystem<UHitFeedbackSubsystem>())
		{
			FeedbackSub->OnHitStopEnded.RemoveDynamic(this, &UHeartHealthComponent::HandleHitStopEnded);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FCombatHitResponse UHeartHealthComponent::ApplyCombatHit(const FCombatHitPayload& IncomingPayload)
{
	FCombatHitResponse Response;
	Response.MaxHearts = MaxHearts;
	Response.RemainingHearts = CurrentHearts;

	UWorld* World = GetWorld();
	if (!World || bIsDead)
	{
		return Response;
	}

	const double NowGameTime = World->GetTimeSeconds();
	if ((NowGameTime - LastHitWorldTime) < static_cast<double>(InvulnerabilityDuration))
	{
		return Response;
	}
	LastHitWorldTime = NowGameTime;

	// 1. Subtract Hearts (default 1 heart)
	const int32 DamageHearts = FMath::Max(IncomingPayload.HeartDamage, 1);
	const int32 OldHearts = CurrentHearts;
	CurrentHearts = FMath::Max(0, CurrentHearts - DamageHearts);
	const bool bKillingBlow = (CurrentHearts <= 0);
	bIsDead = bKillingBlow;

	Response.bHitAccepted = true;
	Response.bWasKillingBlow = bKillingBlow;
	Response.RemainingHearts = CurrentHearts;

	// Enrich payload copy for local listeners
	FCombatHitPayload EnrichedPayload = IncomingPayload;
	EnrichedPayload.RemainingHearts = CurrentHearts;
	EnrichedPayload.MaxHearts = MaxHearts;
	EnrichedPayload.bIsKillingBlow = bKillingBlow;

	// 2. Start BOTW HitStop Mesh Vibration & Emissive Hit Flash (unscaled real time)
	const double NowReal = World->GetRealTimeSeconds();
	USkeletalMeshComponent* OwnerMesh = ResolveOwnerMesh();

	if (HitStopMeshShakeAmplitude > KINDA_SMALL_NUMBER && IncomingPayload.AppliedHitStopDuration > KINDA_SMALL_NUMBER)
	{
		if (bAutoApplyMeshShake && OwnerMesh && !bIsMeshShaking)
		{
			CachedMeshBaseRelLoc = OwnerMesh->GetRelativeLocation();
		}
		bIsMeshShaking = true;
		ShakeEndRealTime = NowReal + static_cast<double>(IncomingPayload.AppliedHitStopDuration);
	}

	if (HitFlashDuration > KINDA_SMALL_NUMBER && OwnerMesh && !HitFlashMaterialParam.IsNone())
	{
		bIsFlashing = true;
		FlashEndRealTime = NowReal + static_cast<double>(HitFlashDuration);
		OwnerMesh->SetScalarParameterValueOnMaterials(HitFlashMaterialParam, 1.0f);
	}

	if (bIsMeshShaking || bIsFlashing)
	{
		SetComponentTickEnabled(true);
	}

	// 3. Queue Knockback (releases right when HitStop ends, or immediately if no HitStop)
	const FVector ScaledKnockback = IncomingPayload.KnockbackVelocity * KnockbackResistanceScale;
	if (!ScaledKnockback.IsNearlyZero())
	{
		PendingKnockbackVelocity = ScaledKnockback;
		bHasPendingKnockback = true;

		if (IncomingPayload.AppliedHitStopDuration <= KINDA_SMALL_NUMBER)
		{
			ReleasePendingKnockback();
		}
	}

	// 4. Stagger State
	if (!bKillingBlow && IncomingPayload.StaggerDuration > KINDA_SMALL_NUMBER)
	{
		if (!bIsStaggered)
		{
			bIsStaggered = true;
			OnStaggerChanged.Broadcast(true);
		}
		World->GetTimerManager().SetTimer(
			StaggerTimerHandle,
			this,
			&UHeartHealthComponent::EndStagger,
			IncomingPayload.StaggerDuration,
			false);
	}

	// 5. Broadcast Designer Events
	OnHeartsChanged.Broadcast(CurrentHearts, MaxHearts, CurrentHearts - OldHearts);
	OnHitReceived.Broadcast(EnrichedPayload);

	if (bKillingBlow)
	{
		World->GetTimerManager().ClearTimer(StaggerTimerHandle);
		OnDeath.Broadcast(EnrichedPayload);
	}

	return Response;
}

void UHeartHealthComponent::HealHearts(int32 Amount)
{
	if (bIsDead || Amount <= 0)
	{
		return;
	}

	const int32 OldHearts = CurrentHearts;
	CurrentHearts = FMath::Clamp(CurrentHearts + Amount, 0, MaxHearts);
	if (CurrentHearts != OldHearts)
	{
		OnHeartsChanged.Broadcast(CurrentHearts, MaxHearts, CurrentHearts - OldHearts);
	}
}

void UHeartHealthComponent::HandleHitStopEnded()
{
	ReleasePendingKnockback();
}

void UHeartHealthComponent::ReleasePendingKnockback()
{
	if (!bHasPendingKnockback)
	{
		return;
	}

	bHasPendingKnockback = false;
	const FVector LaunchVel = PendingKnockbackVelocity;
	PendingKnockbackVelocity = FVector::ZeroVector;

	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			MoveComp->StopActiveMovement();
		}
		OwnerChar->LaunchCharacter(LaunchVel, true, true);
	}
}

void UHeartHealthComponent::EndStagger()
{
	if (bIsStaggered)
	{
		bIsStaggered = false;
		OnStaggerChanged.Broadcast(false);
	}
}

void UHeartHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World)
	{
		SetComponentTickEnabled(false);
		return;
	}

	const double NowReal = World->GetRealTimeSeconds();
	USkeletalMeshComponent* OwnerMesh = ResolveOwnerMesh();

	// High-frequency local mesh shake during HitStop
	if (bIsMeshShaking)
	{
		if (NowReal < ShakeEndRealTime)
		{
			const float Phase = static_cast<float>(NowReal * 110.0);
			CurrentMeshShakeOffset = FVector(
				FMath::Sin(Phase) * HitStopMeshShakeAmplitude,
				FMath::Cos(Phase * 1.37f) * HitStopMeshShakeAmplitude,
				0.0f);

			if (bAutoApplyMeshShake && OwnerMesh)
			{
				OwnerMesh->SetRelativeLocation(CachedMeshBaseRelLoc + CurrentMeshShakeOffset);
			}
		}
		else
		{
			bIsMeshShaking = false;
			CurrentMeshShakeOffset = FVector::ZeroVector;
			if (bAutoApplyMeshShake && OwnerMesh)
			{
				OwnerMesh->SetRelativeLocation(CachedMeshBaseRelLoc);
			}
		}
	}

	// Emissive Hit Flash smooth decay
	if (bIsFlashing)
	{
		const float Remaining = static_cast<float>(FlashEndRealTime - NowReal);
		const float Alpha = (HitFlashDuration > KINDA_SMALL_NUMBER)
			? FMath::Clamp(Remaining / HitFlashDuration, 0.0f, 1.0f)
			: 0.0f;

		if (OwnerMesh && !HitFlashMaterialParam.IsNone())
		{
			OwnerMesh->SetScalarParameterValueOnMaterials(HitFlashMaterialParam, Alpha);
		}

		if (Remaining <= 0.0f)
		{
			bIsFlashing = false;
		}
	}

	if (!bIsMeshShaking && !bIsFlashing)
	{
		SetComponentTickEnabled(false);
	}
}

USkeletalMeshComponent* UHeartHealthComponent::ResolveOwnerMesh() const
{
	if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		return OwnerChar->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}