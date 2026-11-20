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
	// Ensure the pawn's NavAgent radius/height are valid BEFORE Super::OnPossess registers with UCrowdManager
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

	if (CrowdFollowingComponent)
	{
		CrowdFollowingComponent->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
		CrowdFollowingComponent->SetCrowdSeparation(true);
		// Gentle 2.0f separation so inner formation slots never repel approaching minions
		CrowdFollowingComponent->SetCrowdSeparationWeight(2.0f);
		CrowdFollowingComponent->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
		CrowdFollowingComponent->SetCrowdAnticipateTurns(true);
		CrowdFollowingComponent->SetCrowdObstacleAvoidance(true);
		CrowdFollowingComponent->SetCrowdAvoidanceRangeMultiplier(1.0f);
		CrowdFollowingComponent->UpdateCrowdAgentParams();
	}
}

void AMinionAIController::SetCrowdSteeringEnabled(bool bEnabled)
{
	if (CrowdFollowingComponent)
	{
		CrowdFollowingComponent->SetCrowdSimulationState(
			bEnabled ? ECrowdSimulationState::Enabled : ECrowdSimulationState::Disabled);
	}
}