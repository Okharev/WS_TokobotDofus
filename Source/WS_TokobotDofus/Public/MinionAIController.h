// MinionAIController.h
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MinionAIController.generated.h"

class UCrowdFollowingComponent;

/**
 * AI Controller utilizing Detour Crowd steering so minions separate smoothly without capsule collision pushing.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API AMinionAIController : public AAIController
{
	GENERATED_BODY()

public:
	explicit AMinionAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Enables or disables Detour Crowd simulation (disabled during airborne NavLink jumps). */
	void SetCrowdSteeringEnabled(bool bEnabled);

	/** Helper to retrieve the cast CrowdFollowingComponent. */
	FORCEINLINE UCrowdFollowingComponent* GetCrowdComponent() const { return CrowdFollowingComponent; }

protected:
	virtual void OnPossess(APawn* InPawn) override;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Minion|AI")
	TObjectPtr<UCrowdFollowingComponent> CrowdFollowingComponent;
};