// MinionAIController.cpp
#include "MinionAIController.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

AMinionAIController::AMinionAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	CrowdFollowingComponent = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
}

void AMinionAIController::OnPossess(APawn* InPawn)
{
	if (ACharacter* PossessedChar = Cast<ACharacter>(InPawn))
	{
		if (UCharacterMovementComponent* MoveComp = PossessedChar->GetCharacterMovement())
		{
			MoveComp->SetUpdateNavAgentWithOwnersCollisions(true);
			MoveComp->UpdateNavAgent(*PossessedChar);
		}
	}

	Super::OnPossess(InPawn);

	if (!CrowdFollowingComponent)
	{
		CrowdFollowingComponent = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	}

	bCrowdSteeringWanted = true;
	bIsTightCorridorMode = false;

	if (CrowdFollowingComponent)
	{
		CrowdFollowingComponent->SetCrowdSeparation(true);
		CrowdFollowingComponent->SetCrowdSeparationWeight(NormalCrowdSeparationWeight);
		CrowdFollowingComponent->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
		CrowdFollowingComponent->SetCrowdAnticipateTurns(true);
		CrowdFollowingComponent->SetCrowdObstacleAvoidance(true);
		CrowdFollowingComponent->SetCrowdAvoidanceRangeMultiplier(1.0f);
		RefreshEffectiveCrowdState();
	}
}

void AMinionAIController::SetCrowdSteeringEnabled(bool bEnabled)
{
	if (bCrowdSteeringWanted != bEnabled)
	{
		bCrowdSteeringWanted = bEnabled;
		RefreshEffectiveCrowdState();
	}
}

void AMinionAIController::SetTightCorridorMode(bool bTight)
{
	if (bIsTightCorridorMode != bTight)
	{
		bIsTightCorridorMode = bTight;
		RefreshEffectiveCrowdState();
	}
}

void AMinionAIController::RefreshEffectiveCrowdState()
{
	if (!CrowdFollowingComponent)
	{
		return;
	}

	// Only run Detour Crowd when explicitly wanted AND NOT on a narrow bridge/corridor.
	// When Disabled, UCrowdFollowingComponent falls back cleanly to standard UPathFollowingComponent.
	const bool bEnableCrowd = bCrowdSteeringWanted && !bIsTightCorridorMode;

	CrowdFollowingComponent->SetCrowdSimulationState(
		bEnableCrowd ? ECrowdSimulationState::Enabled : ECrowdSimulationState::Disabled);

	if (bEnableCrowd)
	{
		CrowdFollowingComponent->SetCrowdSeparation(true);
		CrowdFollowingComponent->SetCrowdObstacleAvoidance(true);
		CrowdFollowingComponent->SetCrowdSeparationWeight(NormalCrowdSeparationWeight);
		CrowdFollowingComponent->SetCrowdAvoidanceRangeMultiplier(1.0f);
		CrowdFollowingComponent->UpdateCrowdAgentParams();
	}
}
