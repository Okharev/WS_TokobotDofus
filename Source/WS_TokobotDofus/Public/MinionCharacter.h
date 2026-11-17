// MinionCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MinionAgentInterface.h"
#include "MinionCharacter.generated.h"

class AAIController;
class AMinionAIController;
class ANavLinkProxy;

UCLASS()
class WS_TOKOBOTDOFUS_API AMinionCharacter : public ACharacter, public IMinionAgentInterface
{
	GENERATED_BODY()

public:
	AMinionCharacter();

	// ~Begin IMinionAgentInterface
	virtual EMinionState GetMinionState() const override { return CurrentState; }
	virtual bool MoveToSquadSlot(const FVector& ProjectedSlotLocation) override;
	virtual void EvadePlayer(const FVector& PlayerLocation, const FVector& PlayerVelocity, const FVector& PlayerRight) override;
	virtual void ExecuteNavLinkJump(ANavLinkProxy* NavLink, const FVector& LaunchVelocity) override;
	virtual void SetWhipModeEnabled(bool bEnabled) override;
	virtual void SetCarryRushModeEnabled(bool bEnabled) override;
	// ~End IMinionAgentInterface

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Landed(const FHitResult& Hit) override;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Minion|State")
	EMinionState CurrentState = EMinionState::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|Avoidance")
	float EvadeStepDistance = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|Avoidance")
	float EvadeRecoveryTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|Navigation")
	float SlotAcceptanceRadius = 25.0f;

private:
	AAIController* EnsureAIController();
	void FinishEvadingPlayer();
	void CompleteNavLinkTraversal();

	UPROPERTY(VisibleInstanceOnly, Category = "Minion|State")
	TObjectPtr<AAIController> CachedAIController;

	UPROPERTY(Transient)
	TWeakObjectPtr<ANavLinkProxy> ActiveNavLink;

	FVector CachedSquadSlotLocation = FVector::ZeroVector;
	FVector LastIssuedMoveTarget = FVector::ZeroVector;
	FTimerHandle EvadeTimerHandle;
	FTimerHandle NavLinkSafetyTimerHandle;
};