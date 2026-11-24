// BoarEnemyCharacter.cpp
#include "BoarEnemyCharacter.h"
#include "BoarAIController.h"
#include "BoarRushComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"

ABoarEnemyCharacter::ABoarEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 1. Grounded heavy capsule
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(48.f, 52.f);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// 2. Grounded skeletal mesh aligned to Actor +X forward (-90 Yaw standard UE orientation)
	if (USkeletalMeshComponent* BoarMesh = GetMesh())
	{
		BoarMesh->SetRelativeLocation(FVector(0.f, 0.f, -52.f));
		BoarMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		BoarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 3. Rush hitbox origin attached directly to Capsule so local +X is always Actor Forward
	RushHitOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("RushHitOrigin"));
	RushHitOrigin->SetupAttachment(GetCapsuleComponent());
	RushHitOrigin->SetRelativeLocation(FVector(58.f, 0.f, 0.f));

	// 4. Heart Health Component (auto mesh shake stays TRUE since Boar is grounded)
	HealthComp = CreateDefaultSubobject<UHeartHealthComponent>(TEXT("HealthComp"));
	HealthComp->SetAutoApplyMeshShake(true);

	// 5. Dedicated Rush / Clash / Stun component
	RushComp = CreateDefaultSubobject<UBoarRushComponent>(TEXT("RushComp"));

	// 6. Heavy ground locomotion
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.f, 220.f, 0.f);
		MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
		MoveComp->MaxAcceleration = 1400.f;
		MoveComp->BrakingDecelerationWalking = 1200.f;
		MoveComp->MaxStepHeight = 45.f;
	}

	AIControllerClass = ABoarAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ABoarEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
	}

	if (RushComp)
	{
		RushComp->Initialize(&Tuning, RushHitOrigin);
		RushComp->OnRushClashed.AddUniqueDynamic(this, &ABoarEnemyCharacter::HandleRushClashed);
		RushComp->OnStunVulnerabilityChanged.AddUniqueDynamic(this, &ABoarEnemyCharacter::HandleStunVulnerabilityChanged);
		RushComp->OnRushHitPlayer.AddUniqueDynamic(this, &ABoarEnemyCharacter::HandleRushHitPlayer);
	}

	if (HealthComp)
	{
		HealthComp->OnStaggerChanged.AddUniqueDynamic(this, &ABoarEnemyCharacter::HandleStaggerChanged);
		HealthComp->OnDeath.AddUniqueDynamic(this, &ABoarEnemyCharacter::HandleDeath);
	}
}

void ABoarEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RushComp)
	{
		RushComp->CancelActiveAction();
	}
	Super::EndPlay(EndPlayReason);
}

void ABoarEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateAnimTelemetry();
}

void ABoarEnemyCharacter::UpdateAnimTelemetry()
{
	const float GroundSpeed = GetVelocity().Size2D();
	const float SafeMaxSpeed = FMath::Max(Tuning.RushSpeed, 1.f);

	AnimState.GroundSpeed = GroundSpeed;
	AnimState.NormalizedSpeed = FMath::Clamp(GroundSpeed / SafeMaxSpeed, 0.f, 1.f);
	AnimState.ChargeUpAlpha = RushComp ? RushComp->GetChargeUpAlpha() : 0.f;
	AnimState.bIsChargingUp = RushComp ? RushComp->IsChargingUp() : false;
	AnimState.bIsRushing = RushComp ? RushComp->IsRushing() : false;
	AnimState.bIsStunnedVulnerable = RushComp ? RushComp->IsStunnedVulnerable() : false;

	if (Tuning.bDrawDebugTraces && GetWorld())
	{
		const int32 CurHearts = HealthComp ? HealthComp->GetCurrentHearts() : -1;
		const int32 MaxH = HealthComp ? HealthComp->GetMaxHearts() : -1;
		const FString StateStr = StaticEnum<EBoarAIState>()->GetNameStringByValue(static_cast<int64>(AnimState.CurrentAIState));

		DrawDebugString(
			GetWorld(),
			GetActorLocation() + FVector(0.f, 0.f, 95.f),
			FString::Printf(TEXT("Boar [%s] | Hearts: %d/%d | Stunned: %d | Charge: %.0f%%"),
				*StateStr, CurHearts, MaxH,
				AnimState.bIsStunnedVulnerable ? 1 : 0,
				AnimState.ChargeUpAlpha * 100.f),
			nullptr,
			AnimState.bIsStunnedVulnerable ? FColor::Cyan : FColor::White,
			0.f,
			true);
	}
}

// ============================================================================
// AI STATE & AGGRO TRANSITIONS
// ============================================================================

void ABoarEnemyCharacter::SetAggroState(bool bNewAggroed, AActor* NewTarget)
{
	if (AnimState.bIsAggroed == bNewAggroed)
	{
		return;
	}

	AnimState.bIsAggroed = bNewAggroed;
	OnAggroChanged.Broadcast(bNewAggroed, NewTarget);
	BP_OnAggroChanged(bNewAggroed, NewTarget);
}

void ABoarEnemyCharacter::SetAIState(EBoarAIState NewState)
{
	if (AnimState.CurrentAIState == NewState)
	{
		return;
	}

	const EBoarAIState OldState = AnimState.CurrentAIState;
	AnimState.CurrentAIState = NewState;

	// Adjust ground walk speed for non-rush locomotion states
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		if (NewState == EBoarAIState::Patrol || NewState == EBoarAIState::Idle)
		{
			MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
		}
		else if (NewState == EBoarAIState::ReturnToZone)
		{
			MoveComp->MaxWalkSpeed = Tuning.ReturnSpeed;
		}
	}

	OnAIStateChanged.Broadcast(OldState, NewState);
	BP_OnAIStateChanged(OldState, NewState);
}

// ============================================================================
// DEFENSIVE COMBAT ROUTING (CLASH, ARMOR & SPIN ATTACK VULNERABILITY)
// ============================================================================

FCombatHitResponse ABoarEnemyCharacter::ReceiveCombatHit(const FCombatHitPayload& HitPayload)
{
	if (!HealthComp || !HealthComp->IsAlive())
	{
		return FCombatHitResponse{};
	}

	AActor* Attacker = HitPayload.InstigatorActor
		? HitPayload.InstigatorActor.Get()
		: HitPayload.DamageCauser.Get();

	// 1. Mid-Rush: intercept Carry Rush for head-on clash; deflect everything else (rush super-armor)
	if (RushComp && RushComp->IsRushing())
	{
		if (RushComp->TryResolveRushClash(Attacker))
		{
			FCombatHitResponse ClashResp;
			ClashResp.bHitAccepted = false; // Handled as clash, not normal HP hit
			ClashResp.RemainingHearts = HealthComp->GetCurrentHearts();
			ClashResp.MaxHearts = HealthComp->GetMaxHearts();
			return ClashResp;
		}

		OnHitDeflected.Broadcast(HitPayload);
		BP_OnHitDeflected(HitPayload);
		return FCombatHitResponse{};
	}

	// 2. Stunned after Clash: open to Spin Attack
	if (RushComp && RushComp->IsStunnedVulnerable())
	{
		const bool bIsSpinAttack =
			!Tuning.SpinAttackTypeName.IsNone() &&
			(HitPayload.AttackType == Tuning.SpinAttackTypeName);

		if (Tuning.bRequireSpinAttackWhileStunned && !bIsSpinAttack)
		{
			OnHitDeflected.Broadcast(HitPayload);
			BP_OnHitDeflected(HitPayload);
			return FCombatHitResponse{};
		}

		FCombatHitPayload FinalPayload = HitPayload;
		if (bIsSpinAttack)
		{
			FinalPayload.HeartDamage = FMath::Max(1, FinalPayload.HeartDamage) + Tuning.SpinAttackBonusHearts;
		}

		const FCombatHitResponse Resp = HealthComp->ApplyCombatHit(FinalPayload);
		if (Resp.bHitAccepted && bIsSpinAttack && Tuning.bEndStunOnSpinAttackHit)
		{
			RushComp->EndStunnedVulnerable();
		}
		return Resp;
	}

	// 3. Neutral / Patrol / ChargeUp / Recovering: either armored or takes normal damage
	if (Tuning.bArmoredWhenNotStunned)
	{
		// Still allow attacker to draw aggro even if the hit bounced off thick hide
		if (ABoarAIController* BoarAI = Cast<ABoarAIController>(GetController()))
		{
			BoarAI->NotifyDamagedOrProvokedBy(Attacker);
		}

		OnHitDeflected.Broadcast(HitPayload);
		BP_OnHitDeflected(HitPayload);
		return FCombatHitResponse{};
	}

	return HealthComp->ApplyCombatHit(HitPayload);
}

// ============================================================================
// COMPONENT EVENT HANDLERS
// ============================================================================

void ABoarEnemyCharacter::HandleRushClashed(AActor* ClashedActor)
{
	OnRushClashed.Broadcast(ClashedActor);
	BP_OnRushClashed(ClashedActor);
}

void ABoarEnemyCharacter::HandleStunVulnerabilityChanged(bool bIsStunned)
{
	AnimState.bIsStunnedVulnerable = bIsStunned;
	OnStunVulnerabilityChanged.Broadcast(bIsStunned);
	BP_OnStunVulnerabilityChanged(bIsStunned);
}

void ABoarEnemyCharacter::HandleRushHitPlayer(AActor* HitActor, float DamageDealt)
{
	BP_OnRushHitPlayer(HitActor, DamageDealt);
}

void ABoarEnemyCharacter::HandleStaggerChanged(bool bIsStaggered)
{
	// Interrupt ChargeUp if hit-staggered, but NEVER cancel the post-clash StunnedVulnerable window
	if (bIsStaggered && RushComp && !RushComp->IsStunnedVulnerable())
	{
		RushComp->CancelActiveAction();
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->StopActiveMovement();
		}
	}
}

void ABoarEnemyCharacter::HandleDeath(const FCombatHitPayload& KillingHit)
{
	if (RushComp)
	{
		RushComp->CancelActiveAction();
		RushComp->EndStunnedVulnerable();
	}

	SetActorTickEnabled(false);

	if (AAIController* AICon = Cast<AAIController>(GetController()))
	{
		AICon->StopMovement();
		AICon->UnPossess();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (USkeletalMeshComponent* BoarMesh = GetMesh())
	{
		BoarMesh->SetCollisionProfileName(TEXT("Ragdoll"));
		BoarMesh->SetSimulatePhysics(true);
		BoarMesh->AddImpulse(KillingHit.KnockbackVelocity * 1.15f, NAME_None, true);
	}

	SetLifeSpan(3.5f);
}