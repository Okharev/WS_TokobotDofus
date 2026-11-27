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
#include "HitReactableInterface.h"
#include "HitFeedbackSubsystem.h"
#include "HeartHealthComponent.h"
#include "MinionThrowTargetComponent.h"
#include "NavFilters/NavigationQueryFilter.h"

// ============================================================================
// STANCE 1: UMinionStance_Follow IMPLEMENTATION
// ============================================================================

void UMinionStance_Follow::OnEnterStance()
{
	if (!Squad)
	{
		return;
	}

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	FVector StartNavLoc = PlayerActor ? PlayerActor->GetActorLocation() : Squad->GetComponentLocation();
	const FVector StartFwd = PlayerActor ? PlayerActor->GetActorForwardVector() : Squad->GetForwardVector();

	bHasValidWaitingAnchor = false;
	if (World)
	{
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation GroundNavLoc;
			if (NavSys->ProjectPointToNavigation(StartNavLoc, GroundNavLoc, FVector(120.0f, 120.0f, 250.0f)))
			{
				StartNavLoc = GroundNavLoc.Location;
				bHasValidWaitingAnchor = true;
			}
		}
	}

	FormationSolver.Reset(StartNavLoc, StartFwd);
	WaitingGroupAnchorTransform = FTransform(StartFwd.Rotation(), StartNavLoc);
	LastGroundedPlayerNavLoc = StartNavLoc;
	LastAnchorLocation = StartNavLoc;
	LastAnchorRotation = StartFwd.Rotation();
	LastFormationMode = FormationSolver.GetCurrentMode();
	bSuppressingLateralEvade = false;
	bForceNextUpdate = true;
	LastReachableMinionCount = -1;
	StickySlotAssignments.Reset();
	ProgressTrackers.Reset();

	if (World)
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
	if (Squad)
	{
		// Restore normal crowd separation before leaving Follow stance
		for (int32 i = 0; i < Squad->ActiveMinionInterfaces.Num(); ++i)
		{
			if (IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[i].GetInterface())
			{
				MinionInterface->SetTightCorridorMode(false);
			}
		}

		if (UWorld* World = Squad->GetWorld())
		{
			World->GetTimerManager().ClearTimer(FollowTimerHandle);
		}
	}

	bSuppressingLateralEvade = false;
	ProgressTrackers.Reset();
}

void UMinionStance_Follow::OnSquadRosterChanged()
{
	StickySlotAssignments.Reset();
	ForceImmediateEvaluation();
}

void UMinionStance_Follow::ForceImmediateEvaluation()
{
	bForceNextUpdate = true;
	EvaluateFormationAndAvoidance();
}

FVector UMinionStance_Follow::CalculateSlotAroundTransform(int32 SlotIndex, const FTransform& AnchorTransform) const
{
	if (!Squad)
	{
		return AnchorTransform.GetLocation();
	}

	constexpr int32 ColumnsPerRow = 4;
	const int32 Row = SlotIndex / ColumnsPerRow;
	const int32 Col = SlotIndex % ColumnsPerRow;

	constexpr float CenterOffset = (ColumnsPerRow - 1) * 0.5f;
	const float NormalizedCol = FMath::Abs(Col - CenterOffset);
	const float CrescentPullback = (NormalizedCol > 1.0f) ? 25.0f : 0.0f;

	const float LocalX = -(Row * Squad->RowSpacing) - CrescentPullback;
	const float RowStagger = (Row % 2 == 1) ? (Squad->ColumnSpacing * 0.35f) : 0.0f;
	const float LocalY = ((Col - CenterOffset) * Squad->ColumnSpacing) + RowStagger;

	return AnchorTransform.TransformPosition(FVector(LocalX, LocalY, 0.0f));
}

void UMinionStance_Follow::EvaluateFormationAndAvoidance()
{
	if (!Squad)
	{
		return;
	}

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	if (!PlayerActor || !World)
	{
		return;
	}

	const int32 MinionCount = Squad->PruneInvalidMinions();
	if (MinionCount == 0)
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	if (!NavSys || !NavData)
	{
		return;
	}

	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const FVector PlayerVelocity = PlayerActor->GetVelocity();
	const float PlayerSpeed2D = PlayerVelocity.Size2D();
	const FVector PlayerRight = PlayerActor->GetActorRightVector();
	const FVector PlayerForward = PlayerActor->GetActorForwardVector();
	const float EvadeRadiusSq = FMath::Square(Squad->PlayerEvadeRadius);

	// 0. Moving Platform / Elevator fast-path: bypass static NavMesh queries while riding a dynamic movement base
	const ACharacter* PlayerChar = Cast<ACharacter>(PlayerActor);
	const UPrimitiveComponent* PlayerMoveBase = PlayerChar ? PlayerChar->GetMovementBase() : nullptr;
	const bool bPlayerOnMovingPlatform = PlayerMoveBase && MovementBaseUtility::UseRelativeLocation(PlayerMoveBase);

	if (bPlayerOnMovingPlatform)
	{
		bSuppressingLateralEvade = true;
		const FTransform PlatformAnchor(PlayerForward.Rotation(), PlayerLocation);

		for (int32 i = 0; i < MinionCount; ++i)
		{
			AActor* MinionActor = Squad->ActiveMinions[i];
			IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[i].GetInterface();
			if (!MinionActor || !MinionInterface)
			{
				continue;
			}

			const EMinionState State = MinionInterface->GetMinionState();
			if (State == EMinionState::InCarryRush ||
				State == EMinionState::InThrowFlight ||
				State == EMinionState::LatchedToTarget ||
				State == EMinionState::TraversingNavLink)
			{
				continue;
			}

			// Keep minions standing on the same lift compactly clustered around the player
			const ACharacter* MinionChar = Cast<ACharacter>(MinionActor);
			if (MinionChar && MinionChar->GetMovementBase() == PlayerMoveBase)
			{
				MinionInterface->SetTightCorridorMode(true);
				const float Angle = (2.0f * PI * static_cast<float>(i)) / static_cast<float>(
					FMath::Max(MinionCount, 1));
				const FVector LocalSlot = PlayerLocation + FVector(FMath::Cos(Angle) * 75.0f, FMath::Sin(Angle) * 75.0f,
				                                                   0.0f);
				const float DistToSlot = FVector::Dist2D(MinionActor->GetActorLocation(), LocalSlot);
				MinionInterface->UpdateFollowSpeedForDistance(DistToSlot, PlayerSpeed2D);
				if (DistToSlot > 35.0f)
				{
					MinionInterface->MoveToSquadSlot(LocalSlot);
				}
			}
		}
		return;
	}

	// 1. Check grounded player NavMesh location & record breadcrumb spine
	FVector PlayerNavLoc = LastGroundedPlayerNavLoc;
	const bool bPlayerGroundedOnNav = Squad->IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc);

	if (bPlayerGroundedOnNav)
	{
		LastGroundedPlayerNavLoc = PlayerNavLoc;
		FormationSolver.RecordBreadcrumb(PlayerNavLoc, PlayerForward, Squad->FormationConfig);
	}

	// 2. Classify minions: Reachable vs. StrandedAtTakeoff vs. StrandedAtInteractable
	TArray<int32, TInlineAllocator<16>> ReachableIndices;
	TArray<int32, TInlineAllocator<16>> StrandedAtTakeoffIndices;
	TArray<int32, TInlineAllocator<16>> StrandedAtInteractableIndices;

	for (int32 i = 0; i < MinionCount; ++i)
	{
		IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[i].GetInterface();
		if (!MinionInterface)
		{
			continue;
		}

		const EMinionState State = MinionInterface->GetMinionState();
		if (State == EMinionState::InCarryRush ||
			State == EMinionState::InThrowFlight ||
			State == EMinionState::LatchedToTarget)
		{
			continue;
		}

		if (bPlayerGroundedOnNav && Squad->CanMinionReachNavLocation(Squad->ActiveMinions[i], NavSys, PlayerNavLoc))
		{
			MinionInterface->ClearInteractableWaitAnchor();
			ReachableIndices.Add(i);
		}
		else if (MinionInterface->HasInteractableWaitAnchor())
		{
			StrandedAtInteractableIndices.Add(i);
		}
		else
		{
			StrandedAtTakeoffIndices.Add(i);
		}
	}

	// Only advance WaitingGroupAnchorTransform when grounded AND all follow minions can reach the player
	if (bPlayerGroundedOnNav && StrandedAtTakeoffIndices.IsEmpty() && StrandedAtInteractableIndices.IsEmpty() && !
		ReachableIndices.IsEmpty())
	{
		const FVector TrailForward = (-FormationSolver.GetLastValidTrailBackDir()).GetSafeNormal2D();
		WaitingGroupAnchorTransform = FTransform(
			TrailForward.IsNearlyZero() ? PlayerForward.Rotation() : TrailForward.Rotation(),
			PlayerNavLoc);
		bHasValidWaitingAnchor = true;
	}

	if (ReachableIndices.Num() != LastReachableMinionCount)
	{
		LastReachableMinionCount = ReachableIndices.Num();
		bForceNextUpdate = true;
	}

	// Stationary pivot freeze: only treat anchor as moved on actual player TRANSLATION (not in-place aiming rotation)
	const bool bPlayerTranslated =
		FVector::DistSquared(PlayerNavLoc, LastAnchorLocation) >= FMath::Square(Squad->AnchorMoveThreshold);
	const bool bAnchorMoved = bForceNextUpdate || (bPlayerGroundedOnNav && bPlayerTranslated);

	// 3. Solve Topology-Aware Formation for Reachable Minions
	if (!ReachableIndices.IsEmpty())
	{
		// Sync config spacing with squad properties while preserving topology thresholds
		FMinionFormationConfig ActiveConfig = Squad->FormationConfig;
		ActiveConfig.RowSpacing = Squad->RowSpacing;
		ActiveConfig.ColumnSpacing = Squad->ColumnSpacing;
		ActiveConfig.PlayerSafeRadius = FMath::Max(ActiveConfig.PlayerSafeRadius, Squad->PlayerEvadeRadius + 10.0f);

		const FMinionFormationSolveResult SolveResult = FormationSolver.SolveFormation(
			NavSys,
			NavData,
			PlayerActor,
			PlayerNavLoc,
			PlayerForward,
			ReachableIndices.Num(),
			ActiveConfig);

		const bool bModeChanged = (SolveResult.ActiveMode != LastFormationMode);
		LastFormationMode = SolveResult.ActiveMode;
		bSuppressingLateralEvade = SolveResult.bSuppressLateralEvade;

		if (bAnchorMoved || bModeChanged)
		{
			LastAnchorLocation = PlayerNavLoc;
			LastAnchorRotation = PlayerForward.Rotation();
			bForceNextUpdate = false;
			Squad->OnSquadMoveUpdated.Broadcast(MinionCount, PlayerNavLoc);
		}

		const bool bForceIssueMoves = bAnchorMoved || bModeChanged;
		const int32 GroupCount = ReachableIndices.Num();

		// Build Minion -> Slot assignment
		TArray<int32, TInlineAllocator<16>> AssignedSlotForLocalIdx;
		AssignedSlotForLocalIdx.Init(INDEX_NONE, GroupCount);

		if (SolveResult.ActiveMode == EMinionFormationMode::SingleFileTrail)
		{
			// Single-File Bridge Mode: sort by distance to player with a rank-stickiness bias
			// so side-by-side minions entering a bridge lock into order without flip-flopping.
			TArray<int32, TInlineAllocator<16>> SortedLocalOrder;
			SortedLocalOrder.Reserve(GroupCount);
			for (int32 LocalIdx = 0; LocalIdx < GroupCount; ++LocalIdx)
			{
				SortedLocalOrder.Add(LocalIdx);
			}

			SortedLocalOrder.Sort([&](int32 A, int32 B)
			{
				AActor* ActorA = Squad->ActiveMinions[ReachableIndices[A]];
				AActor* ActorB = Squad->ActiveMinions[ReachableIndices[B]];

				float ScoreA = FVector::Dist2D(ActorA->GetActorLocation(), PlayerNavLoc);
				float ScoreB = FVector::Dist2D(ActorB->GetActorLocation(), PlayerNavLoc);

				if (const int32* PrevRankA = StickySlotAssignments.Find(ActorA))
				{
					ScoreA += static_cast<float>(*PrevRankA) * 25.0f;
				}
				if (const int32* PrevRankB = StickySlotAssignments.Find(ActorB))
				{
					ScoreB += static_cast<float>(*PrevRankB) * 25.0f;
				}

				return ScoreA < ScoreB;
			});

			for (int32 Rank = 0; Rank < GroupCount; ++Rank)
			{
				AssignedSlotForLocalIdx[SortedLocalOrder[Rank]] = Rank;
			}
		}
		else
		{
			// Open / Corridor / Fan Mode: greedy closest assignment with stickiness bonus to prevent unnecessary crisscrossing
			TArray<bool, TInlineAllocator<16>> bLocalUsed;
			bLocalUsed.Init(false, GroupCount);
			constexpr float StickinessBonusCm = 65.0f;

			for (int32 SlotIdx = 0; SlotIdx < SolveResult.Slots.Num() && SlotIdx < GroupCount; ++SlotIdx)
			{
				const FVector& SlotLoc = SolveResult.Slots[SlotIdx];
				int32 BestLocalIdx = INDEX_NONE;
				float BestScoreDist = MAX_flt;

				for (int32 LocalIdx = 0; LocalIdx < GroupCount; ++LocalIdx)
				{
					if (bLocalUsed[LocalIdx])
					{
						continue;
					}

					AActor* MinionActor = Squad->ActiveMinions[ReachableIndices[LocalIdx]];
					float ScoreDist = FVector::Dist2D(MinionActor->GetActorLocation(), SlotLoc);

					if (const int32* PrevSlot = StickySlotAssignments.Find(MinionActor))
					{
						if (*PrevSlot == SlotIdx)
						{
							ScoreDist -= StickinessBonusCm;
						}
					}

					if (ScoreDist < BestScoreDist)
					{
						BestScoreDist = ScoreDist;
						BestLocalIdx = LocalIdx;
					}
				}

				if (BestLocalIdx != INDEX_NONE)
				{
					bLocalUsed[BestLocalIdx] = true;
					AssignedSlotForLocalIdx[BestLocalIdx] = SlotIdx;
				}
			}
		}

		// Issue commands, speed scaling, corridor crowd weight, and tiered anti-stuck watchdog
		const FVector CatchUpWarpTarget = FormationSolver.GetTailBreadcrumbOrFallback(PlayerNavLoc);

		for (int32 LocalIdx = 0; LocalIdx < GroupCount; ++LocalIdx)
		{
			const int32 SlotIdx = AssignedSlotForLocalIdx[LocalIdx];
			if (!SolveResult.Slots.IsValidIndex(SlotIdx))
			{
				continue;
			}

			const int32 SquadIdx = ReachableIndices[LocalIdx];
			AActor* MinionActor = Squad->ActiveMinions[SquadIdx];
			IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[SquadIdx].GetInterface();
			if (!MinionActor || !MinionInterface)
			{
				continue;
			}

			StickySlotAssignments.Add(MinionActor, SlotIdx);
			MinionInterface->SetTightCorridorMode(SolveResult.bSuppressLateralEvade);

			const EMinionState CurrentState = MinionInterface->GetMinionState();
			if (CurrentState == EMinionState::TraversingNavLink)
			{
				ProgressTrackers.Remove(MinionActor);
				continue;
			}

			const FVector MinionLoc = MinionActor->GetActorLocation();
			const FVector& SlotLocation = SolveResult.Slots[SlotIdx];

			// Suppress lateral evade on narrow bridges so player can smoothly pass back through their own line
			const float VerticalDist = FMath::Abs(MinionLoc.Z - PlayerLocation.Z);
			if (!SolveResult.bSuppressLateralEvade &&
				VerticalDist < 120.0f &&
				FVector::DistSquared2D(MinionLoc, PlayerLocation) < EvadeRadiusSq)
			{
				MinionInterface->EvadePlayer(PlayerLocation, PlayerVelocity, PlayerRight);
				ProgressTrackers.Remove(MinionActor);
				continue;
			}

			const float DistToSlot = FVector::Dist2D(MinionLoc, SlotLocation);
			MinionInterface->UpdateFollowSpeedForDistance(DistToSlot, PlayerSpeed2D);

			// --- Tiered Anti-Stuck Watchdog (Piggybacked on UpdateInterval, zero Tick) ---
			if (CurrentState == EMinionState::Following && DistToSlot > 120.0f)
			{
				FMinionProgressTracker& Tracker = ProgressTrackers.FindOrAdd(MinionActor);
				if (Tracker.LastCheckLocation.IsZero())
				{
					Tracker.LastCheckLocation = MinionLoc;
				}
				else
				{
					const float MovedSq = FVector::DistSquared2D(MinionLoc, Tracker.LastCheckLocation);
					if (MovedSq < FMath::Square(18.0f))
					{
						Tracker.StuckSeconds += Squad->UpdateInterval;

						if (Tracker.StuckSeconds >= 4.0f &&
							(!MinionActor->WasRecentlyRendered(0.5f) || FVector::DistSquared2D(MinionLoc, PlayerNavLoc)
								> FMath::Square(1800.0f)))
						{
							MinionInterface->ExecuteUnstuckRecovery(3, CatchUpWarpTarget);
							Tracker.StuckSeconds = 0.0f;
							Tracker.LastTriggeredTier = 0;
							Tracker.LastCheckLocation = CatchUpWarpTarget;
							continue;
						}
						if (Tracker.StuckSeconds >= 2.4f && Tracker.LastTriggeredTier < 2)
						{
							Tracker.LastTriggeredTier = 2;
							MinionInterface->ExecuteUnstuckRecovery(2, SlotLocation);
							continue;
						}
						if (Tracker.StuckSeconds >= 1.2f && Tracker.LastTriggeredTier < 1)
						{
							Tracker.LastTriggeredTier = 1;
							MinionInterface->ExecuteUnstuckRecovery(1, PlayerNavLoc);
							continue;
						}
					}
					else
					{
						Tracker.LastCheckLocation = MinionLoc;
						Tracker.StuckSeconds = 0.0f;
						Tracker.LastTriggeredTier = 0;
					}
				}
			}
			else
			{
				ProgressTrackers.Remove(MinionActor);
			}

			if (Squad->bDebugDrawSlots)
			{
				const FColor SlotColor = (SolveResult.ActiveMode == EMinionFormationMode::SingleFileTrail)
					                         ? FColor::Yellow
					                         : ((SolveResult.ActiveMode == EMinionFormationMode::AdaptiveFan)
						                            ? FColor::Magenta
						                            : FColor::Green);
				DrawDebugSphere(World, SlotLocation, 14.0f, 8, SlotColor, false, Squad->UpdateInterval * 1.1f);
				DrawDebugLine(World, MinionLoc, SlotLocation, FColor::Cyan, false, Squad->UpdateInterval * 1.1f, 0,
				              1.5f);
			}

			if (bForceIssueMoves || DistToSlot > 48.0f)
			{
				MinionInterface->MoveToSquadSlot(SlotLocation);
			}
		}

		// Debug Overlay: Breadcrumb Spine, Topology Probes, and Active Mode readout
		if (Squad->bDebugDrawSlots)
		{
			const float DebugDuration = Squad->UpdateInterval * 1.1f;
			const TArray<FVector>& Trail = FormationSolver.GetBreadcrumbs();
			for (int32 b = 0; b < Trail.Num() - 1; ++b)
			{
				DrawDebugLine(World, Trail[b] + FVector(0, 0, 15.0f), Trail[b + 1] + FVector(0, 0, 15.0f),
				              FColor::Purple, false, DebugDuration, 0, 2.5f);
			}
			if (!Trail.IsEmpty())
			{
				DrawDebugLine(World, Trail.Last() + FVector(0, 0, 15.0f), PlayerNavLoc + FVector(0, 0, 15.0f),
				              FColor::Purple, false, DebugDuration, 0, 2.5f);
			}

			DrawDebugLine(World, SolveResult.DebugProbeOrigin + FVector(0, 0, 25.0f),
			              SolveResult.DebugLeftHit + FVector(0, 0, 25.0f), FColor::Orange, false, DebugDuration, 0,
			              2.0f);
			DrawDebugLine(World, SolveResult.DebugProbeOrigin + FVector(0, 0, 25.0f),
			              SolveResult.DebugRightHit + FVector(0, 0, 25.0f), FColor::Orange, false, DebugDuration, 0,
			              2.0f);
			DrawDebugLine(World, SolveResult.DebugProbeOrigin + FVector(0, 0, 25.0f),
			              SolveResult.DebugBackHit + FVector(0, 0, 25.0f), FColor::Red, false, DebugDuration, 0, 2.0f);

			const TCHAR* ModeLabel = TEXT("CRESCENT");
			switch (SolveResult.ActiveMode)
			{
			case EMinionFormationMode::DoubleFile: ModeLabel = TEXT("DOUBLE-FILE");
				break;
			case EMinionFormationMode::SingleFileTrail: ModeLabel = TEXT("SINGLE-FILE TRAIL");
				break;
			case EMinionFormationMode::AdaptiveFan: ModeLabel = TEXT("ADAPTIVE FAN");
				break;
			default: break;
			}

			DrawDebugString(
				World,
				PlayerLocation + FVector(0.0f, 0.0f, 125.0f),
				FString::Printf(
					TEXT("Mode: %s | Width: %.0fcm | Back: %.0fcm"), ModeLabel, SolveResult.EffectiveCorridorWidth,
					SolveResult.BackClearance),
				nullptr,
				FColor::White,
				DebugDuration,
				true,
				1.0f);
		}
	}

	// 4. Stranded Takeoff Group (with Closed-Gate Guard so minions don't sprint endlessly into a shut door)
	if (!StrandedAtTakeoffIndices.IsEmpty())
	{
		FVector StrandedAnchorLoc = WaitingGroupAnchorTransform.GetLocation();
		AActor* FirstStranded = Squad->ActiveMinions[StrandedAtTakeoffIndices[0]];

		// If the waiting anchor is on the player's side of a closed gate, fall back to the stranded group's own centroid
		if (!bHasValidWaitingAnchor || !Squad->CanMinionReachNavLocation(FirstStranded, NavSys, StrandedAnchorLoc))
		{
			FVector Centroid = FVector::ZeroVector;
			for (int32 SquadIdx : StrandedAtTakeoffIndices)
			{
				Centroid += Squad->ActiveMinions[SquadIdx]->GetActorLocation();
			}
			Centroid /= static_cast<float>(StrandedAtTakeoffIndices.Num());

			FNavLocation ProjectedCentroid;
			if (NavSys->ProjectPointToNavigation(Centroid, ProjectedCentroid, FVector(150.0f, 150.0f, 250.0f), NavData))
			{
				StrandedAnchorLoc = ProjectedCentroid.Location;
			}
			else
			{
				StrandedAnchorLoc = FirstStranded->GetActorLocation();
			}
		}

		const FSharedConstNavQueryFilter Filter =
			UNavigationQueryFilter::GetQueryFilter(*NavData, PlayerActor, nullptr);
		const int32 StrandedCount = StrandedAtTakeoffIndices.Num();
		const FTransform StrandedTransform(WaitingGroupAnchorTransform.GetRotation(), StrandedAnchorLoc);

		for (int32 LocalIdx = 0; LocalIdx < StrandedCount; ++LocalIdx)
		{
			const int32 SquadIdx = StrandedAtTakeoffIndices[LocalIdx];
			AActor* MinionActor = Squad->ActiveMinions[SquadIdx];
			IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[SquadIdx].GetInterface();
			if (!MinionActor || !MinionInterface)
			{
				continue;
			}

			if (MinionInterface->GetMinionState() == EMinionState::TraversingNavLink)
			{
				continue;
			}

			const FVector IdealSlot = CalculateSlotAroundTransform(LocalIdx, StrandedTransform);
			bool bHitEdge = false;
			float ClearDist = 0.0f;
			const FVector SafeSlot = FMinionFormationSolver::ClampPointOnNavMesh(
				NavSys, NavData, Filter, MinionActor, StrandedAnchorLoc, IdealSlot, 40.0f, bHitEdge, ClearDist);

			if (Squad->bDebugDrawSlots)
			{
				DrawDebugSphere(World, SafeSlot, 14.0f, 8, FColor::Blue, false, Squad->UpdateInterval * 1.1f);
			}

			if (FVector::DistSquared2D(MinionActor->GetActorLocation(), SafeSlot) > FMath::Square(45.0f))
			{
				MinionInterface->MoveToSquadSlot(SafeSlot);
			}
		}
	}

	// 5. Detached minions with no path to the player stay grouped around their interactable anchor
	for (int32 SquadIdx : StrandedAtInteractableIndices)
	{
		AActor* MinionActor = Squad->ActiveMinions[SquadIdx];
		IMinionAgentInterface* MinionInterface = Squad->ActiveMinionInterfaces[SquadIdx].GetInterface();
		if (!MinionActor || !MinionInterface)
		{
			continue;
		}

		const FVector AnchorPos = MinionInterface->GetInteractableWaitAnchor();
		if (Squad->bDebugDrawSlots)
		{
			DrawDebugSphere(World, AnchorPos, 18.0f, 8, FColor::Orange, false, Squad->UpdateInterval * 1.1f);
			DrawDebugLine(World, MinionActor->GetActorLocation(), AnchorPos, FColor::Orange, false,
			              Squad->UpdateInterval * 1.1f, 0, 1.5f);
		}

		if (FVector::DistSquared2D(MinionActor->GetActorLocation(), AnchorPos) > FMath::Square(85.0f))
		{
			MinionInterface->MoveToSquadSlot(AnchorPos);
		}
	}
}

// ============================================================================
// STANCE 2: UMinionStance_ChaosWhip IMPLEMENTATION (Uses StanceMinions)
// ============================================================================

void UMinionStance_ChaosWhip::OnEnterStance()
{
	if (!Squad)
	{
		return;
	}

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
	if (!Squad)
	{
		return;
	}

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
	if (!Squad)
	{
		return false;
	}

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
	if (!Squad)
	{
		return;
	}

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
	if (!Squad)
	{
		return;
	}

	AActor* PlayerActor = Squad->GetOwner();
	UWorld* World = Squad->GetWorld();
	if (!PlayerActor || !World)
	{
		return;
	}

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
		if (!MinionActor || !MinionInterface)
		{
			continue;
		}

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
			DrawDebugLine(World, MinionActor->GetActorLocation(), TargetLinkWorld, FColor::Yellow, false, -1.0f, 0,
			              2.0f);
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
				Squad->BroadcastDebug(
					TEXT("Whip Walk-In Timed Out before 4 minions arrived: Returning to Follow."), FColor::Red, 2.5f);
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
	if (!Squad)
	{
		return;
	}

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
		if (!MinionActor || !MinionInterface)
		{
			continue;
		}

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
		UPrimitiveComponent* ChildPrim = ChildMinion
			                                 ? Cast<UPrimitiveComponent>(ChildMinion->GetRootComponent())
			                                 : nullptr;
		if (!ChildPrim)
		{
			continue;
		}

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

		if (!ParentPrim)
		{
			continue;
		}

		UPhysicsConstraintComponent* Constraint = Squad->ConstraintPool[i];
		Constraint->AttachToComponent(ParentPrim, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Constraint->SetDisableCollision(true);

		Constraint->SetLinearXLimit(LCM_Limited, Cfg.LinearConstraintSlack);
		Constraint->SetLinearYLimit(LCM_Limited, Cfg.LinearConstraintSlack);
		Constraint->SetLinearZLimit(LCM_Limited, Cfg.MaxVerticalSlack);

		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Stiffness = Cfg.LinearSpringStiffness;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Damping = Cfg.LinearSpringDamping;

		Constraint->SetAngularSwing1Limit(ACM_Limited, Cfg.MaxJointBendAngleDegrees);
		Constraint->SetAngularSwing2Limit(ACM_Limited, 10.0f);
		Constraint->SetAngularTwistLimit(ACM_Locked, 0.0f);

		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Stiffness = Cfg.AngularBendStiffness;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Damping = Cfg.AngularBendDamping;

		Constraint->SetConstrainedComponents(ParentPrim, NAME_None, ChildPrim, NAME_None);

		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame1, FVector(TargetLinkDistance, 0.0f, 0.0f));
		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame2, FVector::ZeroVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame1, FVector::ForwardVector,
		                                              FVector::RightVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame2, FVector::ForwardVector,
		                                              FVector::RightVector);

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
	if (!Squad)
	{
		return;
	}

	for (UPhysicsConstraintComponent* Constraint : Squad->ConstraintPool)
	{
		if (IsValid(Constraint))
		{
			Constraint->BreakConstraint();
			if (AActor* OwnerActor = Squad->GetOwner())
			{
				Constraint->AttachToComponent(OwnerActor->GetRootComponent(),
				                              FAttachmentTransformRules::KeepRelativeTransform);
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
	if (!Squad)
	{
		return;
	}

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
				if (bAnySpinActive)
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
	if (!Squad)
	{
		return;
	}

	AActor* OwnerActor = Squad->GetOwner();
	if (!OwnerActor)
	{
		return;
	}

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
		if (UPrimitiveComponent* MinionRoot = MinionActor
			                                      ? Cast<UPrimitiveComponent>(MinionActor->GetRootComponent())
			                                      : nullptr)
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
	if (!Squad)
	{
		return;
	}

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
			FVector SafeLandingPos = PlayerActor->GetActorLocation() - (PlayerActor->GetActorForwardVector().
				GetSafeNormal2D() * 65.0f);
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
	if (!Squad)
	{
		return;
	}

	if (Squad->PruneInvalidStanceMinions() < Squad->MinReachableMinionsForSpecialStance)
	{
		Squad->SetSquadStance(EMinionSquadStance::Follow);
	}
}

bool UMinionStance_CarryRush::StartRushCharge()
{
	if (!Squad)
	{
		return false;
	}

	AActor* OwnerActor = Squad->GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

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
	if (!Squad || bIsCarryJumping)
	{
		return false;
	}

	ACharacter* PlayerChar = Cast<ACharacter>(Squad->GetOwner());
	if (!PlayerChar)
	{
		return false;
	}

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
			if (!Cfg.CarriedMinionAttachSocket.IsNone() && PlayerMesh && PlayerMesh->DoesSocketExist(
				Cfg.CarriedMinionAttachSocket))
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
	if (!bIsRushCharging || !Squad || !OtherActor)
	{
		return;
	}
	if (OtherActor->IsA<APawn>())
	{
		return;
	}

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
					const FVector BounceVel = (Hit.ImpactNormal.GetSafeNormal2D() * Cfg.WallBounceBackStrength) +
						FVector(0.0f, 0.0f, 260.0f);
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
	if (!Squad || !bIsRushCharging)
	{
		return;
	}

	UWorld* World = Squad->GetWorld();
	AActor* PlayerActor = Squad->GetOwner();
	if (!World || !PlayerActor)
	{
		return;
	}

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
	World->SweepMultiByChannel(
		HitResults,
		SweepStart,
		ExtendedEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Cfg.RamHitboxRadius),
		QueryParams);

	if (HitResults.IsEmpty())
	{
		return;
	}

	UPrimitiveComponent* PlayerRoot = Cast<UPrimitiveComponent>(PlayerActor->GetRootComponent());
	const double CurrentTime = World->GetTimeSeconds();

	const float SpeedAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(Cfg.InitialSpeed, Cfg.MaxSpeed),
		FVector2D(0.0f, 1.0f),
		CurrentRushSpeed);
	const float RamDamage = Cfg.BaseRamDamage + (SpeedAlpha * Cfg.MaxSpeedBonusDamage);
	const FHitFeedbackConfig& FeedbackCfg = Cfg.RamHitFeedback;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActor == PlayerActor || !HitActor->IsA<APawn>())
		{
			continue;
		}

		if (const double* LastHit = Squad->HitActorTimestamps.Find(HitActor))
		{
			if ((CurrentTime - *LastHit) < 0.4)
			{
				continue;
			}
		}

		// Build unified combat payload (mirrors PerformWhipHitSweep)
		const FVector ToEnemy = (HitActor->GetActorLocation() - PlayerActor->GetActorLocation()).GetSafeNormal2D();
		FVector KnockDir = (CarryRushDirection * 0.5f + ToEnemy * 0.5f).GetSafeNormal2D();
		if (KnockDir.IsNearlyZero())
		{
			KnockDir = CarryRushDirection;
		}

		const float KnockHoriz = (FeedbackCfg.KnockbackHorizontalStrength > KINDA_SMALL_NUMBER)
			                         ? FeedbackCfg.KnockbackHorizontalStrength
			                         : Cfg.EnemyKnockbackStrength;
		const float KnockVert = (FeedbackCfg.KnockbackVerticalStrength > KINDA_SMALL_NUMBER)
			                        ? FeedbackCfg.KnockbackVerticalStrength
			                        : 380.0f;

		FCombatHitPayload Payload;
		Payload.InstigatorActor = PlayerActor;
		Payload.DamageCauser = PlayerActor;
		Payload.TargetActor = HitActor;
		Payload.AttackType = FName("CarryRush");
		if (Hit.ImpactPoint.IsNearlyZero())
		{
			Payload.ImpactPoint = HitActor->GetActorLocation();
		}
		else
		{
			Payload.ImpactPoint = Hit.ImpactPoint;
		}
		Payload.ImpactNormal = Hit.ImpactNormal;
		Payload.KnockbackDirection = KnockDir;
		Payload.KnockbackVelocity = (KnockDir * KnockHoriz) + FVector(0.0f, 0.0f, KnockVert);
		Payload.HeartDamage = FMath::Max(1, FeedbackCfg.HeartDamage);
		Payload.StaggerDuration = FeedbackCfg.StaggerDuration;
		Payload.AppliedHitStopDuration = FeedbackCfg.HitStopDuration;
		Payload.HitResult = Hit;

		FCombatHitResponse HitResponse;
		bool bHandledByCombatTarget = false;

		if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(HitActor))
		{
			// NOTE: If HitActor is a rushing Boar and our speed qualifies, ReceiveCombatHit
			// resolves the clash, calls Squad->OnRushClashed (setting bIsRushCharging = false),
			// and returns bHitAccepted = false.
			HitResponse = Reactable->ReceiveCombatHit(Payload);
			bHandledByCombatTarget = true;
		}
		else if (UHeartHealthComponent* HealthComp = HitActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HitResponse = HealthComp->ApplyCombatHit(Payload);
			bHandledByCombatTarget = true;
		}

		// Immediately abort sweep if a clash stopped our rush on this hit
		if (!bIsRushCharging)
		{
			return;
		}

		// Respect clash, i-frames, or armored hide deflection (do NOT launch or damage deflected targets)
		if (bHandledByCombatTarget && !HitResponse.bHitAccepted)
		{
			Squad->HitActorTimestamps.Add(HitActor, CurrentTime);
			continue;
		}

		Squad->HitActorTimestamps.Add(HitActor, CurrentTime);

		if (PlayerRoot)
		{
			PlayerRoot->IgnoreActorWhenMoving(HitActor, true);
			RushIgnoredEnemies.AddUnique(HitActor);
		}

		if (bHandledByCombatTarget)
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
		else if (ACharacter* HitEnemyChar = Cast<ACharacter>(HitActor))
		{
			// Legacy fallback for pawns without UHeartHealthComponent
			HitEnemyChar->LaunchCharacter(Payload.KnockbackVelocity, true, true);
		}

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

		Squad->OnCarryRushEnemyHit.Broadcast(HitActor, RamDamage, CurrentRushSpeed, Hit);
	}
}

void UMinionStance_CarryRush::TickStance(float DeltaTime)
{
	if (!Squad)
	{
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
			RelLoc.Z = FMath::Lerp(CachedPlayerMeshBaseZ, CachedPlayerMeshBaseZ + Cfg.PlayerMeshLiftOffsetZ,
			                       GatherAlpha);
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

			// A head-on clash inside the sweep may have canceled the rush and launched the player backward
			if (!bIsRushCharging)
			{
				return;
			}

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
					if (IsValid(SquadMinion))
					{
						WallParams.AddIgnoredActor(SquadMinion);
					}
				}
				for (const TWeakObjectPtr<AActor>& IgnoredEnemy : RushIgnoredEnemies)
				{
					if (IgnoredEnemy.IsValid())
					{
						WallParams.AddIgnoredActor(IgnoredEnemy.Get());
					}
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
		                           : ((PlayerVel2D.SizeSquared() > 400.0f)
			                              ? PlayerVel2D.GetSafeNormal()
			                              : PlayerActor->GetActorForwardVector().GetSafeNormal2D());

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
		const FVector TargetCarrySlot = CalculatePalanquinSlotLocation(i, MinionCount, UpdatedPlayerLoc,
		                                                               PalanquinForward, PalanquinRight);
		const FVector NewMinionLoc = FMath::VInterpTo(MinionActor->GetActorLocation(), TargetCarrySlot, DeltaTime,
		                                              SlotInterpSpeed);

		MinionActor->SetActorLocationAndRotation(NewMinionLoc, MinionTargetRot, false);

		if (Squad->bDebugDrawSlots)
		{
			DrawDebugSphere(World, TargetCarrySlot, 16.0f, 8, bIsRushCharging ? FColor::Magenta : FColor::Cyan, false,
			                -1.0f);
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

	StanceWheelOrder = {
		EMinionSquadStance::Follow,
		EMinionSquadStance::AttackWhip,
		EMinionSquadStance::CarryRush
	};
}

void UMinionSquadComponent::BeginPlay()
{
	Super::BeginPlay();
	
	if (StanceWheelOrder.IsEmpty())
	{
		StanceWheelOrder = {
			EMinionSquadStance::Follow,
			EMinionSquadStance::AttackWhip,
			EMinionSquadStance::CarryRush
		};
	}

	SetComponentTickEnabled(false);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

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
		GEngine->AddOnScreenDebugMessage(-1, Duration, ScreenColor,
		                                 FString::Printf(TEXT("[PikminSquad] %s"), *Message));
	}
	UE_LOG(LogTemp, Log, TEXT("[PikminSquad] %s"), *Message);
}

int32 UMinionSquadComponent::PruneInvalidMinions()
{
	for (int32 i = ActiveMinions.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(ActiveMinions[i]) || !ActiveMinionInterfaces.IsValidIndex(i) || !ActiveMinionInterfaces[i].
			GetInterface())
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
		if (!IsValid(StanceMinions[i]) || !StanceMinionInterfaces.IsValidIndex(i) || !StanceMinionInterfaces[i].
			GetInterface())
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
	if (!NavSys || !PlayerActor)
	{
		return false;
	}

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
	if (!NavData)
	{
		return false;
	}

	// 2. Reject if the player's feet are not standing on a valid NavMesh surface
	FNavLocation ProjectedPlayerNav;
	const FVector PlayerFeetExtent(75.0f, 75.0f, 125.0f);
	if (!NavSys->ProjectPointToNavigation(PlayerActor->GetActorLocation(), ProjectedPlayerNav, PlayerFeetExtent,
	                                      NavData))
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
	if (!IsValid(MinionActor) || !NavSys)
	{
		return false;
	}

	if (const IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
	{
		const EMinionState State = MinionInterface->GetMinionState();
		if (State == EMinionState::InThrowFlight || State == EMinionState::LatchedToTarget)
		{
			return false;
		}
		if (State == EMinionState::InAttackWhip || State == EMinionState::InCarryRush)
		{
			return true;
		}
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (!NavData)
	{
		return false;
	}

	FNavLocation ProjectedMinionNav;
	const FVector MinionExtent(150.0f, 150.0f, 300.0f);
	if (!NavSys->ProjectPointToNavigation(MinionActor->GetActorLocation(), ProjectedMinionNav, MinionExtent, NavData))
	{
		return false;
	}

	const FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(
		*NavData, MinionActor, nullptr);

	// Fast-path: unobstructed straight-line NavMesh raycast guarantees same-island reachability—skip A* completely!
	FVector RayHitLoc;
	if (!NavData->Raycast(ProjectedMinionNav.Location, TargetNavLoc, RayHitLoc, QueryFilter, MinionActor))
	{
		return true;
	}

	// Full A* check around corners and Smart NavLinks (Partial Paths strictly disabled)
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
	if (!World)
	{
		return 0;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector PlayerNavLoc = FVector::ZeroVector;

	if (!IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc))
	{
		return 0;
	}

	for (int32 i = 0; i < ActiveMinions.Num(); ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		if (!IsValid(MinionActor) || !ActiveMinionInterfaces.IsValidIndex(i) || !ActiveMinionInterfaces[i].
			GetInterface())
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

	const int32 PreviousWheelIndex = GetWheelIndexForStance(CurrentStance);
	const int32 TargetWheelIndex = GetWheelIndexForStance(NewStance);

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

			OnStanceWheelChangeRejected.Broadcast(
				TargetWheelIndex,
				NewStance,
				ReachableCount,
				MinReachableMinionsForSpecialStance);

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
		StanceMinions = MoveTemp(ValidReachableMinions);
		StanceMinionInterfaces = MoveTemp(ValidReachableInterfaces);

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

	const int32 ResolvedDirection = (PendingWheelDirection != 0)
		                                ? PendingWheelDirection
		                                : ComputeShortestWheelDirection(PreviousWheelIndex, TargetWheelIndex);

	OnSquadStanceChanged.Broadcast(CurrentStance);
	OnStanceWheelChanged.Broadcast(TargetWheelIndex, PreviousWheelIndex, ResolvedDirection, CurrentStance);
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

void UMinionSquadComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
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
	if (PruneInvalidMinions() == 0)
	{
		return false;
	}

	if (CurrentStance != EMinionSquadStance::AttackWhip)
	{
		if (!bAutoEnterAttackStance)
		{
			return false;
		}
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
		if (PruneInvalidMinions() == 0)
		{
			return;
		}

		if (CurrentStance != EMinionSquadStance::AttackWhip)
		{
			if (!bAutoEnterAttackStance)
			{
				return;
			}
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

bool UMinionSquadComponent::CycleStanceWheel(int32 Direction)
{
	if (StanceWheelOrder.IsEmpty() || Direction == 0)
	{
		return false;
	}

	const int32 Step = (Direction > 0) ? 1 : -1;
	const int32 Count = StanceWheelOrder.Num();

	int32 CurrentIndex = GetCurrentWheelIndex();
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = 0;
	}

	const int32 TargetIndex = (CurrentIndex + Step + Count) % Count;

	PendingWheelDirection = Step;
	const bool bSuccess = SetSquadStance(StanceWheelOrder[TargetIndex]);
	PendingWheelDirection = 0;

	return bSuccess;
}

bool UMinionSquadComponent::SetSquadStanceByWheelIndex(int32 WheelIndex)
{
	if (!StanceWheelOrder.IsValidIndex(WheelIndex))
	{
		return false;
	}

	const int32 CurrentIndex = GetCurrentWheelIndex();
	PendingWheelDirection = ComputeShortestWheelDirection(CurrentIndex, WheelIndex);
	const bool bSuccess = SetSquadStance(StanceWheelOrder[WheelIndex]);
	PendingWheelDirection = 0;

	return bSuccess;
}

int32 UMinionSquadComponent::GetCurrentWheelIndex() const
{
	return GetWheelIndexForStance(CurrentStance);
}

int32 UMinionSquadComponent::GetWheelIndexForStance(EMinionSquadStance Stance) const
{
	return StanceWheelOrder.IndexOfByKey(Stance);
}

int32 UMinionSquadComponent::ComputeShortestWheelDirection(int32 FromIndex, int32 ToIndex) const
{
	const int32 Count = StanceWheelOrder.Num();
	if (Count <= 1 || FromIndex == INDEX_NONE || ToIndex == INDEX_NONE || FromIndex == ToIndex)
	{
		return 0;
	}

	const int32 ForwardSteps = (ToIndex - FromIndex + Count) % Count;
	const int32 BackwardSteps = (FromIndex - ToIndex + Count) % Count;

	return (ForwardSteps <= BackwardSteps) ? 1 : -1;
}

bool UMinionSquadComponent::StartCarryRushCharge(bool bAutoEnterCarryStance)
{
	if (PruneInvalidMinions() == 0)
	{
		return false;
	}

	if (CurrentStance != EMinionSquadStance::CarryRush)
	{
		if (!bAutoEnterCarryStance)
		{
			return false;
		}
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

void UMinionSquadComponent::OnPlayerRushActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse,
                                                 const FHitResult& Hit)
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
	if (!World || !OwnerActor)
	{
		return;
	}

	TArray<AActor*> FoundMinions;
	UGameplayStatics::GetAllActorsWithInterface(World, UMinionAgentInterface::StaticClass(), FoundMinions);

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	FoundMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared(
			B.GetActorLocation(), PlayerLoc);
	});

	for (AActor* Minion : FoundMinions)
	{
		if (ActiveMinions.Num() >= MaxMinions)
		{
			break;
		}
		RegisterMinion(Minion);
	}
}

bool UMinionSquadComponent::RegisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor))
	{
		return false;
	}

	IMinionAgentInterface* InterfacePtr = Cast<IMinionAgentInterface>(MinionActor);
	if (!InterfacePtr)
	{
		return false;
	}

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
		if (IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc) && CanMinionReachNavLocation(
			MinionActor, NavSys, PlayerNavLoc))
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

bool UMinionSquadComponent::CanWorldLocationReachPlayer(const FVector& WorldLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector PlayerNavLoc = FVector::ZeroVector;
	if (!IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc))
	{
		return false;
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (!NavData)
	{
		return false;
	}

	FNavLocation StartNav;
	if (!NavSys->ProjectPointToNavigation(WorldLocation, StartNav, FVector(250.0f, 250.0f, 500.0f), NavData))
	{
		return false;
	}

	FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(*NavData, GetOwner(), nullptr);
	FPathFindingQuery PathQuery(GetOwner(), *NavData, StartNav.Location, PlayerNavLoc, QueryFilter);
	PathQuery.SetAllowPartialPaths(false);

	return NavSys->TestPathSync(PathQuery, EPathFindingMode::Regular);
}

// ============================================================================
// IRushClashInterface IMPLEMENTATION (Head-on Clash with Rushing Enemies)
// ============================================================================

bool UMinionSquadComponent::IsCarryRushing_Implementation() const
{
	return IsCarryRushCharging();
}

float UMinionSquadComponent::GetCarryRushSpeed_Implementation() const
{
	return GetCurrentCarryRushSpeed();
}

FVector UMinionSquadComponent::GetCarryRushDirection_Implementation() const
{
	if (CurrentStance == EMinionSquadStance::CarryRush && CarryRushStanceInstance)
	{
		return CarryRushStanceInstance->GetCarryRushDirection();
	}
	return GetOwner() ? GetOwner()->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
}

void UMinionSquadComponent::OnRushClashed_Implementation(AActor* ClashedOpponent, const FVector& RecoilVelocity)
{
	// 1. Prevent residual bumper sweeps from re-hitting the clashed opponent on the same frame
	if (UWorld* World = GetWorld())
	{
		if (IsValid(ClashedOpponent))
		{
			HitActorTimestamps.Add(ClashedOpponent, World->GetTimeSeconds());
		}
	}

	// 2. Cancel Carry Rush (dropping back to Follow lets the player immediately follow up with Whip Spin Attack)
	StopCarryRushCharge(CarryRushConfig.bExitCarryStanceOnClash);

	// 3. Apply bounce-back recoil to the Player
	if (ACharacter* PlayerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = PlayerChar->GetCharacterMovement())
		{
			MoveComp->StopActiveMovement();
		}
		PlayerChar->LaunchCharacter(RecoilVelocity, true, true);
	}

	OnCarryRushClashed.Broadcast(ClashedOpponent, RecoilVelocity);
	BroadcastDebug(
		FString::Printf(TEXT("CARRY RUSH CLASHED with %s!"), *GetNameSafe(ClashedOpponent)),
		FColor::Cyan,
		2.5f);
}

AActor* UMinionSquadComponent::FindBestThrowableMinion(const FVector& ReferenceOrigin, float MaxGrabDistance) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector PlayerNavLoc = FVector::ZeroVector;
	const bool bPlayerGrounded = IsPlayerGroundedOnNavMesh(NavSys, PlayerNavLoc);
	const float MaxGrabDistSq = FMath::Square(MaxGrabDistance);
	constexpr float CloseGrabBypassSq = FMath::Square(350.0f);

	AActor* BestMinion = nullptr;
	float BestDistSq = MAX_flt;

	for (int32 i = 0; i < ActiveMinions.Num(); ++i)
	{
		AActor* Candidate = ActiveMinions[i];
		if (!IsValid(Candidate) || !ActiveMinionInterfaces.IsValidIndex(i))
		{
			continue;
		}

		IMinionAgentInterface* MinionInterface = ActiveMinionInterfaces[i].GetInterface();
		if (!MinionInterface || !MinionInterface->CanBeThrown())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), ReferenceOrigin);
		if (DistSq > MaxGrabDistSq)
		{
			continue;
		}

		if (bPlayerGrounded && DistSq > CloseGrabBypassSq && !
			CanMinionReachNavLocation(Candidate, NavSys, PlayerNavLoc))
		{
			continue;
		}

		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestMinion = Candidate;
		}
	}

	return BestMinion;
}

int32 UMinionSquadComponent::RecallThrownMinions(bool bOnlyDetachIfReachable)
{
	PruneInvalidMinions();

	int32 RecalledCount = 0;
	for (int32 i = 0; i < ActiveMinions.Num(); ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		IMinionAgentInterface* MinionInterface = ActiveMinionInterfaces[i].GetInterface();
		if (!MinionActor || !MinionInterface)
		{
			continue;
		}

		const EMinionState State = MinionInterface->GetMinionState();
		if (State != EMinionState::LatchedToTarget && State != EMinionState::InThrowFlight)
		{
			continue;
		}

		UMinionThrowTargetComponent* ThrowTarget = MinionInterface->GetActiveThrowTarget();
		if (bOnlyDetachIfReachable)
		{
			const FVector CheckOrigin = IsValid(ThrowTarget)
				                            ? ThrowTarget->GetComponentLocation()
				                            : MinionActor->GetActorLocation();
			if (!CanWorldLocationReachPlayer(CheckOrigin))
			{
				// No valid path back to player: stay at the interactable until player has a path or hits a checkpoint
				continue;
			}
		}

		if (IsValid(ThrowTarget))
		{
			ThrowTarget->ReleaseMinion(MinionActor, FVector::ZeroVector, false);
		}
		else
		{
			MinionInterface->DetachFromThrowTarget(FVector(0.0f, 0.0f, 220.0f), false);
		}

		++RecalledCount;
	}

	if (CurrentStance == EMinionSquadStance::Follow && FollowStanceInstance)
	{
		FollowStanceInstance->ForceImmediateEvaluation();
	}

	if (RecalledCount > 0)
	{
		BroadcastDebug(FString::Printf(TEXT("Recalled %d thrown minion(s)."), RecalledCount), FColor::Cyan, 1.5f);
	}

	return RecalledCount;
}

void UMinionSquadComponent::ResetMinionsToPlayerCheckpoint(bool bDetachFromInteractables)
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwner();
	if (!World || !PlayerActor)
	{
		return;
	}

	// Return to Follow stance first so any active Whip/CarryRush state releases cleanly
	if (CurrentStance != EMinionSquadStance::Follow)
	{
		SetSquadStance(EMinionSquadStance::Follow);
	}

	// >>> ADD THESE LINES HERE <<<
	// Clean up an interrupted Carry Jump (player fell into void before LandedDelegate fired)
	if (CarryRushStanceInstance && CarryRushStanceInstance->IsCarryJumping())
	{
		CarryRushStanceInstance->HandlePlayerLanded(FHitResult());
	}
	// >>> END ADDITION <<<

	const int32 MinionCount = PruneInvalidMinions();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	
	const FVector NavExtent(250.0f, 250.0f, 500.0f);

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		IMinionAgentInterface* MinionInterface = ActiveMinionInterfaces[i].GetInterface();
		if (!MinionActor || !MinionInterface)
		{
			continue;
		}

		// 1. Cleanly remove minion from any affected interactable object (fires OnMinionDetached & resets door/slow states)
		if (bDetachFromInteractables)
		{
			if (UMinionThrowTargetComponent* ThrowTarget = MinionInterface->GetActiveThrowTarget())
			{
				ThrowTarget->ReleaseMinion(MinionActor, FVector::ZeroVector, true);
			}
			else if (MinionInterface->GetMinionState() == EMinionState::InThrowFlight ||
				MinionInterface->GetMinionState() == EMinionState::LatchedToTarget)
			{
				MinionInterface->DetachFromThrowTarget(FVector::ZeroVector, true);
			}
		}

		MinionInterface->ClearInteractableWaitAnchor();

		// 2. Teleport cleanly onto the minion's NavMesh formation slot behind the player
		FVector TargetSlot = CalculateSlotWorldLocation(i);
		if (NavSys)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(TargetSlot, Projected, NavExtent))
			{
				TargetSlot = Projected.Location + FVector(0.0f, 0.0f, 45.0f);
			}
			else if (NavSys->ProjectPointToNavigation(PlayerActor->GetActorLocation(), Projected, NavExtent))
			{
				TargetSlot = Projected.Location + FVector(0.0f, 0.0f, 45.0f);
			}
		}

		if (APawn* MinionPawn = Cast<APawn>(MinionActor))
		{
			if (AAIController* AICon = Cast<AAIController>(MinionPawn->GetController()))
			{
				AICon->StopMovement();
			}
		}

		MinionActor->SetActorLocationAndRotation(
			TargetSlot,
			FRotator(0.0f, PlayerActor->GetActorRotation().Yaw, 0.0f),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	if (FollowStanceInstance)
	{
		FollowStanceInstance->ForceImmediateEvaluation();
	}

	BroadcastDebug(
		FString::Printf(
			TEXT("Checkpoint Reset: Teleported %d minions to player and cleared affected interactables."), MinionCount),
		FColor::Green, 2.5f);
}

void UMinionSquadComponent::UnregisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor))
	{
		return;
	}


	const int32 FoundIdx = ActiveMinions.IndexOfByKey(MinionActor);
	if (FoundIdx != INDEX_NONE)
	{
		if (IMinionAgentInterface* MinionInterface = ActiveMinionInterfaces[FoundIdx].GetInterface())
		{
			if (UMinionThrowTargetComponent* ThrowTarget = MinionInterface->GetActiveThrowTarget())
			{
				ThrowTarget->ReleaseMinion(MinionActor, FVector::ZeroVector, true);
			}
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
	if (CurrentStance != EMinionSquadStance::Follow)
	{
		return;
	}

	// Do not trigger lateral side-steps on narrow bridges or moving platforms
	if (FollowStanceInstance && FollowStanceInstance->IsLateralEvadeSuppressed())
	{
		return;
	}

	AActor* PlayerActor = GetOwner();
	if (!PlayerActor || !IsValid(OtherActor) || OtherActor == PlayerActor)
	{
		return;
	}

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
		const FVector BaseLeftOffset = WhipConfig.bDualArmWhip
			                               ? WhipConfig.LeftArmLocalOffset
			                               : WhipConfig.RightArmLocalOffset;
		const float BaseLeftYaw = WhipConfig.bDualArmWhip
			                          ? WhipConfig.LeftArmRestYawOffset
			                          : WhipConfig.RightArmRestYawOffset;

		const FVector LeftPos = BaseLeftOffset.RotateAngleAxis(ExtraOrbitYaw, FVector::UpVector);
		const FRotator LeftRot(0.0f, BaseLeftYaw + ExtraOrbitYaw, 0.0f);
		HandAnchorSpheres[1]->SetRelativeLocationAndRotation(LeftPos, LeftRot);
	}
}

FVector UMinionSquadComponent::CalculateWhipLinkWorldTarget(int32 StanceMinionIndex) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return GetComponentLocation();
	}

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
	if (!OwnerActor || PruneInvalidStanceMinions() <= 1)
	{
		return;
	}

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	const FVector PlayerRight = OwnerActor->GetActorRightVector();

	if (!WhipConfig.bDualArmWhip)
	{
		StanceMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared2D(
				B.GetActorLocation(), PlayerLoc);
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
			if (i < RightCount)
			{
				RightArmPool.Add(SortedBySide[i]);
			}
			else
			{
				LeftArmPool.Add(SortedBySide[i]);
			}
		}

		const FVector RightAnchor = HandAnchorSpheres[0] ? HandAnchorSpheres[0]->GetComponentLocation() : PlayerLoc;
		const FVector LeftAnchor = HandAnchorSpheres[1] ? HandAnchorSpheres[1]->GetComponentLocation() : PlayerLoc;

		RightArmPool.Sort([&RightAnchor](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), RightAnchor) < FVector::DistSquared2D(
				B.GetActorLocation(), RightAnchor);
		});

		LeftArmPool.Sort([&LeftAnchor](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), LeftAnchor) < FVector::DistSquared2D(
				B.GetActorLocation(), LeftAnchor);
		});

		StanceMinions.Empty(Total);
		const int32 MaxArmLen = FMath::Max(RightArmPool.Num(), LeftArmPool.Num());
		for (int32 LinkIdx = 0; LinkIdx < MaxArmLen; ++LinkIdx)
		{
			if (LinkIdx < RightArmPool.Num())
			{
				StanceMinions.Add(RightArmPool[LinkIdx]);
			}
			if (LinkIdx < LeftArmPool.Num())
			{
				StanceMinions.Add(LeftArmPool[LinkIdx]);
			}
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

void UMinionSquadComponent::PerformWhipHitSweep(AActor* MinionActor, int32 LinkIndexInArm, const FVector& StartLoc,
                                                const FVector& EndLoc)
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwner();
	if (!World || !PlayerActor || !MinionActor)
	{
		return;
	}

	// Guard against degenerate zero-length sweeps (e.g., slow frames or right after HitStop)
	const FVector SafeEndLoc = StartLoc.Equals(EndLoc, 0.1f)
		                           ? (EndLoc + FVector(0.0f, 0.0f, 1.0f))
		                           : EndLoc;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MinionWhipSweep), false, PlayerActor);
	QueryParams.AddIgnoredActor(PlayerActor);
	for (AActor* SquadMinion : ActiveMinions)
	{
		if (IsValid(SquadMinion))
		{
			QueryParams.AddIgnoredActor(SquadMinion);
		}
	}

	TArray<FHitResult> HitResults;
	// NOTE: SweepMultiByChannel returns TRUE only for BLOCKING hits; overlap hits populate HitResults while returning FALSE.
	const bool bBlockingHitFound = World->SweepMultiByChannel(
		HitResults,
		StartLoc,
		SafeEndLoc,
		FQuat::Identity,
		WhipConfig.HitTraceChannel,
		FCollisionShape::MakeSphere(WhipConfig.HitSphereRadius),
		QueryParams);

	if (bDebugDrawSlots)
	{
		// White = empty sweep volume | Yellow = overlap hit(s) detected | Red = blocking hit detected
		const FColor SweepColor = HitResults.IsEmpty()
			                          ? FColor::White
			                          : (bBlockingHitFound ? FColor::Red : FColor::Yellow);

		DrawDebugSphere(World, SafeEndLoc, WhipConfig.HitSphereRadius, 10, SweepColor, false, 0.05f, 0, 1.0f);
		DrawDebugLine(World, StartLoc, SafeEndLoc, SweepColor, false, 0.05f, 0, 1.5f);
	}

	// CRITICAL FIX: Check HitResults.IsEmpty(), NOT !bBlockingHitFound
	if (HitResults.IsEmpty())
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();
	const FHitFeedbackConfig& FeedbackCfg = WhipConfig.HitFeedback;

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		UPrimitiveComponent* HitComp = Hit.GetComponent();
		if (!IsValid(HitActor))
		{
			continue;
		}

		// 1. Check Whip Per-Target Cooldown
		if (const double* LastHitTime = HitActorTimestamps.Find(HitActor))
		{
			const double Elapsed = CurrentTime - *LastHitTime;
			if (Elapsed < static_cast<double>(WhipConfig.HitCooldownPerTarget))
			{
				if (bDebugDrawSlots)
				{
					UE_LOG(LogTemp, Verbose, TEXT("[WhipDebug] Ignored %s (%s): Whip Cooldown %.2f / %.2fs"),
					       *GetNameSafe(HitActor), *GetNameSafe(HitComp), Elapsed, WhipConfig.HitCooldownPerTarget);
				}
				continue;
			}
		}

		// 2. Compute BOTW knockback direction (radial away from player + whip travel follow-through)
		const FVector ShotDirection = (SafeEndLoc - StartLoc).GetSafeNormal();
		const FVector RadialAway = (HitActor->GetActorLocation() - PlayerActor->GetActorLocation()).GetSafeNormal2D();
		FVector KnockDir = (RadialAway * 0.7f + ShotDirection.GetSafeNormal2D() * 0.3f).GetSafeNormal2D();
		if (KnockDir.IsNearlyZero())
		{
			KnockDir = PlayerActor->GetActorForwardVector().GetSafeNormal2D();
		}

		const FVector KnockbackVel =
			(KnockDir * FeedbackCfg.KnockbackHorizontalStrength) +
			FVector(0.0f, 0.0f, FeedbackCfg.KnockbackVerticalStrength);

		FCombatHitPayload Payload;
		Payload.InstigatorActor = PlayerActor;
		Payload.DamageCauser = MinionActor;
		Payload.TargetActor = HitActor;
		Payload.AttackType = FName("SpinAttack");
		if (Hit.ImpactPoint.IsNearlyZero())
		{
			Payload.ImpactPoint = HitActor->GetActorLocation();
		}
		else
		{
			Payload.ImpactPoint = Hit.ImpactPoint;
		}
		Payload.ImpactNormal = Hit.ImpactNormal;
		Payload.KnockbackDirection = KnockDir;
		Payload.KnockbackVelocity = KnockbackVel;
		Payload.HeartDamage = FeedbackCfg.HeartDamage;
		Payload.WhipChainIndex = LinkIndexInArm;
		Payload.StaggerDuration = FeedbackCfg.StaggerDuration;
		Payload.AppliedHitStopDuration = FeedbackCfg.HitStopDuration;
		Payload.HitResult = Hit;

		// 3. Route through IHitReactableInterface or UHeartHealthComponent
		FCombatHitResponse HitResponse;
		bool bHandledByCombatTarget = false;

		if (IHitReactableInterface* Reactable = Cast<IHitReactableInterface>(HitActor))
		{
			HitResponse = Reactable->ReceiveCombatHit(Payload);
			bHandledByCombatTarget = true;
		}
		else if (UHeartHealthComponent* HealthComp = HitActor->FindComponentByClass<UHeartHealthComponent>())
		{
			HitResponse = HealthComp->ApplyCombatHit(Payload);
			bHandledByCombatTarget = true;
		}

		// Log why a valid combat target rejected the hit (i-frames or already dead)
		if (bHandledByCombatTarget && !HitResponse.bHitAccepted)
		{
			UE_LOG(LogTemp, Warning,
			       TEXT("[WhipDebug] Target %s (%s) REJECTED hit (i-frames or dead). RemainingHearts=%d"),
			       *GetNameSafe(HitActor), *GetNameSafe(HitComp), HitResponse.RemainingHearts);

			if (bDebugDrawSlots)
			{
				DrawDebugSphere(World, Payload.ImpactPoint, 22.0f, 8, FColor::Orange, false, 0.4f, 0, 2.0f);
			}
			continue;
		}

		HitActorTimestamps.Add(HitActor, CurrentTime);

		if (bHandledByCombatTarget)
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

			OnWhipCombatFeedback.Broadcast(Payload);

			BroadcastDebug(
				FString::Printf(
					TEXT(
						"WHIP HIT %s [Comp: %s | Block:%d] -> -%d Heart (%d/%d left) | Kill:%d | WhipZ:%.0f vs TargetZ:%.0f"),
					*GetNameSafe(HitActor),
					*GetNameSafe(HitComp),
					Hit.bBlockingHit ? 1 : 0,
					Payload.HeartDamage,
					Payload.RemainingHearts,
					Payload.MaxHearts,
					Payload.bIsKillingBlow ? 1 : 0,
					SafeEndLoc.Z,
					HitActor->GetActorLocation().Z),
				Payload.bIsKillingBlow ? FColor::Yellow : FColor::Green,
				2.5f);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
			       TEXT(
				       "[WhipDebug] Swept %s (%s) [Block:%d] but it has NO IHitReactableInterface or UHeartHealthComponent!"
			       ),
			       *GetNameSafe(HitActor), *GetNameSafe(HitComp), Hit.bBlockingHit ? 1 : 0);
		}

		// Preserve existing UE5 PointDamage + OnWhipDamageHit compatibility
		const float CalculatedDamage = static_cast<float>(FeedbackCfg.HeartDamage);
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
			DrawDebugSphere(World, Payload.ImpactPoint, 28.0f, 12,
			                Payload.bIsKillingBlow ? FColor::Yellow : FColor::Green, false, 0.8f, 0, 2.5f);
			DrawDebugDirectionalArrow(World, Payload.ImpactPoint, Payload.ImpactPoint + (KnockDir * 120.0f), 25.0f,
			                          FColor::Cyan, false, 0.8f, 0, 2.5f);
		}
	}
}

FVector UMinionSquadComponent::CalculateSlotWorldLocation(int32 SlotIndex) const
{
	constexpr int32 ColumnsPerRow = 4;
	const int32 Row = SlotIndex / ColumnsPerRow;
	const int32 Col = SlotIndex % ColumnsPerRow;

	constexpr float CenterOffset = (ColumnsPerRow - 1) * 0.5f;
	const float NormalizedCol = FMath::Abs(Col - CenterOffset);
	const float CrescentPullback = (NormalizedCol > 1.0f) ? 25.0f : 0.0f;

	const float LocalX = -(Row * RowSpacing) - CrescentPullback;
	const float RowStagger = (Row % 2 == 1) ? (ColumnSpacing * 0.35f) : 0.0f;
	const float LocalY = ((Col - CenterOffset) * ColumnSpacing) + RowStagger;

	return GetComponentTransform().TransformPosition(FVector(LocalX, LocalY, 0.0f));
}
