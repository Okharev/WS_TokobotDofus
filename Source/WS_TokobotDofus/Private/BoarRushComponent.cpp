// BoarRushComponent.cpp
#include "BoarRushComponent.h"
#include "RushClashInterface.h"
#include "HitReactableInterface.h"
#include "HeartHealthComponent.h"
#include "HitFeedbackSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

UBoarRushComponent::UBoarRushComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBoarRushComponent::Initialize(const FBoarEnemyTuning* InTuning, USceneComponent* InRushOrigin)
{
	Tuning = InTuning;
	OwnerChar = Cast<ACharacter>(GetOwner());
	RushOrigin = InRushOrigin;
}

void UBoarRushComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveAction();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StunTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

float UBoarRushComponent::GetRushProgressAlpha() const
{
	if (!Tuning || Tuning->RushDistance <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return FMath::Clamp(DistanceTraveled / Tuning->RushDistance, 0.f, 1.f);
}

// ============================================================================
// PHASE 1: TELEGRAPH & TARGET TRACKING (CHARGE-UP)
// ============================================================================

bool UBoarRushComponent::StartChargeUp(AActor* Target)
{
	if (!Tuning || !OwnerChar.IsValid() || !IsValid(Target) || bIsStunnedVulnerable)
	{
		return false;
	}

	CancelActiveAction();

	ChargeTarget = Target;
	ChargeUpElapsed = 0.f;
	ChargeUpAlpha = 0.f;
	Phase = ERushPhase::ChargingUp;
	LockedRushDir = OwnerChar->GetActorForwardVector().GetSafeNormal2D();

	if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	// Optional telegraph montage (e.g., hoof pawing + snort)
	if (Tuning->ChargeUpMontage)
	{
		if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
			{
				AnimInst->Montage_Play(Tuning->ChargeUpMontage);
			}
		}
	}

	SetComponentTickEnabled(true);
	return true;
}

void UBoarRushComponent::TickChargeUp(float DeltaTime)
{
	if (!Tuning || !OwnerChar.IsValid())
	{
		CancelActiveAction();
		return;
	}

	ChargeUpElapsed += DeltaTime;
	ChargeUpAlpha = FMath::Clamp(ChargeUpElapsed / FMath::Max(Tuning->ChargeUpDuration, 0.01f), 0.f, 1.f);

	// Track the player until the final AimLockFreezeWindow, then lock heading for fair dodging / clash lining-up
	const float LockTime = FMath::Max(0.f, Tuning->ChargeUpDuration - Tuning->AimLockFreezeWindow);
	if (ChargeUpElapsed <= LockTime && ChargeTarget.IsValid())
	{
		const FVector ToTarget = (ChargeTarget->GetActorLocation() - OwnerChar->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurrentRot = OwnerChar->GetActorRotation();
			const FRotator DesiredRot(0.f, ToTarget.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpConstantTo(CurrentRot, DesiredRot, DeltaTime, Tuning->ChargeUpTurnRate);
			OwnerChar->SetActorRotation(NewRot);
			LockedRushDir = NewRot.Vector().GetSafeNormal2D();
		}
	}

	if (Tuning->bDrawDebugTraces && GetWorld())
	{
		const FVector Start = OwnerChar->GetActorLocation();
		const FColor LineColor = (ChargeUpElapsed > LockTime) ? FColor::Red : FColor::Yellow;
		DrawDebugLine(GetWorld(), Start, Start + LockedRushDir * Tuning->RushDistance, LineColor, false, -1.f, 0, 2.f);
	}

	if (ChargeUpElapsed >= Tuning->ChargeUpDuration)
	{
		Phase = ERushPhase::None;
		ChargeUpAlpha = 1.f;
		SetComponentTickEnabled(false);
		OnChargeUpCompleted.Broadcast();
	}
}

// ============================================================================
// PHASE 2: FIXED-DISTANCE RUSH EXECUTION & HITBOX SWEEP
// ============================================================================

bool UBoarRushComponent::ExecuteRush()
{
	if (!Tuning || !OwnerChar.IsValid() || bIsStunnedVulnerable)
	{
		return false;
	}

	// Stop ChargeUp montage cleanly if still blending out
	if (Tuning->ChargeUpMontage)
	{
		if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
			{
				if (AnimInst->Montage_IsPlaying(Tuning->ChargeUpMontage))
				{
					AnimInst->Montage_Stop(0.1f, Tuning->ChargeUpMontage);
				}
			}
		}
	}

	Phase = ERushPhase::Rushing;
	DistanceTraveled = 0.f;
	PrevRushLocation = OwnerChar->GetActorLocation();
	HitActorsThisRush.Reset();

	if (LockedRushDir.IsNearlyZero())
	{
		LockedRushDir = OwnerChar->GetActorForwardVector().GetSafeNormal2D();
	}
	OwnerChar->SetActorRotation(LockedRushDir.Rotation());

	if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
	{
		SavedMaxWalkSpeed = MoveComp->MaxWalkSpeed;
		SavedMaxAcceleration = MoveComp->MaxAcceleration;
		bSavedOrientToMovement = MoveComp->bOrientRotationToMovement;

		MoveComp->bOrientRotationToMovement = false;
		MoveComp->MaxWalkSpeed = Tuning->RushSpeed;
		MoveComp->MaxAcceleration = 14000.f; // Snappy burst off the line
	}

	if (UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
	{
		Capsule->OnComponentHit.AddUniqueDynamic(this, &UBoarRushComponent::HandleCapsuleHit);
	}

	SetComponentTickEnabled(true);
	return true;
}

void UBoarRushComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Phase == ERushPhase::ChargingUp)
	{
		TickChargeUp(DeltaTime);
	}
	else if (Phase == ERushPhase::Rushing)
	{
		TickRush(DeltaTime);
	}
	else
	{
		SetComponentTickEnabled(false);
	}
}

void UBoarRushComponent::TickRush(float DeltaTime)
{
	if (!Tuning || !OwnerChar.IsValid())
	{
		CancelActiveAction();
		return;
	}

	// Drive rush through CMC so slopes, step height, and static wall blocking behave naturally
	OwnerChar->AddMovementInput(LockedRushDir, 1.f, true);

	const FVector CurrentLoc = OwnerChar->GetActorLocation();
	DistanceTraveled += FVector::Dist2D(PrevRushLocation, CurrentLoc);
	PrevRushLocation = CurrentLoc;

	SweepRushHitbox();

	// SweepRushHitbox may have resolved a clash and exited Rushing
	if (Phase != ERushPhase::Rushing)
	{
		return;
	}

	if (DistanceTraveled >= Tuning->RushDistance)
	{
		StopRushInternal(true);
	}
}

void UBoarRushComponent::SweepRushHitbox()
{
	UWorld* World = GetWorld();
	if (!World || !Tuning || !OwnerChar.IsValid())
	{
		return;
	}

	const FVector Start = RushOrigin.IsValid()
		? RushOrigin->GetComponentLocation()
		: OwnerChar->GetActorLocation() + (LockedRushDir * 55.f);
	const FVector End = Start + (LockedRushDir * Tuning->RushHitForwardLength);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BoarRushSweep), false, OwnerChar.Get());
	TArray<FHitResult> HitResults;

	World->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Tuning->RushHitRadius),
		QueryParams);

	if (Tuning->bDrawDebugTraces)
	{
		const FColor TraceColor = HitResults.IsEmpty() ? FColor::Orange : FColor::Red;
		DrawDebugSphere(World, End, Tuning->RushHitRadius, 12, TraceColor, false, 0.05f);
	}

	// Check HitResults.IsEmpty(), not return bool, so ECC_Pawn overlaps register reliably
	if (HitResults.IsEmpty())
	{
		return;
	}

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActorsThisRush.Contains(HitActor))
		{
			continue;
		}

		if (!Tuning->TargetTag.IsNone() && !HitActor->ActorHasTag(Tuning->TargetTag))
		{
			continue;
		}

		// Priority 1: Check head-on Carry Rush clash BEFORE applying damage to the player
		if (TryResolveRushClash(HitActor))
		{
			return;
		}

		// Priority 2: Standard rush impact (damage + knockback)
		HitActorsThisRush.Add(HitActor);
		ApplyRushHitToTarget(HitActor, Hit);
	}
}

// ============================================================================
// CLASH RESOLUTION & DAMAGE APPLICATION
// ============================================================================

bool UBoarRushComponent::CanClashWithActor(AActor* Candidate) const
{
	auto LogReject = [this, Candidate](const FString& Reason) -> bool
	{
		if (Tuning && Tuning->bDrawDebugTraces)
		{
			const FString Msg = FString::Printf(TEXT("[BoarClash REJECT] %s vs %s -> %s"),
				*GetNameSafe(GetOwner()), *GetNameSafe(Candidate), *Reason);
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, Msg);
			}
		}
		return false;
	};

	if (!Tuning)
	{
		return false;
	}
	if (Phase != ERushPhase::Rushing)
	{
		return LogReject(FString::Printf(TEXT("Boar not Rushing (Phase=%d)"), static_cast<int32>(Phase)));
	}

	UObject* ClashObj = ResolveRushClashObject(Candidate);
	if (!ClashObj)
	{
		return LogReject(TEXT("No IRushClashInterface found on Actor or Components (Cold-rebuild if you used Live Coding!)"));
	}

	if (!IRushClashInterface::Execute_IsCarryRushing(ClashObj))
	{
		return LogReject(FString::Printf(TEXT("IsCarryRushing() == false on [%s]"), *GetNameSafe(ClashObj)));
	}

	const float PlayerSpeed = IRushClashInterface::Execute_GetCarryRushSpeed(ClashObj);
	if (PlayerSpeed < Tuning->MinPlayerClashSpeed)
	{
		return LogReject(FString::Printf(TEXT("Speed too low: %.1f < MinPlayerClashSpeed %.1f (cm/s)"),
			PlayerSpeed, Tuning->MinPlayerClashSpeed));
	}

	FVector PlayerRushDir = IRushClashInterface::Execute_GetCarryRushDirection(ClashObj).GetSafeNormal2D();
	if (PlayerRushDir.IsNearlyZero())
	{
		PlayerRushDir = Candidate->GetVelocity().GetSafeNormal2D();
	}
	if (PlayerRushDir.IsNearlyZero())
	{
		return LogReject(TEXT("PlayerRushDir is zero"));
	}

	const float HeadOnDot = FVector::DotProduct(LockedRushDir, PlayerRushDir);
	if (HeadOnDot > -Tuning->MinClashHeadOnDot)
	{
		return LogReject(FString::Printf(TEXT("Not head-on enough: Dot=%.2f (must be <= %.2f)"),
			HeadOnDot, -Tuning->MinClashHeadOnDot));
	}

	return true;
}

bool UBoarRushComponent::TryResolveRushClash(AActor* Candidate)
{
	UObject* ClashObj = ResolveRushClashObject(Candidate);
	if (!ClashObj || !CanClashWithActor(Candidate) || !OwnerChar.IsValid() || !Tuning)
	{
		return false;
	}

	// 1. Exit Rushing immediately (prevents double-trigger if both sweeps fire on the same frame)
	StopRushInternal(false);

	// 2. Mutual separation recoil
	FVector AwayFromPlayer = (OwnerChar->GetActorLocation() - Candidate->GetActorLocation()).GetSafeNormal2D();
	if (AwayFromPlayer.IsNearlyZero())
	{
		AwayFromPlayer = -LockedRushDir;
	}

	const FVector BoarRecoil = (AwayFromPlayer * Tuning->ClashRecoilSpeed) + FVector(0.f, 0.f, 180.f);
	const FVector PlayerRecoil = (-AwayFromPlayer * Tuning->ClashRecoilSpeed) + FVector(0.f, 0.f, 160.f);

	OwnerChar->LaunchCharacter(BoarRecoil, true, true);

	// 3. Cancel player's Carry Rush via UMinionSquadComponent::OnRushClashed_Implementation
	IRushClashInterface::Execute_OnRushClashed(ClashObj, OwnerChar.Get(), PlayerRecoil);

	// 4. Trigger BOTW clash HitStop, camera shake & VFX
	if (UWorld* World = GetWorld())
	{
		if (UHitFeedbackSubsystem* FeedbackSub = World->GetSubsystem<UHitFeedbackSubsystem>())
		{
			FCombatHitPayload ClashPayload;
			ClashPayload.InstigatorActor = Candidate;
			ClashPayload.DamageCauser = Candidate;
			ClashPayload.TargetActor = OwnerChar.Get();
			ClashPayload.ImpactPoint = (OwnerChar->GetActorLocation() + Candidate->GetActorLocation()) * 0.5f;
			ClashPayload.ImpactNormal = AwayFromPlayer;
			ClashPayload.KnockbackDirection = AwayFromPlayer;
			ClashPayload.KnockbackVelocity = BoarRecoil;
			ClashPayload.HeartDamage = 0;
			ClashPayload.AppliedHitStopDuration = Tuning->ClashFeedback.HitStopDuration;

			FeedbackSub->TriggerCombatFeedback(ClashPayload, Tuning->ClashFeedback);
		}
	}

	if (Tuning->bDrawDebugTraces && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 2.5f, FColor::Cyan,
			FString::Printf(TEXT("[BoarClash] Clashed with %s! Stunned for %.1fs (Spin Attack vulnerable)"),
				*GetNameSafe(Candidate), Tuning->ClashStunDuration));
	}

	OnRushClashed.Broadcast(Candidate);
	EnterStunnedVulnerable(Tuning->ClashStunDuration);
	return true;
}

void UBoarRushComponent::ApplyRushHitToTarget(AActor* TargetActor, const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	if (!World || !Tuning || !OwnerChar.IsValid())
	{
		return;
	}

	const FHitFeedbackConfig& FeedbackCfg = Tuning->RushHitFeedback;

	FVector KnockDir = (TargetActor->GetActorLocation() - OwnerChar->GetActorLocation()).GetSafeNormal2D();
	if (KnockDir.IsNearlyZero())
	{
		KnockDir = LockedRushDir;
	}

	const FVector KnockbackVel =
		(KnockDir * FeedbackCfg.KnockbackHorizontalStrength) +
		FVector(0.f, 0.f, FeedbackCfg.KnockbackVerticalStrength);

	FCombatHitPayload Payload;
	Payload.InstigatorActor = OwnerChar.Get();
	Payload.DamageCauser = OwnerChar.Get();
	Payload.TargetActor = TargetActor;
	if (Hit.ImpactPoint.IsNearlyZero())
	{
		Payload.ImpactPoint = TargetActor->GetActorLocation();
	}
	else
	{
		Payload.ImpactPoint = Hit.ImpactPoint;
	}
	Payload.ImpactNormal = Hit.ImpactNormal;
	Payload.KnockbackDirection = KnockDir;
	Payload.KnockbackVelocity = KnockbackVel;
	Payload.HeartDamage = FMath::Max(1, FeedbackCfg.HeartDamage);
	Payload.StaggerDuration = FeedbackCfg.StaggerDuration;
	Payload.AppliedHitStopDuration = FeedbackCfg.HitStopDuration;
	Payload.HitResult = Hit;

	FCombatHitResponse HitResponse;
	bool bHandledByHealthComp = false;

	if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(TargetActor))
	{
		HitResponse = Reactable->ReceiveCombatHit(Payload);
		bHandledByHealthComp = true;
	}
	else if (UHeartHealthComponent* TargetHealth = TargetActor->FindComponentByClass<UHeartHealthComponent>())
	{
		HitResponse = TargetHealth->ApplyCombatHit(Payload);
		bHandledByHealthComp = true;
	}

	// Respect Player i-frames
	if (bHandledByHealthComp && !HitResponse.bHitAccepted)
	{
		return;
	}

	if (bHandledByHealthComp)
	{
		Payload.RemainingHearts = HitResponse.RemainingHearts;
		Payload.MaxHearts = HitResponse.MaxHearts;
		Payload.bIsKillingBlow = HitResponse.bWasKillingBlow;
		Payload.AppliedHitStopDuration = HitResponse.bWasKillingBlow
			? FeedbackCfg.KillHitStopDuration
			: FeedbackCfg.HitStopDuration;

		if (UHitFeedbackSubsystem* FeedbackSub = World->GetSubsystem<UHitFeedbackSubsystem>())
		{
			FeedbackSub->TriggerCombatFeedback(Payload, FeedbackCfg);
		}
	}

	const float FinalDamage = bHandledByHealthComp
		? static_cast<float>(Payload.HeartDamage)
		: Tuning->BaseRushDamage;

	UGameplayStatics::ApplyDamage(
		TargetActor, FinalDamage, OwnerChar->GetController(), OwnerChar.Get(), UDamageType::StaticClass());

	OnRushHitPlayer.Broadcast(TargetActor, FinalDamage);
}

void UBoarRushComponent::HandleCapsuleHit(
	UPrimitiveComponent* HitComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (Phase != ERushPhase::Rushing || !OtherComp)
	{
		return;
	}

	// Stop rush cleanly if the Boar slams head-on into a steep static wall
	const bool bIsSteepWall = FMath::Abs(Hit.ImpactNormal.Z) < 0.5f;
	const bool bHeadOnWall = FVector::DotProduct(Hit.ImpactNormal, LockedRushDir) < -0.55f;

	if (bIsSteepWall && bHeadOnWall && OtherComp->GetCollisionObjectType() == ECC_WorldStatic)
	{
		StopRushInternal(true);
	}
}

void UBoarRushComponent::CancelActiveAction()
{
	if (Phase == ERushPhase::Rushing)
	{
		StopRushInternal(false);
	}
	else
	{
		Phase = ERushPhase::None;
		ChargeUpAlpha = 0.f;
		SetComponentTickEnabled(false);

		if (Tuning && Tuning->ChargeUpMontage && OwnerChar.IsValid())
		{
			if (USkeletalMeshComponent* Mesh = OwnerChar->GetMesh())
			{
				if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
				{
					if (AnimInst->Montage_IsPlaying(Tuning->ChargeUpMontage))
					{
						AnimInst->Montage_Stop(0.15f, Tuning->ChargeUpMontage);
					}
				}
			}
		}
	}
}

void UBoarRushComponent::StopRushInternal(bool bBroadcastFinished)
{
	Phase = ERushPhase::None;
	SetComponentTickEnabled(false);

	if (OwnerChar.IsValid())
	{
		if (UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent())
		{
			Capsule->OnComponentHit.RemoveDynamic(this, &UBoarRushComponent::HandleCapsuleHit);
		}
		if (UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement())
		{
			MoveComp->MaxWalkSpeed = SavedMaxWalkSpeed;
			MoveComp->MaxAcceleration = SavedMaxAcceleration;
			MoveComp->bOrientRotationToMovement = bSavedOrientToMovement;
			MoveComp->StopActiveMovement();
		}
	}

	if (bBroadcastFinished)
	{
		OnRushFinished.Broadcast();
	}
}

UObject* UBoarRushComponent::ResolveRushClashObject(AActor* Candidate) const
{
	if (!IsValid(Candidate))
	{
		return nullptr;
	}

	// 1. Prefer a component implementing IRushClashInterface (e.g., UMinionSquadComponent)
	//    so an unimplemented Blueprint interface on the Character can never shadow it.
	if (UActorComponent* ClashComp = Candidate->FindComponentByInterface(URushClashInterface::StaticClass()))
	{
		return ClashComp;
	}

	// 2. Fallback to the Actor itself if no component implements it.
	if (Candidate->Implements<URushClashInterface>())
	{
		return Candidate;
	}

	return nullptr;
}

// ============================================================================
// STUNNED VULNERABILITY WINDOW (POST-CLASH)
// ============================================================================

void UBoarRushComponent::EnterStunnedVulnerable(float DurationOverride)
{
	UWorld* World = GetWorld();
	if (!World || !Tuning)
	{
		return;
	}

	CancelActiveAction();

	const float Duration = (DurationOverride > 0.f) ? DurationOverride : Tuning->ClashStunDuration;
	const bool bWasStunned = bIsStunnedVulnerable;
	bIsStunnedVulnerable = true;

	World->GetTimerManager().SetTimer(
		StunTimerHandle,
		this,
		&UBoarRushComponent::EndStunnedVulnerable,
		Duration,
		false);

	if (!bWasStunned)
	{
		OnStunVulnerabilityChanged.Broadcast(true);
	}
}

void UBoarRushComponent::EndStunnedVulnerable()
{
	if (!bIsStunnedVulnerable)
	{
		return;
	}

	bIsStunnedVulnerable = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StunTimerHandle);
	}

	OnStunVulnerabilityChanged.Broadcast(false);
}