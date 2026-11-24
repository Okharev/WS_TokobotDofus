// BoarStateTreeNodes.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "BoarEnemyTypes.h"
#include "BoarStateTreeNodes.generated.h"

class ABoarAIController;
class ABoarEnemyCharacter;

// ============================================================================
// 1. EVALUATOR: Boar Status, Aggro-Drop & Stunned Vulnerability
// ============================================================================
USTRUCT()
struct FBoarStatusEvaluatorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Output)
	float DistanceToTarget = BIG_NUMBER;

	UPROPERTY(EditAnywhere, Category = Output)
	float DistanceToSpawn = 0.f;

	UPROPERTY(EditAnywhere, Category = Output)
	float PostRushRecoveryDuration = 0.9f;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasTarget = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bOutsidePatrolZone = false;

	UPROPERTY(EditAnywhere, Category = Output)
	bool bIsStunnedVulnerable = false;
};

USTRUCT(meta = (DisplayName = "Boar Status Evaluator", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarStatusEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStatusEvaluatorInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ============================================================================
// 2. TASK: Find Patrol Point around SpawnLocation
// ============================================================================
USTRUCT()
struct FBoarFindPatrolPointTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Output)
	FVector OutPatrolLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Boar Find Patrol Point", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarFindPatrolPointTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarFindPatrolPointTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// ============================================================================
// 3. TASK: Charge Up (Track Player & Lock Aim)
// ============================================================================
USTRUCT()
struct FBoarChargeUpTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Input)
	TObjectPtr<AActor> TargetActor = nullptr;
};

USTRUCT(meta = (DisplayName = "Boar Charge Up", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarChargeUpTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarChargeUpTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// ============================================================================
// 4. TASK: Execute Rush (Fixed-Distance Charge)
// ============================================================================
USTRUCT()
struct FBoarExecuteRushTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;
};

USTRUCT(meta = (DisplayName = "Boar Execute Rush", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarExecuteRushTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarExecuteRushTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// ============================================================================
// 5. TASK: Stunned Vulnerable (Waits until Post-Clash Stun Expires)
// ============================================================================
USTRUCT()
struct FBoarStunnedTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;
};

USTRUCT(meta = (DisplayName = "Boar Stunned Vulnerable", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarStunnedTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarStunnedTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ============================================================================
// 6. TASK: Tag Active AI State for Animations, Audio & VFX
// ============================================================================
USTRUCT()
struct FBoarSetAIStateTaskInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	TObjectPtr<ABoarEnemyCharacter> BoarPawn = nullptr;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EBoarAIState DesiredState = EBoarAIState::Idle;
};

USTRUCT(meta = (DisplayName = "Boar Set AI State", Category = "Boar AI"))
struct WS_TOKOBOTDOFUS_API FBoarSetAIStateTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBoarSetAIStateTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};