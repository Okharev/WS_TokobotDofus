// BirdAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "CombatFeedbackTypes.h"
#include "BirdAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UStateTreeAIComponent;
class UDamageType;

UCLASS(Blueprintable)
class WS_TOKOBOTDOFUS_API ABirdAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABirdAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UFUNCTION(BlueprintPure, Category = "Bird|AI")
	AActor* GetTargetActor() const { return TargetActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Bird|AI")
	FVector GetSpawnLocation() const { return SpawnLocation; }

	UFUNCTION(BlueprintCallable, Category = "Bird|AI")
	void SetTargetActor(AActor* NewTarget);

	UFUNCTION(BlueprintCallable, Category = "Bird|AI")
	void ClearTargetActor();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|AI")
	TObjectPtr<UAIPerceptionComponent> BirdPerceptionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|AI")
	TObjectPtr<UStateTreeAIComponent> StateTreeAIComp;

private:
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandlePawnCombatHit(const FCombatHitPayload& HitPayload);

	UFUNCTION()
	void HandlePawnTakeAnyDamage(
		AActor* DamagedActor,
		float Damage,
		const UDamageType* DamageType,
		AController* InstigatedBy,
		AActor* DamageCauser);

	void TryAggroFromAttacker(AActor* AttackerActor);
	void ApplySightConfigFromPawn(APawn* InPawn);
	void BindPawnDamageEvents(APawn* InPawn);
	void UnbindPawnDamageEvents(APawn* InPawn);

	TWeakObjectPtr<AActor> TargetActor;
	FVector SpawnLocation = FVector::ZeroVector;
};
