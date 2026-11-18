// MinionNavLinkProxy.cpp
#include "MinionNavLinkProxy.h"
#include "MinionAgentInterface.h"
#include "NavLinkCustomComponent.h"
#include "NavigationSystem.h"
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
		SmartComp->SetBroadcastData(TriggerRadius, ECollisionChannel::ECC_Pawn, 0.2f);
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
		SmartComp->SetBroadcastData(TriggerRadius, ECollisionChannel::ECC_Pawn, 0.2f);
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

	const FVector StartLocation = MovingActor->GetActorLocation();
	const FVector PaddedDestination = DestinationPoint + FVector(0.0f, 0.0f, LandingHeightPadding);
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