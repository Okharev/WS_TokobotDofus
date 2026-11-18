// MinionCharacter.cpp
#include "MinionCharacter.h"
#include "MinionAIController.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/NavLinkProxy.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AMinionCharacter::AMinionCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

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
		MoveComp->SetUpdateNavAgentWithOwnersCollisions(true);
		MoveComp->UpdateNavAgent(*this);
	}

	bUseControllerRotationYaw = false;
}

void AMinionCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Enforce zero-push Pawn overlap and NavAgent dimensions in case a Blueprint preset overrode them
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
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

void AMinionCharacter::SetWhipModeEnabled(bool bEnabled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvadeTimerHandle);
		World->GetTimerManager().ClearTimer(NavLinkSafetyTimerHandle);
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
		World->GetTimerManager().ClearTimer(EvadeTimerHandle);
		World->GetTimerManager().ClearTimer(NavLinkSafetyTimerHandle);
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
			MoveComp->SetMovementMode(MOVE_Walking);
		}

		if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
		{
			CrowdCon->SetCrowdSteeringEnabled(true);
		}
	}
}

bool AMinionCharacter::MoveToSquadSlot(const FVector& ProjectedSlotLocation)
{
	CachedSquadSlotLocation = ProjectedSlotLocation;

	if (CurrentState == EMinionState::EvadingPlayer ||
		CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush)
	{
		return true;
	}

	AAIController* AICon = EnsureAIController();
	if (!AICon)
	{
		return false;
	}

	if (FVector::DistSquared2D(GetActorLocation(), ProjectedSlotLocation) <= FMath::Square(SlotAcceptanceRadius + 10.0f))
	{
		CurrentState = EMinionState::Idle;
		return true;
	}

	const bool bIsActuallyMoving = (AICon->GetMoveStatus() == EPathFollowingStatus::Moving) &&
		(GetVelocity().SizeSquared2D() > 25.0f);
	const bool bTargetUnchanged = FVector::DistSquared2D(ProjectedSlotLocation, LastIssuedMoveTarget) < FMath::Square(15.0f);

	if (bIsActuallyMoving && bTargetUnchanged)
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
		true,
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

void AMinionCharacter::EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight)
{
	if (CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush)
	{
		return;
	}

	AAIController* AICon = EnsureAIController();
	if (!AICon)
	{
		return;
	}

	CurrentState = EMinionState::EvadingPlayer;

	const FVector MinionLocation = GetActorLocation();
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
	const FVector LateralDir = ReferenceRight * SideSign;

	const FVector EvadeDirection = (LateralDir * 0.75f + PlayerToMinion * 0.25f).GetSafeNormal2D();
	const FVector TargetEvadeLocation = MinionLocation + (EvadeDirection * EvadeStepDistance);

	AICon->MoveToLocation(TargetEvadeLocation, 5.0f, false, true, true, true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			EvadeTimerHandle,
			this,
			&AMinionCharacter::FinishEvadingPlayer,
			EvadeRecoveryTime,
			false);
	}
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
	if (CurrentState == EMinionState::InAttackWhip || CurrentState == EMinionState::InCarryRush)
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
	}

	CurrentState = EMinionState::TraversingNavLink;
	ActiveNavLink = NavLink;

	if (AMinionAIController* CrowdCon = Cast<AMinionAIController>(AICon))
	{
		CrowdCon->SetCrowdSteeringEnabled(false);
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
}

void AMinionCharacter::CompleteNavLinkTraversal()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NavLinkSafetyTimerHandle);
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