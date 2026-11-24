// BoarAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "CombatFeedbackTypes.h"
#include "BoarAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UStateTreeAIComponent;
class UDamageType;

UCLASS(Blueprintable)
class WS_TOKOBOTDOFUS_API ABoarAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABoarAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	AActor* GetTargetActor() const { return TargetActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Boar|AI")
	FVector GetSpawnLocation() const { return SpawnLocation; }

	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	void SetTargetActor(AActor* NewTarget);

	UFUNCTION(BlueprintCallable, Category = "Boar|AI")
	void ClearTargetActor();

	/** Called when a deflected hit still provokes the Boar into aggroing the attacker. */
	void NotifyDamagedOrProvokedBy(AActor* AttackerActor);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|AI")
	TObjectPtr<UAIPerceptionComponent> BoarPerceptionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|AI")
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