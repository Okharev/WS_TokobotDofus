// MinionAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MinionAIController.generated.h"

class UCrowdFollowingComponent;

UCLASS()
class WS_TOKOBOTDOFUS_API AMinionAIController : public AAIController
{
	GENERATED_BODY()

public:
	explicit AMinionAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Enables or disables Detour Crowd simulation (disabled during NavLink jumps, Whip chains, Carry stance, and Tier 1 unstuck). */
	void SetCrowdSteeringEnabled(bool bEnabled);

	/** Fully disables Detour Crowd RVO/separation on narrow bridges so minions walk single-file without velocity-obstacle deadlocks. */
	void SetTightCorridorMode(bool bTight);

	FORCEINLINE UCrowdFollowingComponent* GetCrowdComponent() const { return CrowdFollowingComponent; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minion|AI")
	float NormalCrowdSeparationWeight = 2.0f;

private:
	void RefreshEffectiveCrowdState();

	UPROPERTY(VisibleInstanceOnly, Category = "Minion|AI")
	TObjectPtr<UCrowdFollowingComponent> CrowdFollowingComponent;

	bool bCrowdSteeringWanted = true;
	bool bIsTightCorridorMode = false;
};