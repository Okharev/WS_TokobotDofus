// MinionCharacter.cpp
#include "MinionCharacter.h"
#include "MinionAIController.h"
#include "MinionThrowTargetComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AMinionCharacter::AMinionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	AIControllerClass = AMinionAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(28.0f, 45.0f);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToAllChannels(ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		// Overlap other Pawns (Player and other Minions) so they never physically push or block each other
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCanEverAffectNavigation(false);
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
		MoveComp->MaxWalkSpeed = 520.0f;
		MoveComp->MaxAcceleration = 2048.0f;
		MoveComp->BrakingDecelerationWalking = 1024.0f;
		MoveComp->bEnablePhysicsInteraction = false;
		MoveComp->PushForceFactor = 0.0f;
		MoveComp->SetJumpAllowed(true);
		MoveComp->bUseRVOAvoidance = false;
		MoveComp->bUseControllerDesiredRotation = false;
		// CRITICAL SAFEGUARD: Prevent crowd separation or velocity overshoot from sliding minions off thin bridges/cliffs
		MoveComp->bCanWalkOffLedges = false;
		MoveComp->SetUpdateNavAgentWithOwnersCollisions(true);
		MoveComp->UpdateNavAgent(*this);
	}

	bUseControllerRotationYaw = false;
}

void AMinionCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Enforce zero-push Pawn overlap, ledge safety, and NavAgent dimensions in case a Blueprint preset overrode them
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		CachedBaseWalkSpeed = FMath::Max(MoveComp->MaxWalkSpeed, 200.0f);
		MoveComp->bCanWalkOffLedges = false;
		MoveComp->SetUpdateNavAgentWithOwnersCollisions(true);
		MoveComp->UpdateNavAgent(*this);
	}

	CachedSquadSlotLocation = GetActorLocation();
	if (AAIController* AICon = EnsureAIController())
	{
		if (UCrowdFollowingComponent* CrowdComp = Cast<UCrowdFollowingComponent>(AICon->GetPathFollowingComponent()))
		{
			CrowdComp->UpdateCrowdAgentParams();
		}
	}
}

void AMinionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(EvadeTimerHandle);
		TM.ClearTimer(NavLinkSafetyTimerHandle);
		TM.ClearTimer(ThrowBounceSafetyTimerHandle);
		TM.ClearTimer(UnstuckCrowdRestoreTimerHandle);
		TM.ClearTimer(ThrowBounceGraceTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AMinionCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CachedAIController = Cast<AAIController>(NewController);
}

AAIController* AMinionCharacter::EnsureAIController()
{
	if (!IsValid(CachedAIController))
	{
		CachedAIController = Cast<AAIController>(GetController());
		if (!CachedAIController && HasAuthority())
		{
			SpawnDefaultController();
			CachedAIController = Cast<AAIController>(GetController());
		}
	}
	return CachedAIController;
}

void AMinionCharacter::SetTightCorridorMode(bool bTight)
{
	if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(EnsureAIController()))
	{
		// If switching crowd simulation state mid-move, invalidate LastIssuedMoveTarget
		// so the next MoveToSquadSlot cleanly resumes path following without stalling.
		const bool bWasCrowdEnabled = CrowdCon->GetCrowdComponent()
			&& CrowdCon->GetCrowdComponent()->IsCrowdSimulationEnabled();

		CrowdCon->SetTightCorridorMode(bTight);

		const bool bIsCrowdEnabledNow = CrowdCon->GetCrowdComponent()
			&& CrowdCon->GetCrowdComponent()->IsCrowdSimulationEnabled();

		if (bWasCrowdEnabled != bIsCrowdEnabledNow)
		{
			LastIssuedMoveTarget = FVector::ZeroVector;
		}
	}
}

bool AMinionCharacter::MoveToSquadSlot(const FVector& ProjectedSlotLocation)
{
	CachedSquadSlotLocation = ProjectedSlotLocation;

	if (CurrentState == EMinionState::EvadingPlayer ||
		CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce)
	{
		return true;
	}

	AAIController* AICon = EnsureAIController();
	if (!AICon)
	{
		return false;
	}

	const float DistToGoalSq = FVector::DistSquared2D(GetActorLocation(), ProjectedSlotLocation);
	const bool bPathFollowMoving = (AICon->GetMoveStatus() == EPathFollowingStatus::Moving);

	// Hysteresis deadzone: once stopped (Idle), don't restart until the slot moves >= 48cm away
	// (prevents constant accelerate -> brake jitter on narrow bridges).
	if (!bPathFollowMoving && DistToGoalSq <= FMath::Square(SlotAcceptanceRadius + 23.0f))
	{
		CurrentState = EMinionState::Idle;
		return true;
	}

	// While actively moving, ignore small slot nudges (< 38cm) so we don't abort & rebuild paths every timer tick.
	const bool bIsActuallyMoving = bPathFollowMoving && (GetVelocity().SizeSquared2D() > 25.0f);
	const bool bTargetWithinRepathDeadzone =
		FVector::DistSquared2D(ProjectedSlotLocation, LastIssuedMoveTarget) < FMath::Square(38.0f);

	if (bIsActuallyMoving && bTargetWithinRepathDeadzone)
	{
		CurrentState = EMinionState::Following;
		return true;
	}

	LastIssuedMoveTarget = ProjectedSlotLocation;

	const EPathFollowingRequestResult::Type MoveResult = AICon->MoveToLocation(
		ProjectedSlotLocation,
		SlotAcceptanceRadius,
		false,
		true,
		false,
		true
	);

	if (MoveResult == EPathFollowingRequestResult::RequestSuccessful ||
		MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		CurrentState = (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
			? EMinionState::Idle
			: EMinionState::Following;
		return true;
	}

	CurrentState = EMinionState::Idle;
	return false;
}
void AMinionCharacter::UpdateFollowSpeedForDistance(float DistanceToSlot, float LeaderSpeed)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp || CurrentState == EMinionState::InAttackWhip || CurrentState == EMinionState::InCarryRush)
	{
		return;
	}

	float TargetSpeed = CachedBaseWalkSpeed;

	if (DistanceToSlot < 120.0f)
	{
		// Ease speed smoothly into the slot to eliminate overshooting and edge sliding
		const float Alpha = FMath::Clamp(DistanceToSlot / 120.0f, 0.0f, 1.0f);
		TargetSpeed = CachedBaseWalkSpeed * FMath::Lerp(0.55f, 0.90f, Alpha);
	}
	else if (DistanceToSlot > 450.0f)
	{
		// Boost catch-up speed when lagging behind after jumps or recalls
		const float BoostAlpha = FMath::Clamp((DistanceToSlot - 450.0f) / 450.0f, 0.0f, 1.0f);
		const float BoostedBase = CachedBaseWalkSpeed * FMath::Lerp(1.05f, MaxCatchUpSpeedMultiplier, BoostAlpha);
		TargetSpeed = FMath::Max(BoostedBase, LeaderSpeed * 1.12f);
	}
	else
	{
		// Normal follow band: match sprinting player smoothly
		TargetSpeed = FMath::Max(CachedBaseWalkSpeed, LeaderSpeed * 1.05f);
	}

	MoveComp->MaxWalkSpeed = TargetSpeed;
}

bool AMinionCharacter::CanBeThrown() const
{
	return (CurrentState == EMinionState::Idle || CurrentState == EMinionState::Following)
		&& !bIsRecoveringFromThrowBounce
		&& !bInPostBounceThrowGrace;
}

void AMinionCharacter::ClearPostBounceThrowGrace()
{
	bInPostBounceThrowGrace = false;
}

void AMinionCharacter::RestoreCrowdSteeringAfterUnstuck()
{
	if (CurrentState == EMinionState::Idle || CurrentState == EMinionState::Following)
	{
		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(EnsureAIController()))
		{
			CrowdCon->SetCrowdSteeringEnabled(true);
		}
	}
}

void AMinionCharacter::ExecuteUnstuckRecovery(int32 RecoveryTier, const FVector& TargetLocation)
{
	if (CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce)
	{
		return;
	}

	UWorld* World = GetWorld();
	AAIController* AICon = EnsureAIController();
	if (!World || !AICon)
	{
		return;
	}

	if (RecoveryTier == 1)
	{
		// Tier 1: Break Detour Crowd doorway/corner deadlock by pausing crowd steering for 0.6s and repathing
		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(false);
		}

		LastIssuedMoveTarget = FVector::ZeroVector;
		AICon->MoveToLocation(TargetLocation, SlotAcceptanceRadius, false, true, false, true);

		World->GetTimerManager().SetTimer(
			UnstuckCrowdRestoreTimerHandle,
			this,
			&AMinionCharacter::RestoreCrowdSteeringAfterUnstuck,
			0.6f,
			false);
	}
	else if (RecoveryTier == 2)
	{
		// Tier 2: Small upward + forward hop to clear uneven debris or collision snags (bCanWalkOffLedges stays false)
		const FVector HopDir = (TargetLocation - GetActorLocation()).GetSafeNormal2D();
		const FVector HopVelocity = (HopDir * 220.0f) + FVector(0.0f, 0.0f, 240.0f);
		LaunchCharacter(HopVelocity, true, true);
		LastIssuedMoveTarget = FVector::ZeroVector;
	}
	else if (RecoveryTier >= 3)
	{
		// Tier 3: Clean off-screen / far-away catch-up warp onto the safe breadcrumb tail
		AICon->StopMovement();
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->SetMovementMode(MOVE_Walking);
			MoveComp->bCanWalkOffLedges = false;
		}

		const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 45.0f;
		FVector SafeWarpLoc = TargetLocation + FVector(0.0f, 0.0f, HalfHeight + 4.0f);
		World->FindTeleportSpot(this, SafeWarpLoc, GetActorRotation());

		SetActorLocationAndRotation(
			SafeWarpLoc,
			FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);

		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(true);
		}

		CurrentState = EMinionState::Idle;
		LastIssuedMoveTarget = FVector::ZeroVector;

		if (SquashStretchComp)
		{
			SquashStretchComp->TriggerImpactAftershock(-6.0f, FVector::UpVector, -0.18f);
		}
	}
}

void AMinionCharacter::SetWhipModeEnabled(bool bEnabled)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(EvadeTimerHandle);
		TM.ClearTimer(NavLinkSafetyTimerHandle);
		TM.ClearTimer(UnstuckCrowdRestoreTimerHandle);
	}

	AAIController* AICon = EnsureAIController();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();

	if (bEnabled)
	{
		CurrentState = EMinionState::InAttackWhip;

		if (AICon)
		{
			AICon->StopMovement();
			if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
			{
				CrowdCon->SetCrowdSteeringEnabled(false);
			}
		}

		if (MoveComp)
		{
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
		}

		if (Capsule)
		{
			// Lock Pitch (X) and Roll (Y) in Chaos so the minion stays upright while constrained in the whip
			if (FBodyInstance* BodyInst = Capsule->GetBodyInstance())
			{
				BodyInst->bLockXRotation = true;
				BodyInst->bLockYRotation = true;
				BodyInst->bLockZRotation = false;
				BodyInst->LinearDamping = 1.5f;
				BodyInst->AngularDamping = 4.0f;
				BodyInst->SetDOFLock(EDOFMode::SixDOF);
			}

			Capsule->SetSimulatePhysics(true);
			Capsule->WakeAllRigidBodies();
		}
	}
	else
	{
		CurrentState = EMinionState::Idle;
		LastIssuedMoveTarget = FVector::ZeroVector;

		if (Capsule)
		{
			Capsule->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Capsule->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Capsule->SetSimulatePhysics(false);
		}

		SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));

		if (MoveComp)
		{
			MoveComp->bCanWalkOffLedges = false;
			MoveComp->MaxWalkSpeed = CachedBaseWalkSpeed;
			MoveComp->SetMovementMode(MOVE_Walking);
		}

		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(true);
		}
	}
}

void AMinionCharacter::SetCarryRushModeEnabled(bool bEnabled)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(EvadeTimerHandle);
		TM.ClearTimer(NavLinkSafetyTimerHandle);
		TM.ClearTimer(UnstuckCrowdRestoreTimerHandle);
	}

	AAIController* AICon = EnsureAIController();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();

	if (bEnabled)
	{
		CurrentState = EMinionState::InCarryRush;

		if (Capsule && Capsule->IsSimulatingPhysics())
		{
			Capsule->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Capsule->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Capsule->SetSimulatePhysics(false);
		}

		if (AICon)
		{
			AICon->StopMovement();
			if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
			{
				CrowdCon->SetCrowdSteeringEnabled(false);
			}
		}

		if (MoveComp)
		{
			MoveComp->StopMovementImmediately();
			MoveComp->SetMovementMode(MOVE_Flying);
		}
	}
	else
	{
		CurrentState = EMinionState::Idle;
		LastIssuedMoveTarget = FVector::ZeroVector;

		SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));

		if (MoveComp)
		{
			MoveComp->StopMovementImmediately();
			MoveComp->bCanWalkOffLedges = false;
			MoveComp->MaxWalkSpeed = CachedBaseWalkSpeed;
			MoveComp->SetMovementMode(MOVE_Walking);
		}

		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(true);
		}
	}
}

void AMinionCharacter::EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight)
{
	if (CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce)
	{
		return;
	}

	AAIController* AICon = EnsureAIController();
	UWorld* World = GetWorld();
	if (!AICon || !World)
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	if (!NavData)
	{
		return;
	}

	const FVector MinionLocation = GetActorLocation();
	FNavLocation StartNav;
	if (!NavSys->ProjectPointToNavigation(MinionLocation, StartNav, FVector(60.0f, 60.0f, 120.0f), NavData))
	{
		return;
	}

	FVector PlayerToMinion = (MinionLocation - PlayerLocation).GetSafeNormal2D();
	if (PlayerToMinion.IsNearlyZero())
	{
		PlayerToMinion = -PlayerRight;
	}

	FVector ReferenceRight = PlayerRight;
	if (PlayerVelocity.SizeSquared2D() > 100.0f)
	{
		const FVector MoveDir = PlayerVelocity.GetSafeNormal2D();
		ReferenceRight = FVector::CrossProduct(FVector::UpVector, MoveDir);
	}

	const float SideSign = (FVector::DotProduct(PlayerToMinion, ReferenceRight) >= 0.0f) ? 1.0f : -1.0f;
	const FVector PreferredLateral = ReferenceRight * SideSign;

	// 4 ranked evade candidates: preferred side -> opposite side -> directly away -> rear diagonal
	const FVector Candidates[4] = {
		(PreferredLateral * 0.75f + PlayerToMinion * 0.25f).GetSafeNormal2D(),
		(-PreferredLateral * 0.75f + PlayerToMinion * 0.25f).GetSafeNormal2D(),
		PlayerToMinion,
		(PreferredLateral * 0.5f - PlayerToMinion * 0.5f).GetSafeNormal2D()
	};

	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, this, nullptr);
	constexpr float EdgeMargin = 38.0f;

	FVector BestEvadeTarget = StartNav.Location;
	float BestScore = -1.0f;

	for (int32 i = 0; i < 4; ++i)
	{
		const FVector DesiredEnd = StartNav.Location + (Candidates[i] * EvadeStepDistance);
		FVector HitLoc = DesiredEnd;
		const bool bHitEdge = NavData->Raycast(StartNav.Location, DesiredEnd, HitLoc, Filter, this);

		const float ClearDist = bHitEdge
			? FMath::Max(0.0f, FVector::Dist2D(StartNav.Location, HitLoc) - EdgeMargin)
			: EvadeStepDistance;

		const float PreferenceBonus = (i == 0) ? 25.0f : 0.0f;
		const float Score = ClearDist + PreferenceBonus;

		if (ClearDist >= 55.0f && Score > BestScore)
		{
			BestScore = Score;
			BestEvadeTarget = StartNav.Location + (Candidates[i] * ClearDist);
		}
	}

	// Boxed in on all sides (thin bridge or tight corner): stay calm instead of jittering into walls/ledges
	if (BestScore < 0.0f)
	{
		return;
	}

	CurrentState = EMinionState::EvadingPlayer;
	AICon->MoveToLocation(BestEvadeTarget, 5.0f, false, true, false, true);

	World->GetTimerManager().SetTimer(
		EvadeTimerHandle,
		this,
		&AMinionCharacter::FinishEvadingPlayer,
		EvadeRecoveryTime,
		false);
}

void AMinionCharacter::FinishEvadingPlayer()
{
	if (CurrentState == EMinionState::EvadingPlayer)
	{
		CurrentState = EMinionState::Idle;
		LastIssuedMoveTarget = FVector::ZeroVector;
		MoveToSquadSlot(CachedSquadSlotLocation);
	}
}

void AMinionCharacter::ExecuteNavLinkJump(ANavLinkProxy* NavLink, const FVector& LaunchVelocity)
{
	if (CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce)
	{
		return;
	}

	AAIController* AICon = EnsureAIController();
	if (!NavLink || !AICon)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvadeTimerHandle);
		World->GetTimerManager().ClearTimer(UnstuckCrowdRestoreTimerHandle);
	}

	CurrentState = EMinionState::TraversingNavLink;
	ActiveNavLink = NavLink;

	if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
	{
		CrowdCon->SetCrowdSteeringEnabled(false);
	}

	// Allow intentional ledge departure during Smart NavLink traversal
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bCanWalkOffLedges = true;
	}

	if (!LaunchVelocity.IsNearlyZero())
	{
		SetActorRotation(LaunchVelocity.GetSafeNormal2D().Rotation());
	}

	LaunchCharacter(LaunchVelocity, true, true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			NavLinkSafetyTimerHandle,
			this,
			&AMinionCharacter::CompleteNavLinkTraversal,
			2.5f,
			false);
	}
}

void AMinionCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (CurrentState == EMinionState::TraversingNavLink)
	{
		CompleteNavLinkTraversal();
	}
	else if (bIsRecoveringFromThrowBounce)
	{
		CompleteThrowBounceRecovery();
	}
}

void AMinionCharacter::CompleteNavLinkTraversal()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NavLinkSafetyTimerHandle);
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bCanWalkOffLedges = false;
	}

	if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(EnsureAIController()))
	{
		CrowdCon->SetCrowdSteeringEnabled(true);
	}

	if (ActiveNavLink.IsValid())
	{
		ActiveNavLink->ResumePathFollowing(this);
		ActiveNavLink.Reset();
	}

	CurrentState = EMinionState::Idle;
	LastIssuedMoveTarget = FVector::ZeroVector;
	MoveToSquadSlot(CachedSquadSlotLocation);
}

bool AMinionCharacter::LaunchAtThrowTarget(
	UMinionThrowTargetComponent* TargetComp,
	int32 ReservedSlotIndex,
	const FVector& LaunchOrigin,
	float FlightSpeed,
	float ArcHeight,
	AActor* ThrowInstigator)
{
	if (!IsValid(TargetComp) ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget)
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(EvadeTimerHandle);
		TM.ClearTimer(NavLinkSafetyTimerHandle);
		TM.ClearTimer(ThrowBounceSafetyTimerHandle);
		TM.ClearTimer(UnstuckCrowdRestoreTimerHandle);
		TM.ClearTimer(ThrowBounceGraceTimerHandle);
	}

	bIsRecoveringFromThrowBounce = false;
	bInPostBounceThrowGrace = false;
	bHasInteractableWaitAnchor = false;

	if (AAIController* AICon = EnsureAIController())
	{
		AICon->StopMovement();
		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(false);
		}
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		if (Capsule->IsSimulatingPhysics())
		{
			Capsule->SetSimulatePhysics(false);
		}
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	ActiveThrowTarget = TargetComp;
	ThrowInstigatorActor = ThrowInstigator;
	ReservedThrowSlotIndex = ReservedSlotIndex;

	ThrowStartLocation = GetActorLocation();

	const FVector TargetGoal = TargetComp->GetSlotWorldLocation(ReservedSlotIndex);
	const float InitialDist = FVector::Dist(ThrowStartLocation, TargetGoal);

	const float EffectiveArc = FMath::Clamp(InitialDist * 0.14f, 18.0f, ArcHeight);
	const FVector MidPoint = FMath::Lerp(ThrowStartLocation, TargetGoal, 0.45f);
	ThrowControlPoint = FMath::Lerp(MidPoint, LaunchOrigin, 0.35f) + FVector(0.0f, 0.0f, EffectiveArc);

	ThrowFlightDuration = FMath::Clamp(InitialDist / FMath::Max(FlightSpeed, 300.0f), 0.11f, 0.48f);
	ThrowFlightElapsed = 0.0f;
	ThrowArcHeight = EffectiveArc;

	if (SquashStretchComp)
	{
		SquashStretchComp->TriggerImpactAftershock(6.5f, FVector::ForwardVector, 0.16f);
	}

	CurrentState = EMinionState::InThrowFlight;
	SetActorTickEnabled(true);
	return true;
}

void AMinionCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentState != EMinionState::InThrowFlight)
	{
		SetActorTickEnabled(false);
		return;
	}

	UMinionThrowTargetComponent* TargetComp = ActiveThrowTarget.Get();
	if (!IsValid(TargetComp) || !TargetComp->IsTargetable())
	{
		DetachFromThrowTarget(FVector(0.0f, 0.0f, 120.0f), false);
		return;
	}

	ThrowFlightElapsed += DeltaTime;
	const float RawAlpha = FMath::Clamp(ThrowFlightElapsed / FMath::Max(ThrowFlightDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	const float T = FMath::Pow(RawAlpha, 1.22f);
	const float InvT = 1.0f - T;

	const FVector LiveGoal = TargetComp->GetSlotWorldLocation(ReservedThrowSlotIndex);
	const FVector PrevPos = GetActorLocation();
	const FVector NewPos =
		(InvT * InvT * ThrowStartLocation) +
		(2.0f * InvT * T * ThrowControlPoint) +
		(T * T * LiveGoal);

	const FVector StepDelta = NewPos - PrevPos;
	const float InstantSpeed = (DeltaTime > KINDA_SMALL_NUMBER) ? (StepDelta.Size() / DeltaTime) : 0.0f;
	const FRotator NewRot = StepDelta.IsNearlyZero() ? GetActorRotation() : StepDelta.GetSafeNormal().Rotation();
	SetActorLocationAndRotation(NewPos, NewRot, false, nullptr, ETeleportType::TeleportPhysics);

	if (SquashStretchComp)
	{
		SquashStretchComp->SetSpeedStretch(InstantSpeed, FVector::ForwardVector);
	}

	if (RawAlpha >= 1.0f)
	{
		SetActorTickEnabled(false);
		const FVector ImpactDir = (LiveGoal - ThrowControlPoint).GetSafeNormal();
		TargetComp->NotifyMinionArrived(this, ReservedThrowSlotIndex, ImpactDir, ThrowInstigatorActor.Get());
	}
}

void AMinionCharacter::ConfirmLatchedToThrowTarget(UMinionThrowTargetComponent* TargetComp, int32 SlotIndex)
{
	if (!IsValid(TargetComp))
	{
		DetachFromThrowTarget(FVector::ZeroVector, false);
		return;
	}

	CurrentState = EMinionState::LatchedToTarget;
	ActiveThrowTarget = TargetComp;
	ReservedThrowSlotIndex = SlotIndex;
	SetActorTickEnabled(false);

	AttachToComponent(TargetComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	const FTransform SlotWorld = TargetComp->GetSlotWorldTransform(SlotIndex);
	SetActorLocationAndRotation(SlotWorld.GetLocation(), SlotWorld.GetRotation().Rotator(), false, nullptr, ETeleportType::TeleportPhysics);

	if (SquashStretchComp)
	{
		SquashStretchComp->TriggerImpactAftershock(-10.5f, FVector::ForwardVector, -0.28f);
	}
}

FVector AMinionCharacter::ComputeSafeBounceVelocity(const FVector& DesiredPopVelocity) const
{
	if (DesiredPopVelocity.SizeSquared2D() < 100.0f)
	{
		return DesiredPopVelocity;
	}

	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!NavSys)
	{
		return DesiredPopVelocity;
	}

	const UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	const float Gravity = (MoveComp && FMath::Abs(MoveComp->GetGravityZ()) > 10.0f)
		? FMath::Abs(MoveComp->GetGravityZ())
		: 980.0f;

	const float EstAirTime = FMath::Clamp((2.0f * FMath::Max(DesiredPopVelocity.Z, 120.0f)) / Gravity, 0.25f, 0.90f);
	const FVector StartLoc = GetActorLocation();
	const FVector HorizVel(DesiredPopVelocity.X, DesiredPopVelocity.Y, 0.0f);

	// Test original direction, then 90°, -90°, and 180° fallback angles to avoid bouncing off cliffs
	static constexpr float TestAnglesDeg[] = { 0.0f, 90.0f, -90.0f, 180.0f };
	for (float AngleDeg : TestAnglesDeg)
	{
		const FVector CandidateHoriz = HorizVel.RotateAngleAxis(AngleDeg, FVector::UpVector);
		const FVector PredictedLand = StartLoc + (CandidateHoriz * EstAirTime);

		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(PredictedLand, Projected, FVector(55.0f, 55.0f, 180.0f)))
		{
			return FVector(CandidateHoriz.X, CandidateHoriz.Y, DesiredPopVelocity.Z);
		}
	}

	// Surrounded by drop-offs: pop gently straight up so the minion lands safely in place
	return FVector(0.0f, 0.0f, FMath::Min(DesiredPopVelocity.Z, 180.0f));
}

void AMinionCharacter::DetachFromThrowTarget(const FVector& PopVelocity, bool bSnapWithoutPop)
{
	SetActorTickEnabled(false);

	const FVector DetachReferenceLoc = ActiveThrowTarget.IsValid()
		? ActiveThrowTarget->GetComponentLocation()
		: GetActorLocation();

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ActiveThrowTarget.Reset();
	ThrowInstigatorActor.Reset();
	ReservedThrowSlotIndex = INDEX_NONE;

	if (!bSnapWithoutPop)
	{
		bHasInteractableWaitAnchor = true;
		InteractableWaitAnchor = DetachReferenceLoc;

		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(DetachReferenceLoc, Projected, FVector(250.0f, 250.0f, 500.0f)))
			{
				InteractableWaitAnchor = Projected.Location;
			}
		}
	}
	else
	{
		bHasInteractableWaitAnchor = false;
	}

	const FRotator UprightRot(0.0f, GetActorRotation().Yaw, 0.0f);
	SetActorRotation(UprightRot);

	// Push out of any overlapping wall/prop BEFORE restoring physics collision
	if (UWorld* World = GetWorld())
	{
		FVector SafeSpot = GetActorLocation();
		if (World->FindTeleportSpot(this, SafeSpot, UprightRot))
		{
			SetActorLocation(SafeSpot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	if (!bSnapWithoutPop && !PopVelocity.IsNearlyZero())
	{
		const FVector SafePopVelocity = ComputeSafeBounceVelocity(PopVelocity);
		if (MoveComp)
		{
			MoveComp->bCanWalkOffLedges = true;
		}

		bIsRecoveringFromThrowBounce = true;
		CurrentState = EMinionState::Idle;
		LaunchCharacter(SafePopVelocity, true, true);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				ThrowBounceSafetyTimerHandle,
				this,
				&AMinionCharacter::CompleteThrowBounceRecovery,
				2.0f,
				false);
		}
	}
	else
	{
		CompleteThrowBounceRecovery();
	}
}

void AMinionCharacter::CompleteThrowBounceRecovery()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(ThrowBounceSafetyTimerHandle);
	}

	bIsRecoveringFromThrowBounce = false;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bCanWalkOffLedges = false;
		MoveComp->MaxWalkSpeed = CachedBaseWalkSpeed;
	}

	if (bHasInteractableWaitAnchor)
	{
		InteractableWaitAnchor = GetActorLocation();
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(GetActorLocation(), Projected, FVector(200.0f, 200.0f, 350.0f)))
			{
				InteractableWaitAnchor = Projected.Location;
			}
		}
	}

	if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(EnsureAIController()))
	{
		CrowdCon->SetCrowdSteeringEnabled(true);
	}

	// Brief throw grace so mashing Throw cycles smoothly through the squad instead of re-grabbing the landing minion
	if (World && PostBounceThrowGraceDuration > KINDA_SMALL_NUMBER)
	{
		bInPostBounceThrowGrace = true;
		World->GetTimerManager().SetTimer(
			ThrowBounceGraceTimerHandle,
			this,
			&AMinionCharacter::ClearPostBounceThrowGrace,
			PostBounceThrowGraceDuration,
			false);
	}

	CurrentState = EMinionState::Idle;
	LastIssuedMoveTarget = FVector::ZeroVector;

	if (SquashStretchComp)
	{
		SquashStretchComp->TriggerImpactAftershock(-7.5f, FVector::UpVector, -0.20f);
	}
}