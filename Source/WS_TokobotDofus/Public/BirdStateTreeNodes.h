// BirdStateTreeNodes.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "BirdEnemyTypes.h"
#include "BirdStateTreeNodes.generated.h"

class ABirdAIController;
class ABirdEnemyCharacter;

// ============================================================================
// 1. EVALUATOR: Bird Status & 11m Aggro-Drop Enforcement
// ============================================================================
USTRUCT()
struct FBirdStatusEvaluatorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdEnemyCharacter> BirdPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	float DistanceToTarget = BIG_NUMBER;

	UPROPERTY(EditAnywhere, Category = Output)
	float DistanceToSpawn = 0.f;

	UPROPERTY(EditAnywhere, Category = Output)
	float AttackAcceptanceRadius = 150.f;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasTarget = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bInAttackRange = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bOutsidePatrolZone = false;
};

USTRUCT(meta = (DisplayName = "Bird Status Evaluator", Category = "Bird AI"))
struct WS_TOKOBOTDOFUS_API FBirdStatusEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBirdStatusEvaluatorInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ============================================================================
// 2. TASK: Find Patrol Point around SpawnLocation
// ============================================================================
USTRUCT()
struct FBirdFindPatrolPointTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdEnemyCharacter> BirdPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector OutPatrolLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Bird Find Patrol Point", Category = "Bird AI"))
struct WS_TOKOBOTDOFUS_API FBirdFindPatrolPointTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBirdFindPatrolPointTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
	                                       const FStateTreeTransitionResult& Transition) const override;
};

// ============================================================================
// 3. TASK: Face Target, Play Attack Montage, Enforce Cooldown
// ============================================================================
USTRUCT()
struct FBirdAttackTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdEnemyCharacter> BirdPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;

	float CooldownRemaining = 0.f;
	bool bWaitingForCooldown = false;
};

USTRUCT(meta = (DisplayName = "Bird Attack", Category = "Bird AI"))
struct WS_TOKOBOTDOFUS_API FBirdAttackTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBirdAttackTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
	                                       const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context,
	                       const FStateTreeTransitionResult& Transition) const override;
};

// ============================================================================
// 4. TASK: Tag Active AI State for Animations, Audio & VFX
// ============================================================================
USTRUCT()
struct FBirdSetAIStateTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABirdEnemyCharacter> BirdPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EBirdAIState DesiredState = EBirdAIState::Idle;
};

USTRUCT(meta = (DisplayName = "Bird Set AI State", Category = "Bird AI"))
struct WS_TOKOBOTDOFUS_API FBirdSetAIStateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBirdSetAIStateTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
	                                       const FStateTreeTransitionResult& Transition) const override;
};
