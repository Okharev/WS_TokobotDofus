// BirdEnemyCharacter.cpp
#include "BirdEnemyCharacter.h"
#include "BirdAIController.h"
#include "HeartHealthComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "HitFeedbackSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

// ============================================================================
// LIFECYCLE & COMPONENT SETUP
// ============================================================================

class UHitFeedbackSubsystem;

void ABirdEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearFallbackAttackTimers();
	Super::EndPlay(EndPlayReason);
}

ABirdEnemyCharacter::ABirdEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 1. Grounded navigation-only capsule: stays on the NavMesh for free pathfinding & avoidance.
	//    Visibility/Camera are ignored so player traces pass through empty air beneath the bird.
	//    ECC_Pawn is set to Overlap so minions/players don't snag under the bird while low-sweeping
	//    whip attacks can still connect (switch ECC_Pawn to ECR_Ignore if HoverHeight is lowered to ~85cm).
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(25.f, 40.f);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	// 2. Inherited SkeletalMesh acts as the elevated visual parent (UE5 blocks reparenting it under BP components).
	//    Standard -90 deg Yaw aligns the bird's nose with the Actor's forward vector (+X).
	if (USkeletalMeshComponent* BirdMesh = GetMesh())
	{
		BirdMesh->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
		BirdMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		BirdMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 3. Elevated hurtbox attached to Mesh so it automatically hovers, bobs, and shakes with the bird.
	HurtboxSphere = CreateDefaultSubobject<USphereComponent>(TEXT("HurtboxSphere"));
	HurtboxSphere->SetupAttachment(GetMesh());
	HurtboxSphere->InitSphereRadius(30.f);
	HurtboxSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HurtboxSphere->SetCollisionObjectType(ECC_Pawn);
	HurtboxSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	HurtboxSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HurtboxSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// 4. Beak/talon strike origin (Mesh is rotated -90 Yaw, so local +Y points along Actor Forward).
	AttackOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("AttackOrigin"));
	AttackOrigin->SetupAttachment(GetMesh());
	AttackOrigin->SetRelativeLocation(FVector(0.f, 45.f, 0.f));

	// 5. Heart-based HP, HitStop shake, hit flash, knockback release, and stagger management.
	//    Disable auto mesh-shake because UpdateHoverAndAnimTelemetry writes Mesh RelativeLocation every tick.
	HealthComp = CreateDefaultSubobject<UHeartHealthComponent>(TEXT("HealthComp"));
	HealthComp->SetAutoApplyMeshShake(false);

	// 6. Smooth, momentum-driven ground movement tuned to feel airborne.
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.f, 260.f, 0.f);
		MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
		MoveComp->MaxAcceleration = 600.f;
		MoveComp->BrakingDecelerationWalking = 400.f;
		MoveComp->MaxStepHeight = 60.f;
	}

	// 7. Elevate AI viewpoint & default AI controller ownership.
	BaseEyeHeight = Tuning.HoverHeight;
	AIControllerClass = ABirdAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ABirdEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Apply designer-edited Blueprint Tuning values on spawn.
	BaseEyeHeight = Tuning.HoverHeight;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
	}

	SmoothedHoverWorldZ = GetActorLocation().Z + Tuning.HoverHeight;
	LastActorYaw = GetActorRotation().Yaw;
	LastBobOffset = 0.f;

	if (USkeletalMeshComponent* BirdMesh = GetMesh())
	{
		BirdMesh->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
	}

	// Bind combat feedback lifecycle events from UHeartHealthComponent.
	if (HealthComp)
	{
		HealthComp->OnStaggerChanged.AddUniqueDynamic(this, &ABirdEnemyCharacter::HandleStaggerChanged);
		HealthComp->OnDeath.AddUniqueDynamic(this, &ABirdEnemyCharacter::HandleDeath);
	}
}

void ABirdEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateHoverAndAnimTelemetry(DeltaTime);
}

void ABirdEnemyCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	// Originates AIPerception sight traces from the hovering bird's elevation instead of the grounded capsule,
	// preventing low waist-high cover from falsely blinding an airborne enemy.
	OutLocation = GetActorLocation() + FVector(0.f, 0.f, Tuning.HoverHeight);
	OutRotation = GetActorRotation();
}

#if WITH_EDITOR
void ABirdEnemyCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Live-preview HoverHeight tweaks immediately in the Blueprint & Level Viewports.
	if (USkeletalMeshComponent* BirdMesh = GetMesh())
	{
		BirdMesh->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
	}
	BaseEyeHeight = Tuning.HoverHeight;
}
#endif

// ============================================================================
// HOVER SMOOTHING & ANIMATION TELEMETRY
// ============================================================================

void ABirdEnemyCharacter::UpdateHoverAndAnimTelemetry(float DeltaTime)
{
	UWorld* World = GetWorld();
	USkeletalMeshComponent* BirdMesh = GetMesh();
	if (!World || !BirdMesh)
	{
		return;
	}

	// Smooth World-Z toward (CapsuleZ + HoverHeight) so ramps and stairs don't cause rigid vertical snapping.
	const FVector ActorLoc = GetActorLocation();
	const float TargetWorldZ = ActorLoc.Z + Tuning.HoverHeight;
	SmoothedHoverWorldZ = FMath::FInterpTo(SmoothedHoverWorldZ, TargetWorldZ, DeltaTime, Tuning.HoverHeightInterpSpeed);

	// Layer subtle sinusoidal hover bob + BOTW HitStop vibration offset from HealthComp.
	const float BobOffset = FMath::Sin(World->GetTimeSeconds() * Tuning.BobFrequency) * Tuning.BobAmplitude;
	const float RelativeZ = (SmoothedHoverWorldZ + BobOffset) - ActorLoc.Z;
	const FVector ShakeOffset = HealthComp ? HealthComp->GetCurrentMeshShakeOffset() : FVector::ZeroVector;

	BirdMesh->SetRelativeLocation(FVector(ShakeOffset.X, ShakeOffset.Y, RelativeZ + ShakeOffset.Z));

	// Update thread-safe animation telemetry struct read by the AnimBP (BlendSpaces & Banking).
	const float GroundSpeed = GetVelocity().Size2D();
	const float SafeMaxSpeed = FMath::Max(Tuning.ChaseSpeed, 1.f);

	AnimState.GroundSpeed = GroundSpeed;
	AnimState.NormalizedSpeed = FMath::Clamp(GroundSpeed / SafeMaxSpeed, 0.f, 1.f);
	AnimState.BobVelocity = (DeltaTime > KINDA_SMALL_NUMBER) ? (BobOffset - LastBobOffset) / DeltaTime : 0.f;
	LastBobOffset = BobOffset;

	const float CurrentYaw = GetActorRotation().Yaw;
	const float YawDelta = FMath::FindDeltaAngleDegrees(LastActorYaw, CurrentYaw);
	const float YawRate = (DeltaTime > KINDA_SMALL_NUMBER) ? (YawDelta / DeltaTime) : 0.f;
	LastActorYaw = CurrentYaw;

	const float TargetBank = FMath::Clamp(
		(YawRate / 260.f) * Tuning.MaxBankAngle * AnimState.NormalizedSpeed,
		-Tuning.MaxBankAngle,
		Tuning.MaxBankAngle);
	AnimState.BankAngle = FMath::FInterpTo(AnimState.BankAngle, TargetBank, DeltaTime, 8.f);

	// Visual collision & Z-height debugs (enable via BP_BirdEnemy -> Tuning -> bDrawDebugTraces)
	if (Tuning.bDrawDebugTraces)
	{
		if (const UCapsuleComponent* Cap = GetCapsuleComponent())
		{
			DrawDebugCapsule(
				World,
				Cap->GetComponentLocation(),
				Cap->GetScaledCapsuleHalfHeight(),
				Cap->GetScaledCapsuleRadius(),
				Cap->GetComponentQuat(),
				FColor::Blue,
				false,
				-1.0f,
				0,
				1.5f);
		}

		if (HurtboxSphere)
		{
			DrawDebugSphere(
				World,
				HurtboxSphere->GetComponentLocation(),
				HurtboxSphere->GetScaledSphereRadius(),
				12,
				FColor::Magenta,
				false,
				-1.0f,
				0,
				1.5f);
		}

		const int32 CurHearts = HealthComp ? HealthComp->GetCurrentHearts() : -1;
		const int32 MaxH = HealthComp ? HealthComp->GetMaxHearts() : -1;
		const bool bStaggered = HealthComp ? HealthComp->IsStaggered() : false;

		DrawDebugString(
			World,
			BirdMesh->GetComponentLocation() + FVector(0.f, 0.f, 45.f),
			FString::Printf(TEXT("Hearts: %d/%d | Stagger:%d | CapsuleZ:%.0f | HurtboxZ:%.0f"),
				CurHearts,
				MaxH,
				bStaggered ? 1 : 0,
				GetActorLocation().Z,
				HurtboxSphere ? HurtboxSphere->GetComponentLocation().Z : 0.f),
			nullptr,
			FColor::White,
			0.0f,
			true);
	}
}

// ============================================================================
// AI STATE & AGGRO TRANSITIONS (Driven by ABirdAIController / StateTree)
// ============================================================================

void ABirdEnemyCharacter::SetAggroState(bool bNewAggroed, AActor* NewTarget)
{
	if (AnimState.bIsAggroed == bNewAggroed)
	{
		return;
	}

	AnimState.bIsAggroed = bNewAggroed;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = bNewAggroed ? Tuning.ChaseSpeed : Tuning.PatrolSpeed;
	}

	OnAggroChanged.Broadcast(bNewAggroed, NewTarget);
	BP_OnAggroChanged(bNewAggroed, NewTarget);
}

void ABirdEnemyCharacter::SetAIState(EBirdAIState NewState)
{
	if (AnimState.CurrentAIState == NewState)
	{
		return;
	}

	const EBirdAIState OldState = AnimState.CurrentAIState;
	AnimState.CurrentAIState = NewState;

	OnAIStateChanged.Broadcast(OldState, NewState);
	BP_OnAIStateChanged(OldState, NewState);
}


// ============================================================================
// OFFENSIVE ATTACK MONTAGE & HITBOX SWEEP (Driven by StateTree & AnimNotifyState)
// ============================================================================

bool ABirdEnemyCharacter::StartAttack(AActor* TargetActor)
{
	// Refuse to start attacks while staggered or dead.
	if (HealthComp && (!HealthComp->IsAlive() || HealthComp->IsStaggered()))
	{
		return false;
	}

	// Path A: Montage-driven attack (uses UANS_BirdAttackHitbox NotifyState).
	if (Tuning.AttackMontage)
	{
		UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		if (!AnimInstance)
		{
			return false;
		}

		const float Duration = AnimInstance->Montage_Play(Tuning.AttackMontage);
		if (Duration <= 0.f)
		{
			return false;
		}

		AnimState.bIsAttacking = true;

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &ABirdEnemyCharacter::HandleAttackMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, Tuning.AttackMontage);

		OnAttackStarted.Broadcast(TargetActor);
		return true;
	}

	// Path B: Timer-driven fallback when prototyping without an AnimMontage.
	StartFallbackAttack();
	OnAttackStarted.Broadcast(TargetActor);
	return true;
}

void ABirdEnemyCharacter::CancelAttack()
{
	ClearFallbackAttackTimers();
	EndAttackHitWindow();
	AnimState.bIsAttacking = false;

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (Tuning.AttackMontage && AnimInstance->Montage_IsPlaying(Tuning.AttackMontage))
		{
			AnimInstance->Montage_Stop(0.15f, Tuning.AttackMontage);
		}
	}
}

void ABirdEnemyCharacter::StartFallbackAttack()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearFallbackAttackTimers();
	AnimState.bIsAttacking = true;

	if (Tuning.bDrawDebugTraces && AttackOrigin)
	{
		// Cyan sphere telegraphs the wind-up before the strike fires
		DrawDebugSphere(World, AttackOrigin->GetComponentLocation(), 18.f, 8, FColor::Cyan, false, Tuning.FallbackWindupDuration, 0, 1.5f);
	}

	if (Tuning.FallbackWindupDuration <= KINDA_SMALL_NUMBER)
	{
		HandleFallbackStrikeBegin();
		return;
	}

	World->GetTimerManager().SetTimer(
		FallbackWindupTimerHandle,
		this,
		&ABirdEnemyCharacter::HandleFallbackStrikeBegin,
		Tuning.FallbackWindupDuration,
		false);
}

void ABirdEnemyCharacter::HandleFallbackStrikeBegin()
{
	UWorld* World = GetWorld();
	if (!World || !AnimState.bIsAttacking)
	{
		return;
	}

	BeginAttackHitWindow(1.0f);
	PerformAttackHitTrace(); // Immediate frame-0 sweep

	// Poll at ~33Hz during the brief active window to mirror AnimNotifyState::NotifyTick
	World->GetTimerManager().SetTimer(
		FallbackStrikeTickTimerHandle,
		this,
		&ABirdEnemyCharacter::HandleFallbackStrikeTick,
		0.03f,
		true);

	World->GetTimerManager().SetTimer(
		FallbackEndTimerHandle,
		this,
		&ABirdEnemyCharacter::HandleFallbackAttackEnd,
		FMath::Max(Tuning.FallbackActiveDuration, 0.05f),
		false);
}

void ABirdEnemyCharacter::HandleFallbackStrikeTick()
{
	PerformAttackHitTrace();
}

void ABirdEnemyCharacter::HandleFallbackAttackEnd()
{
	ClearFallbackAttackTimers();
	EndAttackHitWindow();
	AnimState.bIsAttacking = false; // Signals FBirdAttackTask::Tick to start AttackCooldown
}

void ABirdEnemyCharacter::ClearFallbackAttackTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(FallbackWindupTimerHandle);
		TimerManager.ClearTimer(FallbackStrikeTickTimerHandle);
		TimerManager.ClearTimer(FallbackEndTimerHandle);
	}
}

void ABirdEnemyCharacter::HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == Tuning.AttackMontage)
	{
		EndAttackHitWindow();
		AnimState.bIsAttacking = false;
	}
}

void ABirdEnemyCharacter::BeginAttackHitWindow(float InDamageMultiplier)
{
	bHitWindowActive = true;
	ActiveDamageMultiplier = FMath::Max(InDamageMultiplier, 0.f);
	HitActorsThisSwing.Reset();
}

void ABirdEnemyCharacter::PerformAttackHitTrace()
{
	UWorld* World = GetWorld();
	if (!World || !bHitWindowActive || !AttackOrigin)
	{
		return;
	}

	const FVector Start = AttackOrigin->GetComponentLocation();
	const FVector End = Start + (GetActorForwardVector() * Tuning.AttackTraceDistance);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BirdAttackTrace), false, this);
	TArray<FHitResult> HitResults;

	const bool bBlockingHit = World->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Tuning.AttackTraceRadius),
		QueryParams);

	if (Tuning.bDrawDebugTraces)
	{
		const FColor TraceColor = HitResults.IsEmpty()
			? FColor::Green
			: (bBlockingHit ? FColor::Red : FColor::Yellow);

		DrawDebugSphere(World, End, Tuning.AttackTraceRadius, 12, TraceColor, false, 0.1f);
		DrawDebugLine(World, Start, End, TraceColor, false, 0.1f, 0, 2.0f);
	}

	// CRITICAL: Check HitResults.IsEmpty(), NOT !bBlockingHit, so ECC_Pawn overlaps still register
	if (HitResults.IsEmpty())
	{
		return;
	}

	const FHitFeedbackConfig& FeedbackCfg = Tuning.AttackFeedback;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActorsThisSwing.Contains(HitActor))
		{
			continue;
		}

		if (!Tuning.TargetTag.IsNone() && !HitActor->ActorHasTag(Tuning.TargetTag))
		{
			continue;
		}

		HitActorsThisSwing.Add(HitActor);

		// Compute knockback pushing the player away from the bird
		FVector KnockDir = (HitActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (KnockDir.IsNearlyZero())
		{
			KnockDir = GetActorForwardVector().GetSafeNormal2D();
		}

		const FVector KnockbackVel =
			(KnockDir * FeedbackCfg.KnockbackHorizontalStrength) +
			FVector(0.0f, 0.0f, FeedbackCfg.KnockbackVerticalStrength);

		FCombatHitPayload Payload;
		Payload.InstigatorActor = this;
		Payload.DamageCauser = this;
		Payload.TargetActor = HitActor;
		Payload.ImpactPoint = Hit.ImpactPoint.IsNearlyZero() ? End : Hit.ImpactPoint;
		Payload.ImpactNormal = Hit.ImpactNormal;
		Payload.KnockbackDirection = KnockDir;
		Payload.KnockbackVelocity = KnockbackVel;
		Payload.HeartDamage = FMath::Max(1, FMath::RoundToInt(FeedbackCfg.HeartDamage * ActiveDamageMultiplier));
		Payload.StaggerDuration = FeedbackCfg.StaggerDuration;
		Payload.AppliedHitStopDuration = FeedbackCfg.HitStopDuration;
		Payload.HitResult = Hit;

		FCombatHitResponse HitResponse;
		bool bHandledByHealthComp = false;

		if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(HitActor))
		{
			HitResponse = Reactable->ReceiveCombatHit(Payload);
			bHandledByHealthComp = true;
		}
		else if (UHeartHealthComponent* TargetHealth = HitActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HitResponse = TargetHealth->ApplyCombatHit(Payload);
			bHandledByHealthComp = true;
		}

		// Respect Player i-frames (no HitStop or double damage while invulnerable)
		if (bHandledByHealthComp && !HitResponse.bHitAccepted)
		{
			if (Tuning.bDrawDebugTraces)
			{
				UE_LOG(LogTemp, Log, TEXT("[BirdAttack] %s ignored hit (i-frames or dead)."), *GetNameSafe(HitActor));
			}
			continue;
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

		// Keep standard UE5 Event AnyDamage working alongside UHeartHealthComponent
		const float FinalDamage = bHandledByHealthComp
			? static_cast<float>(Payload.HeartDamage)
			: (Tuning.BaseDamage * ActiveDamageMultiplier);

		UGameplayStatics::ApplyDamage(HitActor, FinalDamage, GetController(), this, UDamageType::StaticClass());

		OnAttackHit.Broadcast(HitActor, FinalDamage);
		BP_OnAttackHit(HitActor, FinalDamage);

		if (Tuning.bDrawDebugTraces && GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				-1, 2.0f, FColor::Red,
				FString::Printf(TEXT("[BirdAttack] Hit %s for %.0f | Hearts Left: %d/%d"),
					*GetNameSafe(HitActor), FinalDamage, Payload.RemainingHearts, Payload.MaxHearts));
		}
	}
}

void ABirdEnemyCharacter::EndAttackHitWindow()
{
	bHitWindowActive = false;
	HitActorsThisSwing.Reset();
}

// ============================================================================
// DEFENSIVE COMBAT FEEDBACK (IHitReactableInterface & UHeartHealthComponent)
// ============================================================================

FCombatHitResponse ABirdEnemyCharacter::ReceiveCombatHit(const FCombatHitPayload& HitPayload)
{
	return HealthComp ? HealthComp->ApplyCombatHit(HitPayload) : FCombatHitResponse{};
}

void ABirdEnemyCharacter::HandleStaggerChanged(bool bIsStaggered)
{
	if (bIsStaggered)
	{
		// Immediately interrupt any active swoop/peck animation so knockback feels authoritative.
		CancelAttack();

		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->StopActiveMovement();
		}
	}
}

void ABirdEnemyCharacter::HandleDeath(const FCombatHitPayload& KillingHit)
{
	CancelAttack();
	SetActorTickEnabled(false); // Stop hover & bob updates; HealthComp still ticks its hit-flash decay independently.

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->UnPossess();
	}

	// Disable navigation and hurtbox collisions so the dying bird never blocks movement or re-triggers sweeps.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (HurtboxSphere)
	{
		HurtboxSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Drop the hovering bird with a ragdoll knockback impulse and auto-cleanup.
	if (USkeletalMeshComponent* BirdMesh = GetMesh())
	{
		BirdMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		BirdMesh->SetSimulatePhysics(true);
		BirdMesh->AddImpulse(KillingHit.KnockbackVelocity * 1.15f, NAME_None, true);
	}

	SetLifeSpan(3.5f);
}