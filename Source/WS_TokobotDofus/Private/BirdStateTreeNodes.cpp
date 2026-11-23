// BirdStateTreeNodes.cpp
#include "BirdStateTreeNodes.h"
#include "BirdAIController.h"
#include "BirdEnemyCharacter.h"
#include "StateTreeExecutionContext.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"

// ----------------------------------------------------------------------------
// FBirdStatusEvaluator
// ----------------------------------------------------------------------------
void FBirdStatusEvaluator::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (!Data.AIController || !Data.BirdPawn)
    {
        return;
    }

    const FBirdEnemyTuning& Tuning = Data.BirdPawn->GetTuning();
    const FVector BirdLoc = Data.BirdPawn->GetActorLocation();

    Data.SpawnLocation = Data.AIController->GetSpawnLocation();
    Data.DistanceToSpawn = FVector::Dist2D(BirdLoc, Data.SpawnLocation);
    Data.bOutsidePatrolZone = Data.DistanceToSpawn > Tuning.PatrolRadius;
    Data.AttackAcceptanceRadius = Tuning.AttackAcceptanceRadius;

    AActor* CurrentTarget = Data.AIController->GetTargetActor();
    if (!IsValid(CurrentTarget))
    {
        Data.TargetActor = nullptr;
        Data.DistanceToTarget = BIG_NUMBER;
        Data.bHasTarget = false;
        Data.bInAttackRange = false;
        return;
    }

    Data.DistanceToTarget = FVector::Dist2D(BirdLoc, CurrentTarget->GetActorLocation());

    // Enforce 11m aggro drop
    if (Data.DistanceToTarget > Tuning.AggroDropRange)
    {
        Data.AIController->ClearTargetActor();
        Data.TargetActor = nullptr;
        Data.DistanceToTarget = BIG_NUMBER;
        Data.bHasTarget = false;
        Data.bInAttackRange = false;
        return;
    }

    Data.TargetActor = CurrentTarget;
    Data.bHasTarget = true;
    Data.bInAttackRange = Data.DistanceToTarget <= Tuning.AttackRange;
}

// ----------------------------------------------------------------------------
// FBirdFindPatrolPointTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBirdFindPatrolPointTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (!Data.AIController || !Data.BirdPawn)
    {
        return EStateTreeRunStatus::Failed;
    }

    const FVector Origin = Data.AIController->GetSpawnLocation();
    const float Radius = Data.BirdPawn->GetTuning().PatrolRadius;

    if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(Data.BirdPawn->GetWorld()))
    {
        FNavLocation RandomPoint;
        if (NavSys->GetRandomReachablePointInRadius(Origin, Radius, RandomPoint))
        {
            Data.OutPatrolLocation = RandomPoint.Location;
            // Stay Running so the paired built-in Move To task owns state completion
            return EStateTreeRunStatus::Running;
        }
    }

    Data.OutPatrolLocation = Origin;
    return EStateTreeRunStatus::Running;
}

// ----------------------------------------------------------------------------
// FBirdAttackTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBirdAttackTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (!Data.AIController || !Data.BirdPawn || !IsValid(Data.TargetActor))
    {
        return EStateTreeRunStatus::Failed;
    }

    Data.bWaitingForCooldown = false;
    Data.CooldownRemaining = Data.BirdPawn->GetTuning().AttackCooldown;

    Data.BirdPawn->SetAIState(EBirdAIState::Attack);
    Data.AIController->SetFocus(Data.TargetActor);
    Data.BirdPawn->GetCharacterMovement()->StopMovementImmediately();

    if (!Data.BirdPawn->StartAttack(Data.TargetActor))
    {
        return EStateTreeRunStatus::Failed;
    }

    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBirdAttackTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (!Data.BirdPawn)
    {
        return EStateTreeRunStatus::Failed;
    }

    if (!Data.bWaitingForCooldown)
    {
        if (!Data.BirdPawn->IsAttacking())
        {
            Data.bWaitingForCooldown = true;
            if (Data.AIController)
            {
                Data.AIController->ClearFocus(EAIFocusPriority::Gameplay);
            }
        }
        return EStateTreeRunStatus::Running;
    }

    Data.CooldownRemaining -= DeltaTime;
    return (Data.CooldownRemaining <= 0.f) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

void FBirdAttackTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (Data.BirdPawn)
    {
        Data.BirdPawn->CancelAttack();
    }
    if (Data.AIController)
    {
        Data.AIController->ClearFocus(EAIFocusPriority::Gameplay);
    }
}

// ----------------------------------------------------------------------------
// FBirdSetAIStateTask
// ----------------------------------------------------------------------------
EStateTreeRunStatus FBirdSetAIStateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
    FInstanceDataType& Data = Context.GetInstanceData(*this);
    if (Data.BirdPawn)
    {
        Data.BirdPawn->SetAIState(Data.DesiredState);
    }
    // Stay Running so sibling tasks (Delay / Move To) control state completion
    return EStateTreeRunStatus::Running;
}