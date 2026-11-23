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
#include "MinionThrowTargetComponent.h"
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
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce
		)
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
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget ||
		bIsRecoveringFromThrowBounce
		)
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
	if (CurrentState == EMinionState::InAttackWhip || CurrentState == EMinionState::InCarryRush || CurrentState == EMinionState::InThrowFlight || CurrentState == EMinionState::LatchedToTarget || bIsRecoveringFromThrowBounce)
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
		World->GetTimerManager().ClearTimer(EvadeTimerHandle);
		World->GetTimerManager().ClearTimer(NavLinkSafetyTimerHandle);
		World->GetTimerManager().ClearTimer(ThrowBounceSafetyTimerHandle);
	}

	bIsRecoveringFromThrowBounce = false;
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

	ThrowStartLocation = GetActorLocation(); // No hard teleport to player hand!

	const FVector TargetGoal = TargetComp->GetSlotWorldLocation(ReservedSlotIndex);
	const float InitialDist = FVector::Dist(ThrowStartLocation, TargetGoal);

	// Distance-scaled arc: close throws stay snappy darts; long throws keep a readable parabola
	const float EffectiveArc = FMath::Clamp(InitialDist * 0.14f, 18.0f, ArcHeight);
	const FVector MidPoint = FMath::Lerp(ThrowStartLocation, TargetGoal, 0.45f);
	// Pull control point slightly toward the player's launch shoulder so the minion visibly whips past the player
	ThrowControlPoint = FMath::Lerp(MidPoint, LaunchOrigin, 0.35f) + FVector(0.0f, 0.0f, EffectiveArc);

	ThrowFlightDuration = FMath::Clamp(InitialDist / FMath::Max(FlightSpeed, 300.0f), 0.11f, 0.48f);
	ThrowFlightElapsed = 0.0f;
	ThrowArcHeight = EffectiveArc;

	if (SquashStretchComp)
	{
		// Quick launch pop stretch along forward flight axis
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
		// Target was destroyed or disabled mid-flight: drop cleanly to the ground
		DetachFromThrowTarget(FVector(0.0f, 0.0f, 120.0f), false);
		return;
	}
	ThrowFlightElapsed += DeltaTime;
	const float RawAlpha = FMath::Clamp(ThrowFlightElapsed / FMath::Max(ThrowFlightDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	// Slight ease-in accelerates into the impact so sudden-stop inertia feels weighty
	const float T = FMath::Pow(RawAlpha, 1.22f);
	const float InvT = 1.0f - T;

	const FVector LiveGoal = TargetComp->GetSlotWorldLocation(ReservedThrowSlotIndex);
	// Quadratic Bezier: B(t) = (1-t)^2 * P0 + 2(1-t)t * P1 + t^2 * P2
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
		// Slam squash along forward impact axis -> spring oscillates 2-3 times and settles
		SquashStretchComp->TriggerImpactAftershock(-10.5f, FVector::ForwardVector, -0.28f);
	}
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

	// Remember the interactable's NavMesh anchor so if the player has no path here,
	// this minion stays grouped around the interactable instead of trying to run to an unreachable takeoff spot.
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

	SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));

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
		bIsRecoveringFromThrowBounce = true;
		CurrentState = EMinionState::Idle;
		LaunchCharacter(PopVelocity, true, true);

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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ThrowBounceSafetyTimerHandle);
	}

	bIsRecoveringFromThrowBounce = false;

	// Refresh local wait anchor to actual landing NavMesh position if still holding near an interactable
	if (bHasInteractableWaitAnchor)
	{
		InteractableWaitAnchor = GetActorLocation();
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
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

	CurrentState = EMinionState::Idle;
	LastIssuedMoveTarget = FVector::ZeroVector;
	
	if (SquashStretchComp)
	{
		// Vertical landing squash along UpVector
		SquashStretchComp->TriggerImpactAftershock(-7.5f, FVector::UpVector, -0.20f);
	}
}