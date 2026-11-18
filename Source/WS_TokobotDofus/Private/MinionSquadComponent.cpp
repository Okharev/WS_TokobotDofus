// MinionSquadComponent.cpp
#include "MinionSquadComponent.h"
#include "MinionAgentInterface.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "AIController.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "NavFilters/NavigationQueryFilter.h"

// ============================================================================
// STANCE 1: UMinionStance_Follow IMPLEMENTATION
// ============================================================================

void UMinionStance_Follow::OnEnterStance()
{
	if (!Squad) return;

	WaitingGroupAnchorTransform = Squad->GetComponentTransform();

	// Project initial waiting anchor onto the ground NavMesh at the takeoff/entry spot
	if (UWorld* World = Squad->GetWorld())
	{
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation GroundNavLoc;
			if (NavSys->ProjectPointToNavigation(WaitingGroupAnchorTransform.GetLocation(), GroundNavLoc, FVector(250.0f, 250.0f, 500.0f)))
			{
				WaitingGroupAnchorTransform.SetLocation(GroundNavLoc.Location);
			}
		}
	}

	LastAnchorLocation = WaitingGroupAnchorTransform.GetLocation();
	LastAnchorRotation = WaitingGroupAnchorTransform.Rotator();
	bForceNextUpdate = true;
	LastReachableMinionCount = -1;

	if (UWorld* World = Squad->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FollowTimerHandle,
			this,
			&UMinionStance_Follow::EvaluateFormationAndAvoidance,
			Squad->UpdateInterval,
			true);
	}

	EvaluateFormationAndAvoidance();
}

void UMinionStance_Follow::OnExitStance()
{
	if (Squad && Squad->GetWorld())
	{
		Squad->GetWorld()->GetTimerManager().ClearTimer(FollowTimerHandle);
	}
}

void UMinionStance_Follow::OnSquadRosterChanged()
{
	ForceImmediateEvaluation();
}

void UMinionStance_Follow::ForceImmediateEvaluation()
{
	bForceNextUpdate = true;
	EvaluateFormationAndAvoidance();
}

FVector UMinionStance_Follow::CalculateSlotAroundTransform(int32 SlotIndex, const FTransform& AnchorTransform) const
{
	if (!Squad) return AnchorTransform.GetLocation();

	constexpr int32 ColumnsPerRow = 4;
	const int32 Row = SlotIndex / ColumnsPerRow;
	const int32 Col = SlotIndex % ColumnsPerRow;

	const float CenterOffset = (ColumnsPerRow - 1) * 0.5f;
	const float NormalizedCol = FMath::Abs(Col - CenterOffset);
	const float CrescentPullback = (NormalizedCol > 1.0f) ? 25.0f : 0.0f;

	const float LocalX = -(Row * Squad->RowSpacing) - CrescentPullback;
	const float RowStagger = (Row % 2 == 1) ? (Squad->ColumnSpacing * 0.35f) : 0.0f;
	const float LocalY = ((Col - CenterOffset) * Squad->ColumnSpacing) + RowStagger;

	return AnchorTransform.TransformPosition(FVector(LocalX, LocalY, 0.0f));
}

void UMinionStance_Follow::EvaluateFormationAndAvoidance()
{
	if (!Squad) return;

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	if (!PlayerActor || !World) return;

	const int32 MinionCount = Squad->PruneInvalidMinions();
	if (MinionCount == 0) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance())
	{
		return;
	}

	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const FVector PlayerVelocity = PlayerActor->GetVelocity();
	const FVector PlayerRight = PlayerActor->GetActorRightVector();
	const float EvadeRadiusSq = FMath::Square(Squad->PlayerEvadeRadius);

	// 1. Classify each minion as Reachable vs. Stranded (skipping any minion currently carried in mid-air)
	FVector PlayerNavLoc = PlayerLocation;
	const bool bPlayerGroundedOnNav = Squad->IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc);

	TArray<int32, TInlineAllocator<16>> ReachableIndices;
	TArray<int32, TInlineAllocator<16>> StrandedIndices;

	for (int32 i = 0; i < MinionCount; ++i)
	{
		IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[i].GetInterface();
		if (!MinionInterface) continue;

		// Skip the minion currently attached to the player during a mid-air Carry Jump
		if (MinionInterface->GetMinionState() == EMinionState::InCarryRush)
		{
			continue;
		}

		if (bPlayerGroundedOnNav && Squad->CanMinionReachNavLocation(Squad->ActiveMinions[i], NavSys, PlayerNavLoc))
		{
			ReachableIndices.Add(i);
		}
		else
		{
			StrandedIndices.Add(i);
		}
	}

	// Only move the shared WaitingGroupAnchorTransform when ALL non-carried minions can reach the player
	if (StrandedIndices.IsEmpty() && !ReachableIndices.IsEmpty())
	{
		WaitingGroupAnchorTransform = Squad->GetComponentTransform();
	}

	if (ReachableIndices.Num() != LastReachableMinionCount)
	{
		LastReachableMinionCount = ReachableIndices.Num();
		bForceNextUpdate = true;
	}

	const FTransform LivePlayerAnchor = Squad->GetComponentTransform();
	const FVector CurrentAnchorLoc = LivePlayerAnchor.GetLocation();
	const FRotator CurrentAnchorRot = LivePlayerAnchor.Rotator();

	const bool bAnchorMoved = bForceNextUpdate ||
		FVector::DistSquared(CurrentAnchorLoc, LastAnchorLocation) >= FMath::Square(Squad->AnchorMoveThreshold) ||
		!CurrentAnchorRot.Equals(LastAnchorRotation, 8.0f);

	if (bAnchorMoved)
	{
		LastAnchorLocation = CurrentAnchorLoc;
		LastAnchorRotation = CurrentAnchorRot;
		bForceNextUpdate = false;
		Squad->OnSquadMoveUpdated.Broadcast(MinionCount, CurrentAnchorLoc);
	}

	const FVector NavProjectionExtent(250.0f, 250.0f, 500.0f);

	// Helper lambda to assign a group of minions (either Reachable around the Player, or Stranded around the Takeoff Anchor)
	auto AssignGroupToAnchor = [&](const TArray<int32, TInlineAllocator<16>>& GroupIndices, const FTransform& GroupAnchor, FColor DebugSlotColor, bool bForceGroupMove)
	{
		const int32 GroupCount = GroupIndices.Num();
		if (GroupCount == 0) return;

		if (Squad->bDebugDrawSlots)
		{
			DrawDebugSphere(World, GroupAnchor.GetLocation(), 20.0f, 8, DebugSlotColor, false, Squad->UpdateInterval * 1.1f);
		}

		TArray<FVector, TInlineAllocator<16>> GroupSlots;
		GroupSlots.SetNumUninitialized(GroupCount);

		for (int32 SlotIdx = 0; SlotIdx < GroupCount; ++SlotIdx)
		{
			const FVector IdealSlot = CalculateSlotAroundTransform(SlotIdx, GroupAnchor);
			FNavLocation ProjectedLoc;
			if (NavSys->ProjectPointToNavigation(IdealSlot, ProjectedLoc, NavProjectionExtent))
			{
				GroupSlots[SlotIdx] = ProjectedLoc.Location;
			}
			else if (NavSys->ProjectPointToNavigation(GroupAnchor.GetLocation(), ProjectedLoc, NavProjectionExtent))
			{
				GroupSlots[SlotIdx] = ProjectedLoc.Location;
			}
			else
			{
				GroupSlots[SlotIdx] = IdealSlot;
			}
		}

		TArray<bool, TInlineAllocator<16>> bUsed;
		bUsed.Init(false, GroupCount);

		for (int32 SlotIdx = 0; SlotIdx < GroupCount; ++SlotIdx)
		{
			const FVector& SlotLocation = GroupSlots[SlotIdx];
			int32 BestLocalIdx = INDEX_NONE;
			float BestDistSq = MAX_flt;

			for (int32 LocalIdx = 0; LocalIdx < GroupCount; ++LocalIdx)
			{
				if (bUsed[LocalIdx]) continue;
				const int32 SquadIdx = GroupIndices[LocalIdx];
				const float DistSq = FVector::DistSquared2D(Squad->ActiveMinions[SquadIdx]->GetActorLocation(), SlotLocation);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestLocalIdx = LocalIdx;
				}
			}

			if (BestLocalIdx == INDEX_NONE) continue;
			bUsed[BestLocalIdx] = true;

			const int32 SquadIdx = GroupIndices[BestLocalIdx];
			AActor* MinionActor = Squad->ActiveMinions[SquadIdx];
			IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[SquadIdx].GetInterface();
			if (!MinionInterface) continue;

			const EMinionState CurrentState = MinionInterface->GetMinionState();
			if (CurrentState == EMinionState::TraversingNavLink) continue;

			const float VerticalDist = FMath::Abs(MinionActor->GetActorLocation().Z - PlayerLocation.Z);
			if (VerticalDist < 120.0f && FVector::DistSquared2D(MinionActor->GetActorLocation(), PlayerLocation) < EvadeRadiusSq)
			{
				MinionInterface->EvadePlayer(PlayerLocation, PlayerVelocity, PlayerRight);
				continue;
			}

			if (Squad->bDebugDrawSlots)
			{
				DrawDebugSphere(World, SlotLocation, 14.0f, 8, DebugSlotColor, false, Squad->UpdateInterval * 1.1f);
				DrawDebugLine(World, MinionActor->GetActorLocation(), SlotLocation, FColor::Cyan, false, Squad->UpdateInterval * 1.1f, 0, 1.5f);
			}

			const float DistToSlotSq = FVector::DistSquared2D(MinionActor->GetActorLocation(), SlotLocation);
			if (bForceGroupMove || CurrentState == EMinionState::Idle || DistToSlotSq > FMath::Square(35.0f))
			{
				MinionInterface->MoveToSquadSlot(SlotLocation);
			}
		}
	};

	// 2. Command reachable minions (including the 1 carried minion after landing) to follow the live Player anchor
	AssignGroupToAnchor(ReachableIndices, LivePlayerAnchor, FColor::Green, bAnchorMoved);

	// 3. Command stranded minions to stay grouped around the takeoff WaitingGroupAnchorTransform
	AssignGroupToAnchor(StrandedIndices, WaitingGroupAnchorTransform, FColor::Blue, false);
}

// ============================================================================
// STANCE 2: UMinionStance_ChaosWhip IMPLEMENTATION (Uses StanceMinions)
// ============================================================================

void UMinionStance_ChaosWhip::OnEnterStance()
{
	if (!Squad) return;

	Squad->SortStanceMinionsForWhipArms();
	Squad->HitActorTimestamps.Reset();

	bIsSpinAttacking = false;
	bIsContinuousTornadoActive = false;
	SpinElapsedTime = 0.0f;
	ContinuousTornadoAngleOffset = 0.0f;

	if (Squad->WhipConfig.bWalkIntoWhipFormation)
	{
		bIsWalkingIntoWhip = true;
		WhipWalkInElapsedTime = 0.0f;
		WhipWalkInRepathTimer = 0.0f;
		Squad->BroadcastDebug(
			FString::Printf(TEXT("%d Reachable Minions walking into Whip Formation..."), Squad->StanceMinions.Num()),
			FColor::Yellow,
			2.0f);
	}
	else
	{
		bIsWalkingIntoWhip = false;
		BindPooledChaosChains();
	}
}

void UMinionStance_ChaosWhip::OnExitStance()
{
	bIsWalkingIntoWhip = false;
	bPendingBurstSpinAfterWalkIn = false;
	bPendingContinuousSpinAfterWalkIn = false;
	bIsSpinAttacking = false;
	bIsContinuousTornadoActive = false;

	ReleasePooledChaosChains();
}

void UMinionStance_ChaosWhip::OnSquadRosterChanged()
{
	if (!Squad) return;

	if (Squad->PruneInvalidStanceMinions() < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	Squad->SortStanceMinionsForWhipArms();
	if (!bIsWalkingIntoWhip)
	{
		BindPooledChaosChains();
	}
}

bool UMinionStance_ChaosWhip::StartBurstSpin()
{
	if (!Squad) return false;

	Squad->HitActorTimestamps.Reset();

	if (bIsWalkingIntoWhip)
	{
		bPendingBurstSpinAfterWalkIn = true;
		return true;
	}

	bIsSpinAttacking = true;
	SpinElapsedTime = 0.0f;
	Squad->BroadcastDebug(TEXT("Damped Chain Tornado Spin Triggered!"), FColor::Magenta, 1.5f);
	return true;
}

void UMinionStance_ChaosWhip::SetContinuousSpin(bool bEnable)
{
	if (!Squad) return;

	if (bEnable)
	{
		Squad->HitActorTimestamps.Reset();
		if (bIsWalkingIntoWhip)
		{
			bPendingContinuousSpinAfterWalkIn = true;
			return;
		}
		bIsContinuousTornadoActive = true;
		bIsSpinAttacking = false;
	}
	else
	{
		bPendingContinuousSpinAfterWalkIn = false;
		bIsContinuousTornadoActive = false;
		ContinuousTornadoAngleOffset = 0.0f;
		Squad->UpdateHandAnchorTransforms(0.0f);
	}
}

void UMinionStance_ChaosWhip::UpdateWalkIntoWhipFormation(float DeltaTime)
{
	if (!Squad) return;

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	if (!PlayerActor || !World) return;

	Squad->UpdateHandAnchorTransforms(0.0f);

	WhipWalkInElapsedTime += DeltaTime;
	WhipWalkInRepathTimer -= DeltaTime;

	const bool bShouldIssueMove = (WhipWalkInRepathTimer <= 0.0f);
	if (bShouldIssueMove)
	{
		WhipWalkInRepathTimer = 0.1f;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	// Verify the player hasn't jumped off the NavMesh onto an isolated ledge during walk-in!
	FVector PlayerNavLoc = FVector::ZeroVector;
	if (!Squad->IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc))
	{
		Squad->BroadcastDebug(TEXT("Whip Walk-In Aborted: Player left the NavMesh!"), FColor::Red, 2.0f);
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	const FVector NavProjectionExtent(200.0f, 200.0f, 400.0f);
	const float ArrivalRadiusSq = FMath::Square(Squad->WhipConfig.WalkInArrivalRadius);

	// Prune any minion in StanceMinions that lost its path to the player during walk-in
	if (bShouldIssueMove)
	{
		for (int32 i = Squad->StanceMinions.Num() - 1; i >= 0; --i)
		{
			AActor* MinionActor = Squad->StanceMinions[i];
			if (!IsValid(MinionActor) || !Squad->CanMinionReachNavLocation(MinionActor, NavSys, PlayerNavLoc))
			{
				Squad->StanceMinions.RemoveAtSwap(i);
				Squad->StanceMinionInterfaces.RemoveAtSwap(i);
			}
		}

		// If fewer than 4 reachable minions remain, cancel Whip stance back to Follow!
		if (Squad->StanceMinions.Num() < Squad->MinReachableMinionsForSpecialStance)
		{
			Squad->BroadcastDebug(
				FString::Printf(TEXT("Whip Walk-In Canceled: Only %d reachable minions remain (Need %d)!"),
					Squad->StanceMinions.Num(), Squad->MinReachableMinionsForSpecialStance),
				FColor::Red,
				2.5f);
			Squad->SetSquadStance(EMinionSquadStance::Follow);
			return;
		}
	}

	bool bAllMinionsArrived = true;
	int32 ArrivedCount = 0;
	const int32 StanceCount = Squad->PruneInvalidStanceMinions();

	for (int32 i = 0; i < StanceCount; ++i)
	{
		AActor* MinionActor = Squad->StanceMinions[i];
		IMinionAgentInterface* MinionInterface = Squad->StanceMinionInterfaces[i].GetInterface();
		if (!MinionActor || !MinionInterface) continue;

		FVector TargetLinkWorld = Squad->CalculateWhipLinkWorldTarget(i);
		FNavLocation ProjectedLoc;
		if (NavSys->ProjectPointToNavigation(TargetLinkWorld, ProjectedLoc, NavProjectionExtent))
		{
			TargetLinkWorld = ProjectedLoc.Location;
		}
		else
		{
			TargetLinkWorld = PlayerNavLoc;
		}

		const float DistSq2D = FVector::DistSquared2D(MinionActor->GetActorLocation(), TargetLinkWorld);
		if (DistSq2D > ArrivalRadiusSq)
		{
			bAllMinionsArrived = false;
			if (bShouldIssueMove)
			{
				MinionInterface->MoveToSquadSlot(TargetLinkWorld);
			}
		}
		else
		{
			++ArrivedCount;
		}

		if (Squad->bDebugDrawSlots)
		{
			DrawDebugSphere(World, TargetLinkWorld, 15.0f, 8, FColor::Yellow, false, -1.0f);
			DrawDebugLine(World, MinionActor->GetActorLocation(), TargetLinkWorld, FColor::Yellow, false, -1.0f, 0, 2.0f);
		}
	}

	// Lock into Chaos chains once all reachable minions arrive, or if timeout expires and at least 4 have arrived near the player
	if (bAllMinionsArrived || WhipWalkInElapsedTime >= Squad->WhipConfig.MaxWalkInDuration)
	{
		if (!bAllMinionsArrived)
		{
			// Filter StanceMinions to only those that actually made it close to their whip link slot (never snap far minions!)
			const float MaxLockInDistSq = FMath::Square(Squad->WhipConfig.WalkInArrivalRadius * 2.0f);
			for (int32 i = Squad->StanceMinions.Num() - 1; i >= 0; --i)
			{
				const FVector SlotTarget = Squad->CalculateWhipLinkWorldTarget(i);
				if (FVector::DistSquared2D(Squad->StanceMinions[i]->GetActorLocation(), SlotTarget) > MaxLockInDistSq)
				{
					Squad->StanceMinions.RemoveAtSwap(i);
					Squad->StanceMinionInterfaces.RemoveAtSwap(i);
				}
			}

			if (Squad->StanceMinions.Num() < Squad->MinReachableMinionsForSpecialStance)
			{
				Squad->BroadcastDebug(TEXT("Whip Walk-In Timed Out before 4 minions arrived: Returning to Follow."), FColor::Red, 2.5f);
				Squad->SetSquadStance(EMinionSquadStance::Follow);
				return;
			}

			Squad->SortStanceMinionsForWhipArms();
		}

		bIsWalkingIntoWhip = false;
		BindPooledChaosChains();

		if (bPendingBurstSpinAfterWalkIn)
		{
			bPendingBurstSpinAfterWalkIn = false;
			bIsSpinAttacking = true;
			SpinElapsedTime = 0.0f;
		}
		else if (bPendingContinuousSpinAfterWalkIn)
		{
			bPendingContinuousSpinAfterWalkIn = false;
			bIsContinuousTornadoActive = true;
		}
	}
}

void UMinionStance_ChaosWhip::BindPooledChaosChains()
{
	if (!Squad) return;

	ReleasePooledChaosChains();

	AActor* OwnerActor = Squad->GetOwner();
	const int32 MinionCount = Squad->PruneInvalidStanceMinions();
	if (!OwnerActor || MinionCount < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	Squad->UpdateHandAnchorTransforms(0.0f);

	const FMinionWhipConfig& Cfg = Squad->WhipConfig;
	PrevMinionLocations.SetNumUninitialized(MinionCount);
	InitialMinionHeights.SetNumUninitialized(MinionCount);

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = Squad->StanceMinions[i];
		IMinionAgentInterface* MinionInterface = Squad->StanceMinionInterfaces[i].GetInterface();
		if (!MinionActor || !MinionInterface) continue;

		const int32 ArmIndex = Cfg.bDualArmWhip ? (i % 2) : 0;
		USphereComponent* HandSphere = Squad->HandAnchorSpheres[ArmIndex];
		const FRotator OutwardArmRot = HandSphere ? HandSphere->GetComponentRotation() : OwnerActor->GetActorRotation();

		const FVector CurrentPos = MinionActor->GetActorLocation();
		InitialMinionHeights[i] = CurrentPos.Z;
		PrevMinionLocations[i] = CurrentPos;

		MinionActor->SetActorRotation(FRotator(0.0f, OutwardArmRot.Yaw, 0.0f));
		MinionInterface->SetWhipModeEnabled(true);

		if (UPrimitiveComponent* MinionPrim = Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()))
		{
			MinionPrim->SetLinearDamping(Cfg.ChaosLinearDamping);
			MinionPrim->SetAngularDamping(8.0f);
		}
	}

	for (int32 i = 0; i < MinionCount; ++i)
	{
		if (!Squad->ConstraintPool.IsValidIndex(i) || !IsValid(Squad->ConstraintPool[i]))
		{
			continue;
		}

		AActor* ChildMinion = Squad->StanceMinions[i];
		UPrimitiveComponent* ChildPrim = ChildMinion ? Cast<UPrimitiveComponent>(ChildMinion->GetRootComponent()) : nullptr;
		if (!ChildPrim) continue;

		const int32 ArmIndex = Cfg.bDualArmWhip ? (i % 2) : 0;
		const int32 LinkIndexInArm = Cfg.bDualArmWhip ? (i / 2) : i;
		const int32 ParentMinionIdx = Cfg.bDualArmWhip ? (i - 2) : (i - 1);

		UPrimitiveComponent* ParentPrim = nullptr;
		float TargetLinkDistance = Cfg.LinkSpacing;

		if (LinkIndexInArm == 0 || ParentMinionIdx < 0)
		{
			ParentPrim = Squad->HandAnchorSpheres[ArmIndex];
			TargetLinkDistance = Cfg.StartOffsetFromArm;
		}
		else if (Squad->StanceMinions.IsValidIndex(ParentMinionIdx) && IsValid(Squad->StanceMinions[ParentMinionIdx]))
		{
			ParentPrim = Cast<UPrimitiveComponent>(Squad->StanceMinions[ParentMinionIdx]->GetRootComponent());
			TargetLinkDistance = Cfg.LinkSpacing;
		}

		if (!ParentPrim) continue;

		UPhysicsConstraintComponent* Constraint = Squad->ConstraintPool[i];
		Constraint->AttachToComponent(ParentPrim, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Constraint->SetDisableCollision(true);

		Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Limited, Cfg.LinearConstraintSlack);
		Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Limited, Cfg.LinearConstraintSlack);
		Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Limited, Cfg.MaxVerticalSlack);

		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Stiffness = Cfg.LinearSpringStiffness;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Damping = Cfg.LinearSpringDamping;

		Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, Cfg.MaxJointBendAngleDegrees);
		Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, 10.0f);
		Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);

		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Stiffness = Cfg.AngularBendStiffness;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Damping = Cfg.AngularBendDamping;

		Constraint->SetConstrainedComponents(ParentPrim, NAME_None, ChildPrim, NAME_None);

		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame1, FVector(TargetLinkDistance, 0.0f, 0.0f));
		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame2, FVector::ZeroVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame1, FVector::ForwardVector, FVector::RightVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame2, FVector::ForwardVector, FVector::RightVector);

		Constraint->SetLinearPositionDrive(true, true, true);
		Constraint->SetLinearVelocityDrive(true, true, true);
		Constraint->SetLinearPositionTarget(FVector::ZeroVector);
		Constraint->SetLinearVelocityTarget(FVector::ZeroVector);
		Constraint->SetLinearDriveParams(Cfg.LinearSpringStiffness, Cfg.LinearSpringDamping, 0.0f);

		Constraint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
		Constraint->SetAngularOrientationDrive(true, true);
		Constraint->SetAngularVelocityDrive(true, true);
		Constraint->SetAngularOrientationTarget(FRotator::ZeroRotator);
		Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
		Constraint->SetAngularDriveParams(Cfg.AngularBendStiffness, Cfg.AngularBendDamping, 0.0f);
	}

	Squad->BroadcastDebug(
		FString::Printf(TEXT("Pooled Whip Chains Locked In (%d Reachable Minions)!"), MinionCount),
		FColor::Orange,
		1.5f);
}

void UMinionStance_ChaosWhip::ReleasePooledChaosChains()
{
	if (!Squad) return;

	for (UPhysicsConstraintComponent* Constraint : Squad->ConstraintPool)
	{
		if (IsValid(Constraint))
		{
			Constraint->BreakConstraint();
			if (AActor* OwnerActor = Squad->GetOwner())
			{
				Constraint->AttachToComponent(OwnerActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			}
		}
	}

	for (int32 i = 0; i < Squad->StanceMinionInterfaces.Num(); ++i)
	{
		if (IMinionAgentInterface* MinionInterface = Squad->StanceMinionInterfaces[i].GetInterface())
		{
			MinionInterface->SetWhipModeEnabled(false);
		}
	}

	Squad->UpdateHandAnchorTransforms(0.0f);
}

void UMinionStance_ChaosWhip::TickStance(float DeltaTime)
{
	if (!Squad) return;

	if (bIsWalkingIntoWhip)
	{
		UpdateWalkIntoWhipFormation(DeltaTime);
		return;
	}

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	const int32 MinionCount = Squad->PruneInvalidStanceMinions();
	if (!PlayerActor || !World || MinionCount < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	const FMinionWhipConfig& Cfg = Squad->WhipConfig;

	if (PrevMinionLocations.Num() != MinionCount || InitialMinionHeights.Num() != MinionCount)
	{
		PrevMinionLocations.SetNumUninitialized(MinionCount);
		InitialMinionHeights.SetNumUninitialized(MinionCount);
		for (int32 i = 0; i < MinionCount; ++i)
		{
			const FVector Loc = Squad->StanceMinions[i]->GetActorLocation();
			PrevMinionLocations[i] = Loc;
			InitialMinionHeights[i] = Loc.Z;
		}
	}

	const float DirectionSign = (Cfg.SpinDirection >= 0.0f) ? 1.0f : -1.0f;
	float OrbitAngleYaw = 0.0f;

	if (bIsSpinAttacking)
	{
		SpinElapsedTime += DeltaTime;
		if (SpinElapsedTime >= Cfg.SpinDuration)
		{
			bIsSpinAttacking = false;
			SpinElapsedTime = 0.0f;
			Squad->UpdateHandAnchorTransforms(0.0f);
		}
		else
		{
			const float Progress = FMath::Clamp(SpinElapsedTime / FMath::Max(Cfg.SpinDuration, 0.05f), 0.0f, 1.0f);
			const float EasedProgress = FMath::InterpEaseInOut(0.0f, 1.0f, Progress, 1.5f);
			OrbitAngleYaw = EasedProgress * Cfg.SpinRevolutions * 360.0f * DirectionSign;
		}
	}
	else if (bIsContinuousTornadoActive)
	{
		ContinuousTornadoAngleOffset = FMath::Fmod(
			ContinuousTornadoAngleOffset + (Cfg.ContinuousTornadoSpeedDegPerSec * DirectionSign * DeltaTime),
			360.0f);
		OrbitAngleYaw = ContinuousTornadoAngleOffset;
	}

	const bool bAnySpinActive = bIsSpinAttacking || bIsContinuousTornadoActive;
	Squad->UpdateHandAnchorTransforms(OrbitAngleYaw);

	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const float ReferenceHeightZ = PlayerLocation.Z;

	FVector PrevDebugPos[2] = {
		Squad->HandAnchorSpheres[0] ? Squad->HandAnchorSpheres[0]->GetComponentLocation() : PlayerLocation,
		Squad->HandAnchorSpheres[1] ? Squad->HandAnchorSpheres[1]->GetComponentLocation() : PlayerLocation
	};

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = Squad->StanceMinions[i];
		const int32 ArmIndex = Cfg.bDualArmWhip ? (i % 2) : 0;
		const int32 LinkIndexInArm = Cfg.bDualArmWhip ? (i / 2) : i;
		const FVector CurrentLoc = MinionActor->GetActorLocation();

		if (UPrimitiveComponent* MinionPrim = Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()))
		{
			if (MinionPrim->IsSimulatingPhysics())
			{
				const FVector PhysicsVel = MinionPrim->GetPhysicsLinearVelocity();

				const float TargetZ = InitialMinionHeights.IsValidIndex(i) ? InitialMinionHeights[i] : ReferenceHeightZ;
				const float HeightError = CurrentLoc.Z - TargetZ;
				const float VerticalSpringAccel =
					(-HeightError * Cfg.VerticalHeightStiffness) -
					(PhysicsVel.Z * Cfg.VerticalHeightDamping) +
					980.0f;

				FVector TotalAccel(0.0f, 0.0f, VerticalSpringAccel);

				if (bAnySpinActive)
				{
					const FVector RadialOut = (CurrentLoc - PlayerLocation).GetSafeNormal2D();
					const FVector TangentDir = FVector::CrossProduct(FVector::UpVector, RadialOut) * DirectionSign;

					TotalAccel += (TangentDir * Cfg.TornadoWhipForce) +
						(RadialOut * (Cfg.TornadoWhipForce * 0.85f));
				}

				MinionPrim->AddForce(TotalAccel, NAME_None, true);

				const float Speed2D = PhysicsVel.Size2D();
				if (bAnySpinActive || Speed2D >= Cfg.MinPhysicsSpeedForDamage)
				{
					Squad->PerformWhipHitSweep(MinionActor, LinkIndexInArm, PrevMinionLocations[i], CurrentLoc);
				}
			}
		}

		if (Squad->bDebugDrawSlots)
		{
			const FColor ArmColor = bAnySpinActive
				? ((ArmIndex == 0) ? FColor::Magenta : FColor::Purple)
				: ((ArmIndex == 0) ? FColor::Orange : FColor::Cyan);

			DrawDebugLine(World, PrevDebugPos[ArmIndex], CurrentLoc, ArmColor, false, -1.0f, 0, 3.5f);
			DrawDebugSphere(World, CurrentLoc, Cfg.HitSphereRadius * 0.45f, 8, ArmColor, false, -1.0f);
		}

		PrevDebugPos[ArmIndex] = CurrentLoc;
		PrevMinionLocations[i] = CurrentLoc;
	}
}

// ============================================================================
// STANCE 3: UMinionStance_CarryRush IMPLEMENTATION (Uses StanceMinions)
// ============================================================================

void UMinionStance_CarryRush::OnEnterStance()
{
	if (!Squad) return;

	AActor* OwnerActor = Squad->GetOwner();
	if (!OwnerActor) return;

	CarryRushDirection = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	if (CarryRushDirection.IsNearlyZero())
	{
		CarryRushDirection = FVector::ForwardVector;
	}

	CarryStanceElapsedTime = 0.0f;
	CarryRushChargeElapsedTime = 0.0f;
	CurrentRushSpeed = 0.0f;
	bIsRushCharging = false;
	bIsCarryJumping = false;

	Squad->HitActorTimestamps.Reset();
	RushIgnoredEnemies.Empty();

	OwnerActor->OnActorHit.AddUniqueDynamic(Squad, &UMinionSquadComponent::OnPlayerRushActorHit);

	if (ACharacter* PlayerChar = Cast<ACharacter>(OwnerActor))
	{
		if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
		{
			if (!bPlayerMeshZModified)
			{
				CachedPlayerMeshBaseZ = MeshComp->GetRelativeLocation().Z;
				bPlayerMeshZModified = true;
			}
		}
	}

	UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
	const int32 MinionCount = Squad->PruneInvalidStanceMinions();

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = Squad->StanceMinions[i];
		IMinionAgentInterface* MinionInterface = Squad->StanceMinionInterfaces[i].GetInterface();

		if (OwnerRoot && MinionActor)
		{
			OwnerRoot->IgnoreActorWhenMoving(MinionActor, true);
		}
		if (UPrimitiveComponent* MinionRoot = MinionActor ? Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()) : nullptr)
		{
			MinionRoot->IgnoreActorWhenMoving(OwnerActor, true);
		}
		if (MinionInterface)
		{
			MinionInterface->SetCarryRushModeEnabled(true);
		}
	}

	Squad->BroadcastDebug(
		FString::Printf(TEXT("Entered CARRY TRANSPORT Stance (%d Reachable Minions)"), MinionCount),
		FColor::Cyan,
		2.5f);
}

void UMinionStance_CarryRush::RestorePlayerMovementSettings()
{
	if (bPlayerMovementModified && Squad)
	{
		if (ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement())
			{
				MoveComp->MaxWalkSpeed = CachedPlayerMaxWalkSpeed;
				MoveComp->MaxAcceleration = CachedPlayerMaxAcceleration;
			}
		}
		bPlayerMovementModified = false;
	}
}

void UMinionStance_CarryRush::RestorePlayerAirBraking()
{
	if (bPlayerAirBrakingModified && Squad)
	{
		if (ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner()))
		{
			if (UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement())
			{
				MoveComp->BrakingDecelerationFalling = CachedBrakingDecelFalling;
				MoveComp->FallingLateralFriction = CachedFallingLateralFriction;
			}
			PlayerChar->LandedDelegate.RemoveDynamic(Squad, &UMinionSquadComponent::OnPlayerCarryJumpLanded);
		}
		bPlayerAirBrakingModified = false;
	}
}

void UMinionStance_CarryRush::OnExitStance()
{
	if (!Squad) return;

	AActor* OwnerActor = Squad->GetOwner();
	UPrimitiveComponent* OwnerRoot = OwnerActor ? Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()) : nullptr;

	if (OwnerActor)
	{
		OwnerActor->OnActorHit.RemoveDynamic(Squad, &UMinionSquadComponent::OnPlayerRushActorHit);
	}

	RestorePlayerMovementSettings();

	if (!bIsCarryJumping)
	{
		RestorePlayerAirBraking();
		ReleaseCarriedJumpMinion();
	}

	if (bPlayerMeshZModified)
	{
		if (ACharacter* PlayerChar = Cast<ACharacter>(OwnerActor))
		{
			if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
			{
				FVector RelLoc = MeshComp->GetRelativeLocation();
				RelLoc.Z = CachedPlayerMeshBaseZ;
				MeshComp->SetRelativeLocation(RelLoc);
			}
		}
		bPlayerMeshZModified = false;
	}

	if (OwnerRoot)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredEnemy : RushIgnoredEnemies)
		{
			if (IgnoredEnemy.IsValid())
			{
				OwnerRoot->IgnoreActorWhenMoving(IgnoredEnemy.Get(), false);
			}
		}
	}
	RushIgnoredEnemies.Empty();

	bIsRushCharging = false;
	CurrentRushSpeed = 0.0f;

	// Release all minions EXCEPT the one being carried in mid-air during a Momentum Jump!
	for (int32 i = 0; i < Squad->StanceMinionInterfaces.Num(); ++i)
	{
		AActor* MinionActor = Squad->StanceMinions.IsValidIndex(i) ? Squad->StanceMinions[i].Get() : nullptr;
		if (bIsCarryJumping && MinionActor != nullptr && MinionActor == JumpCarriedMinion)
		{
			continue;
		}

		if (IMinionAgentInterface* MinionInterface = Squad->StanceMinionInterfaces[i].GetInterface())
		{
			MinionInterface->SetCarryRushModeEnabled(false);
		}
	}
}

void UMinionStance_CarryRush::ReleaseCarriedJumpMinion()
{
	if (!IsValid(JumpCarriedMinion))
	{
		JumpCarriedMinion = nullptr;
		return;
	}

	AActor* MinionToRelease = JumpCarriedMinion;
	JumpCarriedMinion = nullptr;

	// 1. Detach from the Player keeping world transform
	MinionToRelease->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 2. Place safely on the ground/NavMesh next to the player so it doesn't clip into a wall or floor
	if (Squad)
	{
		if (AActor* PlayerActor = Squad->GetOwner())
		{
			FVector SafeLandingPos = PlayerActor->GetActorLocation() - (PlayerActor->GetActorForwardVector().GetSafeNormal2D() * 65.0f);
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Squad->GetWorld()))
			{
				FNavLocation ProjectedNav;
				if (NavSys->ProjectPointToNavigation(SafeLandingPos, ProjectedNav, FVector(180.0f, 180.0f, 250.0f)))
				{
					SafeLandingPos = ProjectedNav.Location + FVector(0.0f, 0.0f, 45.0f);
				}
			}
			MinionToRelease->SetActorLocationAndRotation(
				SafeLandingPos,
				FRotator(0.0f, PlayerActor->GetActorRotation().Yaw, 0.0f),
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
	}

	// 3. Restore capsule collision and exit CarryRush mode so it resumes normal Follow behavior
	if (UPrimitiveComponent* MinionRoot = Cast<UPrimitiveComponent>(MinionToRelease->GetRootComponent()))
	{
		MinionRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MinionRoot->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionToRelease))
	{
		MinionInterface->SetCarryRushModeEnabled(false);
	}
}

void UMinionStance_CarryRush::OnSquadRosterChanged()
{
	if (!Squad) return;

	if (Squad->PruneInvalidStanceMinions() < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
	}
}

bool UMinionStance_CarryRush::StartRushCharge()
{
	if (!Squad) return false;

	AActor* OwnerActor = Squad->GetOwner();
	if (!OwnerActor) return false;

	CarryRushDirection = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	if (CarryRushDirection.IsNearlyZero())
	{
		CarryRushDirection = FVector::ForwardVector;
	}

	if (ACharacter* PlayerChar = Cast<ACharacter>(OwnerActor))
	{
		if (UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement())
		{
			if (!bPlayerMovementModified)
			{
				CachedPlayerMaxWalkSpeed = MoveComp->MaxWalkSpeed;
				CachedPlayerMaxAcceleration = MoveComp->MaxAcceleration;
				bPlayerMovementModified = true;
			}
			MoveComp->MaxAcceleration = 99999.0f;
		}
	}

	bIsRushCharging = true;
	CarryRushChargeElapsedTime = 0.0f;
	CurrentRushSpeed = FMath::Max(CurrentRushSpeed, Squad->CarryRushConfig.InitialSpeed);
	Squad->HitActorTimestamps.Reset();

	Squad->BroadcastDebug(TEXT("CARRY RUSH CHARGE ACTIVATED!"), FColor::Magenta, 2.0f);
	return true;
}

void UMinionStance_CarryRush::StopRushCharge(bool bExitCarryStance)
{
	bIsRushCharging = false;
	CarryRushChargeElapsedTime = 0.0f;
	CurrentRushSpeed = 0.0f;
	RestorePlayerMovementSettings();

	if (bExitCarryStance && Squad && Squad->GetSquadStance() == EMinionSquadStance::CarryRush)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
	}
}

bool UMinionStance_CarryRush::TriggerMomentumJump()
{
	if (!Squad || bIsCarryJumping) return false;

	ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner());
	if (!PlayerChar) return false;

	UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement();
	const FMinionCarryRushConfig& Cfg = Squad->CarryRushConfig;

	FVector JumpForwardDir = PlayerChar->GetActorForwardVector().GetSafeNormal2D();
	float HorizontalSpeed = 0.0f;

	if (bIsRushCharging)
	{
		JumpForwardDir = CarryRushDirection;
		const float EffectiveRushSpeed = FMath::Max(CurrentRushSpeed, Cfg.MinRushJumpForwardSpeed);
		HorizontalSpeed = EffectiveRushSpeed * Cfg.JumpMomentumMultiplier;
	}
	else
	{
		const FVector PlayerVel2D(PlayerChar->GetVelocity().X, PlayerChar->GetVelocity().Y, 0.0f);
		const float CurrentWalkSpeed = PlayerVel2D.Size();
		if (CurrentWalkSpeed > 20.0f)
		{
			JumpForwardDir = PlayerVel2D / CurrentWalkSpeed;
		}
		HorizontalSpeed = CurrentWalkSpeed * Cfg.JumpMomentumMultiplier;
	}

	RestorePlayerMovementSettings();

	if (MoveComp && !bPlayerAirBrakingModified)
	{
		CachedBrakingDecelFalling = MoveComp->BrakingDecelerationFalling;
		CachedFallingLateralFriction = MoveComp->FallingLateralFriction;
		MoveComp->BrakingDecelerationFalling = 0.0f;
		MoveComp->FallingLateralFriction = 0.0f;
		bPlayerAirBrakingModified = true;
	}

	// Pick 1 minion to carry along with the player during the jump so they have at least 1 minion where they land!
	ReleaseCarriedJumpMinion();
	if (Cfg.bCarryOneMinionDuringJump && Squad->PruneInvalidStanceMinions() > 0)
	{
		const FVector PlayerLoc = PlayerChar->GetActorLocation();
		int32 ClosestIdx = 0;
		float BestDistSq = MAX_flt;

		for (int32 i = 0; i < Squad->StanceMinions.Num(); ++i)
		{
			const float DistSq = FVector::DistSquared(Squad->StanceMinions[i]->GetActorLocation(), PlayerLoc);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				ClosestIdx = i;
			}
		}

		JumpCarriedMinion = Squad->StanceMinions[ClosestIdx];
		if (IsValid(JumpCarriedMinion))
		{
			// Disable capsule collision while attached in mid-air so the carried minion never snags on ledge lips
			if (UPrimitiveComponent* MinionRoot = Cast<UPrimitiveComponent>(JumpCarriedMinion->GetRootComponent()))
			{
				MinionRoot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}

			USkeletalMeshComponent* PlayerMesh = PlayerChar->GetMesh();
			if (!Cfg.CarriedMinionAttachSocket.IsNone() && PlayerMesh && PlayerMesh->DoesSocketExist(Cfg.CarriedMinionAttachSocket))
			{
				JumpCarriedMinion->AttachToComponent(
					PlayerMesh,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale,
					Cfg.CarriedMinionAttachSocket);
			}
			else
			{
				JumpCarriedMinion->AttachToComponent(
					PlayerChar->GetRootComponent(),
					FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				JumpCarriedMinion->SetActorRelativeLocation(Cfg.CarriedMinionLocalOffset);
				JumpCarriedMinion->SetActorRelativeRotation(FRotator::ZeroRotator);
			}
		}
	}

	bIsCarryJumping = true;
	PlayerChar->LandedDelegate.AddUniqueDynamic(Squad, &UMinionSquadComponent::OnPlayerCarryJumpLanded);

	const FVector LaunchVelocity = (JumpForwardDir * HorizontalSpeed) + FVector(0.0f, 0.0f, Cfg.JumpVerticalVelocity);
	PlayerChar->LaunchCharacter(LaunchVelocity, true, true);
	if (MoveComp)
	{
		MoveComp->SetMovementMode(MOVE_Falling);
	}

	Squad->OnCarryJumpTriggered.Broadcast(LaunchVelocity, HorizontalSpeed);
	Squad->BroadcastDebug(
		FString::Printf(TEXT("Jump Propelled! Carrying %s while %d minions wait behind."),
			*GetNameSafe(JumpCarriedMinion), FMath::Max(0, Squad->StanceMinions.Num() - 1)),
		FColor::Yellow,
		2.5f);

	// Switch squad back to Follow stance (OnExitStance skips JumpCarriedMinion until touchdown)
	Squad->SetSquadStance(EMinionSquadStance::Follow);
	return true;
}

void UMinionStance_CarryRush::HandlePlayerLanded(const FHitResult& Hit)
{
	bIsCarryJumping = false;
	RestorePlayerAirBraking();
	ReleaseCarriedJumpMinion();

	if (Squad && Squad->GetSquadStance() == EMinionSquadStance::Follow && Squad->FollowStanceInstance)
	{
		Squad->FollowStanceInstance->ForceImmediateEvaluation();
	}
}

void UMinionStance_CarryRush::HandlePlayerWallHit(AActor* OtherActor, const FHitResult& Hit)
{
	if (!bIsRushCharging || !Squad || !OtherActor) return;
	if (OtherActor->IsA<APawn>()) return;

	const FMinionCarryRushConfig& Cfg = Squad->CarryRushConfig;

	if (FMath::Abs(Hit.ImpactNormal.Z) < Cfg.WallNormalZThreshold)
	{
		if (FVector::DotProduct(CarryRushDirection, Hit.ImpactNormal) < -0.2f)
		{
			const float CrashSpeed = CurrentRushSpeed;
			if (Squad->bDebugDrawSlots && Squad->GetWorld())
			{
				DrawDebugSphere(Squad->GetWorld(), Hit.ImpactPoint, 45.0f, 16, FColor::Orange, false, 2.0f, 0, 4.0f);
			}

			Squad->BroadcastDebug(
				FString::Printf(TEXT("CARRY RUSH HIT WALL (%s) at Speed %.0f!"), *GetNameSafe(OtherActor), CrashSpeed),
				FColor::Orange,
				3.0f);

			if (ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner()))
			{
				if (Cfg.WallBounceBackStrength > 0.0f)
				{
					const FVector BounceVel = (Hit.ImpactNormal.GetSafeNormal2D() * Cfg.WallBounceBackStrength) + FVector(0.0f, 0.0f, 260.0f);
					PlayerChar->LaunchCharacter(BounceVel, true, true);
				}
			}

			Squad->OnCarryRushWallImpact.Broadcast(Hit.ImpactPoint, Hit.ImpactNormal, CrashSpeed, Hit);
			StopRushCharge(Cfg.bExitCarryStanceOnWallHit);
		}
	}
}

FVector UMinionStance_CarryRush::CalculatePalanquinSlotLocation(
	int32 MinionIdx,
	int32 TotalCount,
	const FVector& PlayerLoc,
	const FVector& ForwardDir,
	const FVector& RightDir) const
{
	const FMinionCarryRushConfig& Cfg = Squad->CarryRushConfig;

	const int32 Cols = FMath::Min(TotalCount, 4);
	const int32 Row = MinionIdx / 4;
	const int32 Col = MinionIdx % 4;
	const int32 ColsInThisRow = (Row == 0) ? Cols : FMath::Max(1, TotalCount - 4);

	const float ColCenterOffset = (ColsInThisRow - 1) * 0.5f;
	const float RowCenterOffset = (TotalCount > 4) ? 0.5f : 0.0f;
	const float HoneycombStagger = (Row % 2 == 1 && ColsInThisRow == Cols) ? (Cfg.PalanquinColSpacing * 0.25f) : 0.0f;

	const float LocalForward = Cfg.PalanquinForwardOffset - ((Row - RowCenterOffset) * Cfg.PalanquinRowSpacing);
	const float LocalRight = ((Col - ColCenterOffset) * Cfg.PalanquinColSpacing) + HoneycombStagger;

	float TargetZ = PlayerLoc.Z;
	if (const ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner()))
	{
		if (const UCapsuleComponent* PlayerCap = PlayerChar->GetCapsuleComponent())
		{
			const float PlayerBottomZ = PlayerLoc.Z - PlayerCap->GetScaledCapsuleHalfHeight();
			TargetZ = PlayerBottomZ + 45.0f;
		}
	}

	return FVector(
		PlayerLoc.X + (ForwardDir.X * LocalForward) + (RightDir.X * LocalRight),
		PlayerLoc.Y + (ForwardDir.Y * LocalForward) + (RightDir.Y * LocalRight),
		TargetZ);
}

void UMinionStance_CarryRush::PerformFrontEnemyBumperSweep(const FVector& SweepStart, const FVector& SweepEnd)
{
	if (!Squad) return;

	UWorld* World = Squad->GetWorld();
	AActor* PlayerActor = Squad->GetOwner();
	if (!World || !PlayerActor) return;

	const FMinionCarryRushConfig& Cfg = Squad->CarryRushConfig;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CarryRushEnemySweep), false, PlayerActor);
	QueryParams.AddIgnoredActor(PlayerActor);
	for (AActor* SquadMinion : Squad->ActiveMinions)
	{
		if (IsValid(SquadMinion))
		{
			QueryParams.AddIgnoredActor(SquadMinion);
		}
	}

	const FVector ExtendedEnd = SweepEnd + (CarryRushDirection * 45.0f);

	TArray<FHitResult> HitResults;
	const bool bHit = World->SweepMultiByChannel(
		HitResults,
		SweepStart,
		ExtendedEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Cfg.RamHitboxRadius),
		QueryParams);

	if (!bHit) return;

	UPrimitiveComponent* PlayerRoot = Cast<UPrimitiveComponent>(PlayerActor->GetRootComponent());
	const double CurrentTime = World->GetTimeSeconds();

	const float SpeedAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(Cfg.InitialSpeed, Cfg.MaxSpeed),
		FVector2D(0.0f, 1.0f),
		CurrentRushSpeed);
	const float RamDamage = Cfg.BaseRamDamage + (SpeedAlpha * Cfg.MaxSpeedBonusDamage);

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActor == PlayerActor || !HitActor->IsA<APawn>())
		{
			continue;
		}

		if (PlayerRoot)
		{
			PlayerRoot->IgnoreActorWhenMoving(HitActor, true);
			RushIgnoredEnemies.AddUnique(HitActor);
		}

		if (const double* LastHit = Squad->HitActorTimestamps.Find(HitActor))
		{
			if ((CurrentTime - *LastHit) < 0.4) continue;
		}
		Squad->HitActorTimestamps.Add(HitActor, CurrentTime);

		if (Squad->WhipConfig.bApplyEnginePointDamage)
		{
			APawn* InstigatorPawn = Cast<APawn>(PlayerActor);
			AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
			TSubclassOf<UDamageType> DmgType = Squad->WhipConfig.DamageTypeClass
				? Squad->WhipConfig.DamageTypeClass
				: TSubclassOf<UDamageType>(UDamageType::StaticClass());

			UGameplayStatics::ApplyPointDamage(
				HitActor,
				RamDamage,
				CarryRushDirection,
				Hit,
				InstigatorController,
				PlayerActor,
				DmgType);
		}

		if (ACharacter* HitEnemyChar = Cast<ACharacter>(HitActor))
		{
			const FVector ToEnemy = (HitEnemyChar->GetActorLocation() - PlayerActor->GetActorLocation()).GetSafeNormal2D();
			const FVector KnockDir = (CarryRushDirection * 0.5f + ToEnemy * 0.5f).GetSafeNormal2D();
			const FVector LaunchVel = (KnockDir * Cfg.EnemyKnockbackStrength) + FVector(0.0f, 0.0f, 380.0f);
			HitEnemyChar->LaunchCharacter(LaunchVel, true, true);
		}

		Squad->OnCarryRushEnemyHit.Broadcast(HitActor, RamDamage, CurrentRushSpeed, Hit);
	}
}

void UMinionStance_CarryRush::TickStance(float DeltaTime)
{
	if (!Squad) return;

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	const int32 MinionCount = Squad->PruneInvalidStanceMinions();

	if (!PlayerActor || !World || MinionCount < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
		return;
	}

	const FMinionCarryRushConfig& Cfg = Squad->CarryRushConfig;
	CarryStanceElapsedTime += DeltaTime;

	ACharacter* PlayerChar = Cast<ACharacter>(PlayerActor);
	UCharacterMovementComponent* MoveComp = PlayerChar ? PlayerChar->GetCharacterMovement() : nullptr;

	// 1. Detect Jump and immediately propel player forward while leaving minions behind in Follow stance
	if (!bIsCarryJumping && PlayerChar && MoveComp)
	{
		const bool bJumpStartedThisFrame = PlayerChar->bPressedJump ||
			(MoveComp->IsFalling() && PlayerChar->GetVelocity().Z > 50.0f);

		if (bJumpStartedThisFrame)
		{
			TriggerMomentumJump();
			return;
		}
	}

	// 2. Hoist the player's SkeletalMesh onto the minions' shoulders during GatherDuration
	if (PlayerChar && bPlayerMeshZModified)
	{
		if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
		{
			const float GatherAlpha = (Cfg.GatherDuration > SMALL_NUMBER)
				? FMath::Clamp(CarryStanceElapsedTime / Cfg.GatherDuration, 0.0f, 1.0f)
				: 1.0f;

			FVector RelLoc = MeshComp->GetRelativeLocation();
			RelLoc.Z = FMath::Lerp(CachedPlayerMeshBaseZ, CachedPlayerMeshBaseZ + Cfg.PlayerMeshLiftOffsetZ, GatherAlpha);
			MeshComp->SetRelativeLocation(RelLoc);
		}
	}

	// 3. Drive Grounded Rush via UCharacterMovementComponent
	if (bIsRushCharging)
	{
		CarryRushChargeElapsedTime += DeltaTime;

		if (Cfg.MaxRushDuration > 0.0f && CarryRushChargeElapsedTime >= Cfg.MaxRushDuration)
		{
			StopRushCharge(false);
		}
		else
		{
			PlayerActor->SetActorRotation(CarryRushDirection.Rotation());

			CurrentRushSpeed = FMath::Min(
				CurrentRushSpeed + (Cfg.Acceleration * DeltaTime),
				Cfg.MaxSpeed);

			const FVector StartPlayerLoc = PlayerActor->GetActorLocation();
			const FVector DesiredStep = CarryRushDirection * (CurrentRushSpeed * DeltaTime);

			PerformFrontEnemyBumperSweep(StartPlayerLoc, StartPlayerLoc + DesiredStep);

			if (MoveComp && PlayerChar)
			{
				MoveComp->MaxWalkSpeed = CurrentRushSpeed;
				MoveComp->Velocity = FVector(
					CarryRushDirection.X * CurrentRushSpeed,
					CarryRushDirection.Y * CurrentRushSpeed,
					MoveComp->Velocity.Z);
				PlayerChar->AddMovementInput(CarryRushDirection, 1.0f);
			}

			if (UCapsuleComponent* PlayerCap = PlayerChar ? PlayerChar->GetCapsuleComponent() : nullptr)
			{
				FCollisionQueryParams WallParams(SCENE_QUERY_STAT(CarryRushWallTrace), false, PlayerActor);
				for (AActor* SquadMinion : Squad->ActiveMinions)
				{
					if (IsValid(SquadMinion)) WallParams.AddIgnoredActor(SquadMinion);
				}
				for (const TWeakObjectPtr<AActor>& IgnoredEnemy : RushIgnoredEnemies)
				{
					if (IgnoredEnemy.IsValid()) WallParams.AddIgnoredActor(IgnoredEnemy.Get());
				}

				FHitResult WallHit;
				const float CapRadius = PlayerCap->GetScaledCapsuleRadius();
				const float CapHalfHeight = PlayerCap->GetScaledCapsuleHalfHeight() * 0.8f;
				if (World->SweepSingleByChannel(
					WallHit,
					StartPlayerLoc,
					StartPlayerLoc + (CarryRushDirection * 22.0f),
					FQuat::Identity,
					ECC_WorldStatic,
					FCollisionShape::MakeCapsule(CapRadius, CapHalfHeight),
					WallParams))
				{
					if (WallHit.bBlockingHit && FMath::Abs(WallHit.ImpactNormal.Z) < Cfg.WallNormalZThreshold)
					{
						HandlePlayerWallHit(WallHit.GetActor(), WallHit);
						return;
					}
				}
			}
		}
	}
	else
	{
		CurrentRushSpeed = PlayerActor->GetVelocity().Size2D();
	}

	// 4. Update Participating Minions in the Palanquin Underneath the Player
	const FVector UpdatedPlayerLoc = PlayerActor->GetActorLocation();
	const FVector PlayerVel2D(PlayerActor->GetVelocity().X, PlayerActor->GetVelocity().Y, 0.0f);

	FVector PalanquinForward = bIsRushCharging
		? CarryRushDirection
		: ((PlayerVel2D.SizeSquared() > 400.0f) ? PlayerVel2D.GetSafeNormal() : PlayerActor->GetActorForwardVector().GetSafeNormal2D());

	if (PalanquinForward.IsNearlyZero())
	{
		PalanquinForward = FVector::ForwardVector;
	}

	const FVector PalanquinRight = FVector::CrossProduct(FVector::UpVector, PalanquinForward).GetSafeNormal();
	const FRotator MinionTargetRot = PalanquinForward.Rotation();
	const float SlotInterpSpeed = bIsRushCharging ? 45.0f : Cfg.TransportFollowInterpSpeed;

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = Squad->StanceMinions[i];
		const FVector TargetCarrySlot = CalculatePalanquinSlotLocation(i, MinionCount, UpdatedPlayerLoc, PalanquinForward, PalanquinRight);
		const FVector NewMinionLoc = FMath::VInterpTo(MinionActor->GetActorLocation(), TargetCarrySlot, DeltaTime, SlotInterpSpeed);

		MinionActor->SetActorLocationAndRotation(NewMinionLoc, MinionTargetRot, false);

		if (Squad->bDebugDrawSlots)
		{
			DrawDebugSphere(World, TargetCarrySlot, 16.0f, 8, bIsRushCharging ? FColor::Magenta : FColor::Cyan, false, -1.0f);
		}
	}
}

// ============================================================================
// MAIN UMinionSquadComponent IMPLEMENTATION
// ============================================================================

UMinionSquadComponent::UMinionSquadComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetRelativeLocation(FVector(-160.0f, 0.0f, 0.0f));
}

void UMinionSquadComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(false);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return;

	if (GetRelativeLocation().SizeSquared2D() < FMath::Square(PlayerEvadeRadius + 20.0f))
	{
		SetRelativeLocation(FVector(-(PlayerEvadeRadius + 65.0f), 0.0f, 0.0f));
	}

	AvoidanceSphere = NewObject<USphereComponent>(OwnerActor, TEXT("PlayerMinionAvoidanceSphere"));
	if (AvoidanceSphere)
	{
		AvoidanceSphere->SetupAttachment(OwnerActor->GetRootComponent());
		AvoidanceSphere->SetSphereRadius(PlayerEvadeRadius);
		AvoidanceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		AvoidanceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		AvoidanceSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		AvoidanceSphere->RegisterComponent();
		AvoidanceSphere->OnComponentBeginOverlap.AddDynamic(this, &UMinionSquadComponent::OnPlayerAvoidanceOverlap);
	}

	for (int32 ArmIdx = 0; ArmIdx < 2; ++ArmIdx)
	{
		const FName SphereName = (ArmIdx == 0) ? TEXT("RightHandChainAnchor") : TEXT("LeftHandChainAnchor");
		USphereComponent* AnchorSphere = NewObject<USphereComponent>(OwnerActor, SphereName);
		if (AnchorSphere)
		{
			AnchorSphere->SetupAttachment(OwnerActor->GetRootComponent());
			AnchorSphere->SetSphereRadius(10.0f);
			AnchorSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			AnchorSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
			AnchorSphere->SetSimulatePhysics(false);
			AnchorSphere->RegisterComponent();
			HandAnchorSpheres[ArmIdx] = AnchorSphere;
		}
	}
	UpdateHandAnchorTransforms(0.0f);

	const int32 PoolSize = FMath::Max(MaxMinions, 16);
	ConstraintPool.Reserve(PoolSize);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		const FName ConstraintName = *FString::Printf(TEXT("PooledMinionChainConstraint_%d"), i);
		UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(OwnerActor, ConstraintName);
		if (Constraint)
		{
			Constraint->SetupAttachment(OwnerActor->GetRootComponent());
			Constraint->SetDisableCollision(true);
			Constraint->RegisterComponent();
			ConstraintPool.Add(Constraint);
		}
	}

	FollowStanceInstance = NewObject<UMinionStance_Follow>(this, TEXT("Stance_Follow"));
	FollowStanceInstance->Initialize(this);

	WhipStanceInstance = NewObject<UMinionStance_ChaosWhip>(this, TEXT("Stance_ChaosWhip"));
	WhipStanceInstance->Initialize(this);

	CarryRushStanceInstance = NewObject<UMinionStance_CarryRush>(this, TEXT("Stance_CarryRush"));
	CarryRushStanceInstance->Initialize(this);

	ActiveStanceObject = FollowStanceInstance;
	ActiveStanceObject->OnEnterStance();

	TArray<TObjectPtr<AActor>> Preassigned = MoveTemp(ActiveMinions);
	ActiveMinions.Empty();
	ActiveMinionInterfaces.Empty();
	for (AActor* Candidate : Preassigned)
	{
		RegisterMinion(Candidate);
	}

	if (bAutoRegisterMinionsOnBeginPlay && ActiveMinions.IsEmpty())
	{
		AutoRegisterMinionsInWorld();
	}
}

void UMinionSquadComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ActiveStanceObject)
	{
		ActiveStanceObject->OnExitStance();
	}

	if (CarryRushStanceInstance)
	{
		CarryRushStanceInstance->RestorePlayerAirBraking();
		CarryRushStanceInstance->ReleaseCarriedJumpMinion();
	}

	for (UPhysicsConstraintComponent* Constraint : ConstraintPool)
	{
		if (IsValid(Constraint))
		{
			Constraint->BreakConstraint();
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UMinionSquadComponent::BroadcastDebug(const FString& Message, FColor ScreenColor, float Duration) const
{
	OnSquadDebugMessage.Broadcast(Message);

	if (bPrintDebugToScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, Duration, ScreenColor, FString::Printf(TEXT("[PikminSquad] %s"), *Message));
	}
	UE_LOG(LogTemp, Log, TEXT("[PikminSquad] %s"), *Message);
}

int32 UMinionSquadComponent::PruneInvalidMinions()
{
	for (int32 i = ActiveMinions.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(ActiveMinions[i]) || !ActiveMinionInterfaces.IsValidIndex(i) || !ActiveMinionInterfaces[i].GetInterface())
		{
			ActiveMinions.RemoveAtSwap(i);
			if (ActiveMinionInterfaces.IsValidIndex(i))
			{
				ActiveMinionInterfaces.RemoveAtSwap(i);
			}
		}
	}
	return ActiveMinions.Num();
}

int32 UMinionSquadComponent::PruneInvalidStanceMinions()
{
	for (int32 i = StanceMinions.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(StanceMinions[i]) || !StanceMinionInterfaces.IsValidIndex(i) || !StanceMinionInterfaces[i].GetInterface())
		{
			StanceMinions.RemoveAtSwap(i);
			if (StanceMinionInterfaces.IsValidIndex(i))
			{
				StanceMinionInterfaces.RemoveAtSwap(i);
			}
		}
	}
	return StanceMinions.Num();
}

// ============================================================================
// REACHABILITY & MINIMUM COUNT GATE ( >= 4 Reachable Minions Required )
// ============================================================================

bool UMinionSquadComponent::IsPlayerGroundedOnNavMesh(UNavigationSystemV1* NavSys, FVector& OutPlayerNavLoc) const
{
	const AActor* PlayerActor = GetOwner();
	if (!NavSys || !PlayerActor) return false;

	// 1. Reject if the player is currently jumping/falling in mid-air
	if (const ACharacter* PlayerChar = Cast<ACharacter>(PlayerActor))
	{
		if (const UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement())
		{
			if (MoveComp->IsFalling())
			{
				return false;
			}
		}
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (!NavData) return false;

	// 2. Reject if the player's feet are not standing on a valid NavMesh surface
	FNavLocation ProjectedPlayerNav;
	const FVector PlayerFeetExtent(75.0f, 75.0f, 125.0f);
	if (!NavSys->ProjectPointToNavigation(PlayerActor->GetActorLocation(), ProjectedPlayerNav, PlayerFeetExtent, NavData))
	{
		return false;
	}

	OutPlayerNavLoc = ProjectedPlayerNav.Location;
	return true;
}

bool UMinionSquadComponent::CanMinionReachNavLocation(
	AActor* MinionActor,
	UNavigationSystemV1* NavSys,
	const FVector& TargetNavLoc) const
{
	if (!IsValid(MinionActor) || !NavSys) return false;

	// If the minion is already actively attached to the Whip or Carry palanquin right next to the player, count it as reachable
	if (const IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
	{
		const EMinionState State = MinionInterface->GetMinionState();
		if (State == EMinionState::InAttackWhip || State == EMinionState::InCarryRush)
		{
			return true;
		}
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (!NavData) return false;

	FNavLocation ProjectedMinionNav;
	const FVector MinionExtent(150.0f, 150.0f, 300.0f);
	if (!NavSys->ProjectPointToNavigation(MinionActor->GetActorLocation(), ProjectedMinionNav, MinionExtent, NavData))
	{
		return false;
	}

	// Synchronous complete-path check (Partial Paths strictly disabled!)
	FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(*NavData, MinionActor, nullptr);
	FPathFindingQuery PathQuery(MinionActor, *NavData, ProjectedMinionNav.Location, TargetNavLoc, QueryFilter);
	PathQuery.SetAllowPartialPaths(false);

	return NavSys->TestPathSync(PathQuery, EPathFindingMode::Regular);
}

int32 UMinionSquadComponent::GatherReachableMinionsForStance(
	TArray<TObjectPtr<AActor>>& OutReachableMinions,
	TArray<TScriptInterface<IMinionAgentInterface>>& OutReachableInterfaces) const
{
	OutReachableMinions.Empty();
	OutReachableInterfaces.Empty();

	UWorld* World = GetWorld();
	if (!World) return 0;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector PlayerNavLoc = FVector::ZeroVector;

	if (!IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc))
	{
		return 0;
	}

	for (int32 i = 0; i < ActiveMinions.Num(); ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		if (!IsValid(MinionActor) || !ActiveMinionInterfaces.IsValidIndex(i) || !ActiveMinionInterfaces[i].GetInterface())
		{
			continue;
		}

		if (CanMinionReachNavLocation(MinionActor, NavSys, PlayerNavLoc))
		{
			OutReachableMinions.Add(MinionActor);
			OutReachableInterfaces.Add(ActiveMinionInterfaces[i]);
		}
	}

	return OutReachableMinions.Num();
}

bool UMinionSquadComponent::CanEnterSpecialStance(int32& OutReachableMinionCount) const
{
	TArray<TObjectPtr<AActor>> ReachableMinions;
	TArray<TScriptInterface<IMinionAgentInterface>> ReachableInterfaces;
	OutReachableMinionCount = GatherReachableMinionsForStance(ReachableMinions, ReachableInterfaces);
	return OutReachableMinionCount >= MinReachableMinionsForSpecialStance;
}

bool UMinionSquadComponent::SetSquadStance(EMinionSquadStance NewStance)
{
	if (CurrentStance == NewStance && ActiveStanceObject != nullptr)
	{
		return true;
	}

	// Gate special stances (AttackWhip and CarryRush):
	// Only minions with a complete path to the player can join, and at least MinReachableMinionsForSpecialStance (4) are required!
	TArray<TObjectPtr<AActor>> ValidReachableMinions;
	TArray<TScriptInterface<IMinionAgentInterface>> ValidReachableInterfaces;

	if (NewStance != EMinionSquadStance::Follow)
	{
		PruneInvalidMinions();
		const int32 ReachableCount = GatherReachableMinionsForStance(ValidReachableMinions, ValidReachableInterfaces);

		if (ReachableCount < MinReachableMinionsForSpecialStance)
		{
			BroadcastDebug(
				FString::Printf(TEXT("Cannot shift stance: Only %d/%d minions can reach player (Need at least %d)!"),
					ReachableCount, ActiveMinions.Num(), MinReachableMinionsForSpecialStance),
				FColor::Red,
				2.5f);
			return false;
		}
	}

	if (ActiveStanceObject)
	{
		ActiveStanceObject->OnExitStance();
	}

	CurrentStance = NewStance;

	if (CurrentStance == EMinionSquadStance::Follow)
	{
		StanceMinions.Empty();
		StanceMinionInterfaces.Empty();
		ActiveStanceObject = FollowStanceInstance;
	}
	else
	{
		// Populate StanceMinions with ONLY the reachable minions!
		StanceMinions = MoveTemp(ValidReachableMinions);
		StanceMinionInterfaces = MoveTemp(ValidReachableInterfaces);

		// Stop any unreachable minions so they remain waiting calmly on their island
		for (AActor* MinionActor : ActiveMinions)
		{
			if (IsValid(MinionActor) && !StanceMinions.Contains(MinionActor))
			{
				if (APawn* MinionPawn = Cast<APawn>(MinionActor))
				{
					if (AAIController* AICon = Cast<AAIController>(MinionPawn->GetController()))
					{
						AICon->StopMovement();
					}
				}
			}
		}

		ActiveStanceObject = (CurrentStance == EMinionSquadStance::AttackWhip)
			? Cast<UMinionStanceBase>(WhipStanceInstance)
			: Cast<UMinionStanceBase>(CarryRushStanceInstance);
	}

	if (ActiveStanceObject)
	{
		SetComponentTickEnabled(ActiveStanceObject->RequiresComponentTick());
		ActiveStanceObject->OnEnterStance();
	}

	OnSquadStanceChanged.Broadcast(CurrentStance);
	return true;
}

EMinionSquadStance UMinionSquadComponent::ToggleSquadStance()
{
	SetSquadStance(CurrentStance == EMinionSquadStance::Follow
		? EMinionSquadStance::AttackWhip
		: EMinionSquadStance::Follow);
	return CurrentStance;
}

EMinionSquadStance UMinionSquadComponent::ToggleCarryStance()
{
	SetSquadStance(CurrentStance == EMinionSquadStance::CarryRush
		? EMinionSquadStance::Follow
		: EMinionSquadStance::CarryRush);
	return CurrentStance;
}

void UMinionSquadComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (ActiveStanceObject)
	{
		ActiveStanceObject->TickStance(DeltaTime);
	}
}

// ============================================================================
// PUBLIC WHIP & CARRY RUSH FACADE DELEGATION
// ============================================================================

bool UMinionSquadComponent::StartWhipSpinAttack(bool bAutoEnterAttackStance)
{
	if (PruneInvalidMinions() == 0) return false;

	if (CurrentStance != EMinionSquadStance::AttackWhip)
	{
		if (!bAutoEnterAttackStance) return false;
		if (!SetSquadStance(EMinionSquadStance::AttackWhip))
		{
			return false;
		}
	}

	return WhipStanceInstance ? WhipStanceInstance->StartBurstSpin() : false;
}

void UMinionSquadComponent::SetContinuousTornadoSpin(bool bEnableContinuousSpin, bool bAutoEnterAttackStance)
{
	if (bEnableContinuousSpin)
	{
		if (PruneInvalidMinions() == 0) return;

		if (CurrentStance != EMinionSquadStance::AttackWhip)
		{
			if (!bAutoEnterAttackStance) return;
			if (!SetSquadStance(EMinionSquadStance::AttackWhip))
			{
				return;
			}
		}
	}

	if (WhipStanceInstance)
	{
		WhipStanceInstance->SetContinuousSpin(bEnableContinuousSpin);
	}
}

bool UMinionSquadComponent::IsWhipSpinAttacking() const
{
	return (CurrentStance == EMinionSquadStance::AttackWhip && WhipStanceInstance)
		? WhipStanceInstance->IsSpinning()
		: false;
}

bool UMinionSquadComponent::IsWalkingIntoWhipFormation() const
{
	return (CurrentStance == EMinionSquadStance::AttackWhip && WhipStanceInstance)
		? WhipStanceInstance->IsWalkingIntoFormation()
		: false;
}

bool UMinionSquadComponent::StartCarryRushCharge(bool bAutoEnterCarryStance)
{
	if (PruneInvalidMinions() == 0) return false;

	if (CurrentStance != EMinionSquadStance::CarryRush)
	{
		if (!bAutoEnterCarryStance) return false;
		if (!SetSquadStance(EMinionSquadStance::CarryRush))
		{
			return false;
		}
	}

	return CarryRushStanceInstance ? CarryRushStanceInstance->StartRushCharge() : false;
}

void UMinionSquadComponent::StopCarryRushCharge(bool bExitCarryStance)
{
	if (CarryRushStanceInstance)
	{
		CarryRushStanceInstance->StopRushCharge(bExitCarryStance);
	}
}

void UMinionSquadComponent::SetCarryRushChargeActive(bool bActive, bool bAutoEnterCarryStance)
{
	if (bActive)
	{
		StartCarryRushCharge(bAutoEnterCarryStance);
	}
	else
	{
		StopCarryRushCharge(false);
	}
}

bool UMinionSquadComponent::TriggerCarryJump()
{
	return (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
		? CarryRushStanceInstance->TriggerMomentumJump()
		: false;
}

bool UMinionSquadComponent::StartCarryRushSpell()
{
	return StartCarryRushCharge(true);
}

void UMinionSquadComponent::StopCarryRushSpell()
{
	StopCarryRushCharge(true);
}

bool UMinionSquadComponent::IsCarryRushCharging() const
{
	return (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
		? CarryRushStanceInstance->IsRushCharging()
		: false;
}

bool UMinionSquadComponent::IsCarryJumping() const
{
	return (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
		? CarryRushStanceInstance->IsCarryJumping()
		: false;
}

float UMinionSquadComponent::GetCurrentCarryRushSpeed() const
{
	return (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
		? CarryRushStanceInstance->GetCurrentRushSpeed()
		: 0.0f;
}

void UMinionSquadComponent::OnPlayerCarryJumpLanded(const FHitResult& Hit)
{
	if (CarryRushStanceInstance)
	{
		CarryRushStanceInstance->HandlePlayerLanded(Hit);
	}
}

void UMinionSquadComponent::OnPlayerRushActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	if (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
	{
		CarryRushStanceInstance->HandlePlayerWallHit(OtherActor, Hit);
	}
}

// ============================================================================
// SHARED ROSTER & HELPER FUNCTIONS
// ============================================================================

void UMinionSquadComponent::AutoRegisterMinionsInWorld()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor) return;

	TArray<AActor*> FoundMinions;
	UGameplayStatics::GetAllActorsWithInterface(World, UMinionAgentInterface::StaticClass(), FoundMinions);

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	FoundMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared(B.GetActorLocation(), PlayerLoc);
	});

	for (AActor* Minion : FoundMinions)
	{
		if (ActiveMinions.Num() >= MaxMinions) break;
		RegisterMinion(Minion);
	}
}

bool UMinionSquadComponent::RegisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor)) return false;

	IMinionAgentInterface* InterfacePtr = Cast<IMinionAgentInterface>(MinionActor);
	if (!InterfacePtr) return false;

	if (ActiveMinions.Num() >= MaxMinions || ActiveMinions.Contains(MinionActor))
	{
		return false;
	}

	const int32 NewSlotIndex = ActiveMinions.Add(MinionActor);

	TScriptInterface<IMinionAgentInterface> ScriptInterface;
	ScriptInterface.SetObject(MinionActor);
	ScriptInterface.SetInterface(InterfacePtr);
	ActiveMinionInterfaces.Add(ScriptInterface);

	// If currently in AttackWhip or CarryRush, only add to StanceMinions if this new minion can reach the player
	if (CurrentStance != EMinionSquadStance::Follow)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FVector PlayerNavLoc = FVector::ZeroVector;
		if (IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc) && CanMinionReachNavLocation(MinionActor, NavSys, PlayerNavLoc))
		{
			StanceMinions.Add(MinionActor);
			StanceMinionInterfaces.Add(ScriptInterface);
		}
	}

	if (ActiveStanceObject)
	{
		ActiveStanceObject->OnSquadRosterChanged();
	}

	OnMinionRegistered.Broadcast(MinionActor, NewSlotIndex, ActiveMinions.Num());
	return true;
}

void UMinionSquadComponent::UnregisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor)) return;

	const int32 FoundIdx = ActiveMinions.IndexOfByKey(MinionActor);
	if (FoundIdx != INDEX_NONE)
	{
		if (IMinionAgentInterface* MinionInterface = ActiveMinionInterfaces[FoundIdx].GetInterface())
		{
			MinionInterface->SetWhipModeEnabled(false);
			MinionInterface->SetCarryRushModeEnabled(false);
		}

		ActiveMinions.RemoveAtSwap(FoundIdx);
		ActiveMinionInterfaces.RemoveAtSwap(FoundIdx);

		const int32 StanceIdx = StanceMinions.IndexOfByKey(MinionActor);
		if (StanceIdx != INDEX_NONE)
		{
			StanceMinions.RemoveAtSwap(StanceIdx);
			StanceMinionInterfaces.RemoveAtSwap(StanceIdx);
		}

		if (ActiveStanceObject)
		{
			ActiveStanceObject->OnSquadRosterChanged();
		}

		OnMinionUnregistered.Broadcast(MinionActor, ActiveMinions.Num());
	}
}

void UMinionSquadComponent::OnPlayerAvoidanceOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (CurrentStance != EMinionSquadStance::Follow) return;

	AActor* PlayerActor = GetOwner();
	if (!PlayerActor || !IsValid(OtherActor) || OtherActor == PlayerActor) return;

	if (IMinionAgentInterface* Minion = Cast<IMinionAgentInterface>(OtherActor))
	{
		if (Minion->GetMinionState() != EMinionState::TraversingNavLink)
		{
			Minion->EvadePlayer(
				PlayerActor->GetActorLocation(),
				PlayerActor->GetVelocity(),
				PlayerActor->GetActorRightVector());
		}
	}
}

void UMinionSquadComponent::UpdateHandAnchorTransforms(float ExtraOrbitYaw)
{
	if (HandAnchorSpheres[0])
	{
		const FVector RightPos = WhipConfig.RightArmLocalOffset.RotateAngleAxis(ExtraOrbitYaw, FVector::UpVector);
		const FRotator RightRot(0.0f, WhipConfig.RightArmRestYawOffset + ExtraOrbitYaw, 0.0f);
		HandAnchorSpheres[0]->SetRelativeLocationAndRotation(RightPos, RightRot);
	}

	if (HandAnchorSpheres[1])
	{
		const FVector BaseLeftOffset = WhipConfig.bDualArmWhip ? WhipConfig.LeftArmLocalOffset : WhipConfig.RightArmLocalOffset;
		const float BaseLeftYaw = WhipConfig.bDualArmWhip ? WhipConfig.LeftArmRestYawOffset : WhipConfig.RightArmRestYawOffset;

		const FVector LeftPos = BaseLeftOffset.RotateAngleAxis(ExtraOrbitYaw, FVector::UpVector);
		const FRotator LeftRot(0.0f, BaseLeftYaw + ExtraOrbitYaw, 0.0f);
		HandAnchorSpheres[1]->SetRelativeLocationAndRotation(LeftPos, LeftRot);
	}
}

FVector UMinionSquadComponent::CalculateWhipLinkWorldTarget(int32 StanceMinionIndex) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor) return GetComponentLocation();

	const int32 ArmIndex = WhipConfig.bDualArmWhip ? (StanceMinionIndex % 2) : 0;
	const int32 LinkIndexInArm = WhipConfig.bDualArmWhip ? (StanceMinionIndex / 2) : StanceMinionIndex;
	USphereComponent* HandSphere = HandAnchorSpheres[ArmIndex];

	const FRotator OutwardArmRot = HandSphere ? HandSphere->GetComponentRotation() : OwnerActor->GetActorRotation();
	const FVector HandWorldLoc = HandSphere ? HandSphere->GetComponentLocation() : OwnerActor->GetActorLocation();
	const float ChainDist = WhipConfig.StartOffsetFromArm + (LinkIndexInArm * WhipConfig.LinkSpacing);

	return HandWorldLoc + (OutwardArmRot.Vector() * ChainDist);
}

void UMinionSquadComponent::SortStanceMinionsForWhipArms()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || PruneInvalidStanceMinions() <= 1) return;

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	const FVector PlayerRight = OwnerActor->GetActorRightVector();

	if (!WhipConfig.bDualArmWhip)
	{
		StanceMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared2D(B.GetActorLocation(), PlayerLoc);
		});
	}
	else
	{
		TArray<TObjectPtr<AActor>> SortedBySide = StanceMinions;
		SortedBySide.Sort([&PlayerLoc, &PlayerRight](const AActor& A, const AActor& B)
		{
			const float DotA = FVector::DotProduct(A.GetActorLocation() - PlayerLoc, PlayerRight);
			const float DotB = FVector::DotProduct(B.GetActorLocation() - PlayerLoc, PlayerRight);
			return DotA > DotB;
		});

		const int32 Total = SortedBySide.Num();
		const int32 RightCount = (Total + 1) / 2;

		TArray<TObjectPtr<AActor>, TInlineAllocator<8>> RightArmPool;
		TArray<TObjectPtr<AActor>, TInlineAllocator<8>> LeftArmPool;

		for (int32 i = 0; i < Total; ++i)
		{
			if (i < RightCount) RightArmPool.Add(SortedBySide[i]);
			else LeftArmPool.Add(SortedBySide[i]);
		}

		const FVector RightAnchor = HandAnchorSpheres[0] ? HandAnchorSpheres[0]->GetComponentLocation() : PlayerLoc;
		const FVector LeftAnchor = HandAnchorSpheres[1] ? HandAnchorSpheres[1]->GetComponentLocation() : PlayerLoc;

		RightArmPool.Sort([&RightAnchor](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), RightAnchor) < FVector::DistSquared2D(B.GetActorLocation(), RightAnchor);
		});

		LeftArmPool.Sort([&LeftAnchor](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), LeftAnchor) < FVector::DistSquared2D(B.GetActorLocation(), LeftAnchor);
		});

		StanceMinions.Empty(Total);
		const int32 MaxArmLen = FMath::Max(RightArmPool.Num(), LeftArmPool.Num());
		for (int32 LinkIdx = 0; LinkIdx < MaxArmLen; ++LinkIdx)
		{
			if (LinkIdx < RightArmPool.Num()) StanceMinions.Add(RightArmPool[LinkIdx]);
			if (LinkIdx < LeftArmPool.Num()) StanceMinions.Add(LeftArmPool[LinkIdx]);
		}
	}

	// Rebuild the 1:1 StanceMinionInterfaces cache to match the sorted StanceMinions order
	StanceMinionInterfaces.SetNum(StanceMinions.Num());
	for (int32 i = 0; i < StanceMinions.Num(); ++i)
	{
		AActor* MinionActor = StanceMinions[i];
		StanceMinionInterfaces[i].SetObject(MinionActor);
		StanceMinionInterfaces[i].SetInterface(Cast<IMinionAgentInterface>(MinionActor));
	}
}

void UMinionSquadComponent::PerformWhipHitSweep(AActor* MinionActor, int32 LinkIndexInArm, const FVector& StartLoc, const FVector& EndLoc)
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwner();
	if (!World || !PlayerActor || !MinionActor) return;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MinionWhipSweep), false, PlayerActor);
	QueryParams.AddIgnoredActor(PlayerActor);
	for (AActor* SquadMinion : ActiveMinions)
	{
		if (IsValid(SquadMinion)) QueryParams.AddIgnoredActor(SquadMinion);
	}

	TArray<FHitResult> HitResults;
	const bool bHit = World->SweepMultiByChannel(
		HitResults,
		StartLoc,
		EndLoc,
		FQuat::Identity,
		WhipConfig.HitTraceChannel,
		FCollisionShape::MakeSphere(WhipConfig.HitSphereRadius),
		QueryParams);

	if (!bHit) return;

	const double CurrentTime = World->GetTimeSeconds();

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor)) continue;

		if (const double* LastHitTime = HitActorTimestamps.Find(HitActor))
		{
			if ((CurrentTime - *LastHitTime) < static_cast<double>(WhipConfig.HitCooldownPerTarget))
			{
				continue;
			}
		}

		HitActorTimestamps.Add(HitActor, CurrentTime);

		const float CalculatedDamage = WhipConfig.BaseDamage + (LinkIndexInArm * WhipConfig.BonusDamagePerLink);
		const FVector ShotDirection = (EndLoc - StartLoc).GetSafeNormal();

		if (WhipConfig.bApplyEnginePointDamage)
		{
			APawn* InstigatorPawn = Cast<APawn>(PlayerActor);
			AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
			TSubclassOf<UDamageType> DmgType = WhipConfig.DamageTypeClass
				? WhipConfig.DamageTypeClass
				: TSubclassOf<UDamageType>(UDamageType::StaticClass());

			UGameplayStatics::ApplyPointDamage(
				HitActor,
				CalculatedDamage,
				ShotDirection,
				Hit,
				InstigatorController,
				MinionActor,
				DmgType);
		}

		OnWhipDamageHit.Broadcast(HitActor, MinionActor, CalculatedDamage, LinkIndexInArm, Hit);

		if (bDebugDrawSlots)
		{
			DrawDebugSphere(World, Hit.ImpactPoint, 28.0f, 12, FColor::Red, false, 0.8f, 0, 2.5f);
		}
	}
}



FVector UMinionSquadComponent::CalculateSlotWorldLocation(int32 SlotIndex) const
{
	constexpr int32 ColumnsPerRow = 4;
	const int32 Row = SlotIndex / ColumnsPerRow;
	const int32 Col = SlotIndex % ColumnsPerRow;

	const float CenterOffset = (ColumnsPerRow - 1) * 0.5f;
	const float NormalizedCol = FMath::Abs(Col - CenterOffset);
	const float CrescentPullback = (NormalizedCol > 1.0f) ? 25.0f : 0.0f;

	const float LocalX = -(Row * RowSpacing) - CrescentPullback;
	const float RowStagger = (Row % 2 == 1) ? (ColumnSpacing * 0.35f) : 0.0f;
	const float LocalY = ((Col - CenterOffset) * ColumnSpacing) + RowStagger;

	return GetComponentTransform().TransformPosition(FVector(LocalX, LocalY, 0.0f));
}