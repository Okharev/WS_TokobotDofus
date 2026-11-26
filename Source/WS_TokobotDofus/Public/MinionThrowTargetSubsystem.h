#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MinionThrowTargetSubsystem.generated.h"

class UMinionThrowTargetComponent;

/**
 * World-level intrusive registry of active UMinionThrowTargetComponents.
 * Eliminates per-frame physics overlap queries for throw targeting.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API UMinionThrowTargetSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterTarget(UMinionThrowTargetComponent* TargetComp);
	void UnregisterTarget(UMinionThrowTargetComponent* TargetComp);

	const TArray<TWeakObjectPtr<UMinionThrowTargetComponent>>& GetRegisteredTargets() const
	{
		return RegisteredTargets;
	}

private:
	TArray<TWeakObjectPtr<UMinionThrowTargetComponent>> RegisteredTargets;
};
