// MinionCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MinionAgentInterface.h"
#include "MinionCharacter.generated.h"

class AAIController;
class AMinionAIController;
class ANavLinkProxy;

/**
 * Self-contained Minion Character with zero-push collision, player evasion,
 * Smart NavLink jumping, Chaos Whip simulation, and Carry Palanquin mode.
 */
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

	/** Current state visible in the Details panel during Play mode. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Minion|State")
	EMinionState CurrentState = EMinionState::Idle;

	/** Distance (in cm) the minion steps aside when evading the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|Avoidance")
	float EvadeStepDistance = 140.0f;

	/** Cooldown (in seconds) before a minion returns to normal squad slot updates after evading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|Avoidance")
	float EvadeRecoveryTime = 0.35f;

	/** Acceptance radius (in cm) when moving to a formation slot. */
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