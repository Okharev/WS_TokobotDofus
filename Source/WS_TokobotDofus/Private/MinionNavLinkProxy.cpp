// MinionNavLinkProxy.cpp
#include "MinionNavLinkProxy.h"
#include "MinionAgentInterface.h"
#include "MinionFormationSolver.h"
#include "NavLinkCustomComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

AMinionNavLinkProxy::AMinionNavLinkProxy()
{
	bSmartLinkIsRelevant = true;
	PointLinks.Empty();

	if (UNavLinkCustomComponent* SmartComp = GetSmartLinkComp())
	{
		SmartComp->SetEnabled(true);
		SmartComp->SetLinkData(LinkStartPoint, LinkEndPoint, LinkDirection);
		SmartComp->SetBroadcastData(TriggerRadius, ECC_Pawn, 0.2f);
	}
}

void AMinionNavLinkProxy::OnConstruction(const FTransform& Transform)
{
	PointLinks.Empty();
	bSmartLinkIsRelevant = true;

	if (UNavLinkCustomComponent* SmartComp = GetSmartLinkComp())
	{
		SmartComp->SetEnabled(true);
		SmartComp->SetLinkData(LinkStartPoint, LinkEndPoint, LinkDirection);
		SmartComp->SetBroadcastData(TriggerRadius, ECC_Pawn, 0.2f);
	}

	Super::OnConstruction(Transform);

	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			NavSys->UpdateActorAndComponentsInNavOctree(*this);
		}
	}
}

void AMinionNavLinkProxy::BeginPlay()
{
	Super::BeginPlay();
	OnSmartLinkReached.AddDynamic(this, &AMinionNavLinkProxy::HandleSmartLinkReached);
}

void AMinionNavLinkProxy::HandleSmartLinkReached(AActor* MovingActor, const FVector& DestinationPoint)
{
	if (!IsValid(MovingActor))
	{
		return;
	}

	IMinionAgentInterface* Minion = Cast<IMinionAgentInterface>(MovingActor);
	if (!Minion)
	{
		ResumePathFollowing(MovingActor);
		return;
	}

	// Ignore triggers if minion is already mid-jump, thrown, latched, or locked in a special stance
	const EMinionState CurrentState = Minion->GetMinionState();
	if (CurrentState == EMinionState::TraversingNavLink ||
		CurrentState == EMinionState::InAttackWhip ||
		CurrentState == EMinionState::InCarryRush ||
		CurrentState == EMinionState::InThrowFlight ||
		CurrentState == EMinionState::LatchedToTarget)
	{
		return;
	}

	const FVector StartLocation = MovingActor->GetActorLocation();
	FVector TargetLandingPoint = DestinationPoint;

	// De-clump sequential landings: nudge slightly inward past the ledge lip + deterministic lateral spread,
	// clamped strictly to the landing NavMesh via Raycast so minions never push each other back off the edge.
	const FVector JumpDir2D = (DestinationPoint - StartLocation).GetSafeNormal2D();
	if (!JumpDir2D.IsNearlyZero())
	{
		const FVector JumpRight2D = FVector::CrossProduct(FVector::UpVector, JumpDir2D).GetSafeNormal2D();
		const int32 LaneBucket = static_cast<int32>(GetTypeHash(MovingActor) % 3) - 1; // -1, 0, or +1
		const FVector DesiredSpreadPoint =
			DestinationPoint + (JumpDir2D * 50.0f) + (JumpRight2D * (LaneBucket * 28.0f));

		if (UWorld* World = GetWorld())
		{
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				if (const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance())
				{
					const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(
						*NavData, MovingActor, nullptr);
					bool bHitEdge = false;
					float ClearDist = 0.0f;
					TargetLandingPoint = FMinionFormationSolver::ClampPointOnNavMesh(
						NavSys,
						NavData,
						Filter,
						MovingActor,
						DestinationPoint,
						DesiredSpreadPoint,
						36.0f,
						bHitEdge,
						ClearDist);
				}
			}
		}
	}

	const FVector PaddedDestination = TargetLandingPoint + FVector(0.0f, 0.0f, LandingHeightPadding);
	FVector OutLaunchVelocity = FVector::ZeroVector;

	const bool bHasValidArc = UGameplayStatics::SuggestProjectileVelocity_CustomArc(
		this,
		OutLaunchVelocity,
		StartLocation,
		PaddedDestination,
		0.0f,
		JumpArcHeight);

	if (bDrawDebugJumpArc && GetWorld())
	{
		DrawDebugLine(GetWorld(), StartLocation, PaddedDestination, FColor::Orange, false, 2.0f, 0, 2.5f);
		DrawDebugSphere(GetWorld(), PaddedDestination, 20.0f, 8, FColor::Magenta, false, 2.0f);
	}

	if (bHasValidArc)
	{
		Minion->ExecuteNavLinkJump(this, OutLaunchVelocity);
	}
	else
	{
		const FVector FallbackDir = (PaddedDestination - StartLocation).GetSafeNormal();
		Minion->ExecuteNavLinkJump(this, (FallbackDir * 600.0f) + FVector(0.0f, 0.0f, 450.0f));
	}
}
