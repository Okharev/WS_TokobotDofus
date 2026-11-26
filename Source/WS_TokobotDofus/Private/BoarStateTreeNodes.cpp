// BoarStateTreeNodes.cpp
#include "BoarStateTreeNodes.h"
#include "BoarAIController.h"
#include "BoarEnemyCharacter.h"
#include "BoarRushComponent.h"
#include "StateTreeExecutionContext.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"

// ----------------------------------------------------------------------------
// FBoarStatusEvaluator
// ----------------------------------------------------------------------------
void FBoarStatusEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.AIController || !Data.BoarPawn)
	{
		return;
	}

	const FBoarEnemyTuning& Tuning = Data.BoarPawn->GetTuning();
	const UBoarRushComponent* RushComp = Data.BoarPawn->GetRushComponent();
	const FVector BoarLoc = Data.BoarPawn->GetActorLocation();

	Data.SpawnLocation = Data.AIController->GetSpawnLocation();
	Data.DistanceToSpawn = FVector::Dist2D(BoarLoc, Data.SpawnLocation);
	Data.bOutsidePatrolZone = Data.DistanceToSpawn > Tuning.PatrolRadius;
	Data.PostRushRecoveryDuration = Tuning.PostRushRecoveryDuration;
	Data.bIsStunnedVulnerable = RushComp ? RushComp->IsStunnedVulnerable() : false;

	AActor* CurrentTarget = Data.AIController->GetTargetActor();
	if (!IsValid(CurrentTarget))
	{
		Data.TargetActor = nullptr;
		Data.DistanceToTarget = BIG_NUMBER;
		Data.bHasTarget = false;
		return;
	}

	Data.DistanceToTarget = FVector::Dist2D(BoarLoc, CurrentTarget->GetActorLocation());

	// Enforce aggro drop, but never cut off a rush or post-clash stun mid-execution
	const bool bBusyInRushOrStun = RushComp && (RushComp->IsRushing() || RushComp->IsStunnedVulnerable());
	if (!bBusyInRushOrStun && Data.DistanceToTarget > Tuning.AggroDropRange)
	{
		Data.AIController->ClearTargetActor();
		Data.TargetActor = nullptr;
		Data.DistanceToTarget = BIG_NUMBER;
		Data.bHasTarget = false;
		return;
	}

	Data.TargetActor = CurrentTarget;
	Data.bHasTarget = true;
}

// ----------------------------------------------------------------------------
// FBoarFindPatrolPointTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBoarFindPatrolPointTask::EnterState(FStateTreeExecutionContext& Context,
                                                         const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.AIController || !Data.BoarPawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	const FVector Origin = Data.AIController->GetSpawnLocation();
	const float Radius = Data.BoarPawn->GetTuning().PatrolRadius;

	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Data.BoarPawn->GetWorld()))
	{
		FNavLocation RandomPoint;
		if (NavSys->GetRandomReachablePointInRadius(Origin, Radius, RandomPoint))
		{
			Data.OutPatrolLocation = RandomPoint.Location;
			// Stay Running so sibling built-in Move To task owns state completion
			return EStateTreeRunStatus::Running;
		}
	}

	Data.OutPatrolLocation = Origin;
	return EStateTreeRunStatus::Running;
}

// ----------------------------------------------------------------------------
// FBoarChargeUpTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBoarChargeUpTask::EnterState(FStateTreeExecutionContext& Context,
                                                  const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.AIController || !Data.BoarPawn || !IsValid(Data.TargetActor))
	{
		return EStateTreeRunStatus::Failed;
	}

	UBoarRushComponent* RushComp = Data.BoarPawn->GetRushComponent();
	if (!RushComp)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.AIController->StopMovement();
	Data.BoarPawn->SetAIState(EBoarAIState::ChargeUp);

	return RushComp->StartChargeUp(Data.TargetActor)
		       ? EStateTreeRunStatus::Running
		       : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FBoarChargeUpTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.BoarPawn || !Data.BoarPawn->GetRushComponent())
	{
		return EStateTreeRunStatus::Failed;
	}

	return Data.BoarPawn->GetRushComponent()->IsChargingUp()
		       ? EStateTreeRunStatus::Running
		       : EStateTreeRunStatus::Succeeded;
}

void FBoarChargeUpTask::ExitState(FStateTreeExecutionContext& Context,
                                  const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.BoarPawn && Data.BoarPawn->GetRushComponent() && Data.BoarPawn->GetRushComponent()->IsChargingUp())
	{
		Data.BoarPawn->GetRushComponent()->CancelActiveAction();
	}
}

// ----------------------------------------------------------------------------
// FBoarExecuteRushTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBoarExecuteRushTask::EnterState(FStateTreeExecutionContext& Context,
                                                     const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.AIController || !Data.BoarPawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	UBoarRushComponent* RushComp = Data.BoarPawn->GetRushComponent();
	if (!RushComp)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.AIController->StopMovement();
	Data.BoarPawn->SetAIState(EBoarAIState::Rushing);

	return RushComp->ExecuteRush()
		       ? EStateTreeRunStatus::Running
		       : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FBoarExecuteRushTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.BoarPawn || !Data.BoarPawn->GetRushComponent())
	{
		return EStateTreeRunStatus::Failed;
	}

	const UBoarRushComponent* RushComp = Data.BoarPawn->GetRushComponent();

	// Stay Running if a clash stunned us so Active's Critical OnTick -> Stunned transition takes over cleanly
	if (RushComp->IsStunnedVulnerable())
	{
		return EStateTreeRunStatus::Running;
	}

	return RushComp->IsRushing()
		       ? EStateTreeRunStatus::Running
		       : EStateTreeRunStatus::Succeeded;
}

void FBoarExecuteRushTask::ExitState(FStateTreeExecutionContext& Context,
                                     const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.BoarPawn && Data.BoarPawn->GetRushComponent() && Data.BoarPawn->GetRushComponent()->IsRushing())
	{
		Data.BoarPawn->GetRushComponent()->CancelActiveAction();
	}
}

// ----------------------------------------------------------------------------
// FBoarStunnedTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBoarStunnedTask::EnterState(FStateTreeExecutionContext& Context,
                                                 const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.AIController || !Data.BoarPawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.AIController->StopMovement();
	Data.BoarPawn->SetAIState(EBoarAIState::StunnedVulnerable);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBoarStunnedTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.BoarPawn || !Data.BoarPawn->GetRushComponent())
	{
		return EStateTreeRunStatus::Failed;
	}

	return Data.BoarPawn->GetRushComponent()->IsStunnedVulnerable()
		       ? EStateTreeRunStatus::Running
		       : EStateTreeRunStatus::Succeeded;
}

// ----------------------------------------------------------------------------
// FBoarSetAIStateTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBoarSetAIStateTask::EnterState(FStateTreeExecutionContext& Context,
                                                    const FStateTreeTransitionResult& Transition) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (Data.BoarPawn)
	{
		Data.BoarPawn->SetAIState(Data.DesiredState);
	}
	// Stay Running so sibling tasks (Delay / Move To) control state completion
	return EStateTreeRunStatus::Running;
}
