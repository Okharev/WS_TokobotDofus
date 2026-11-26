#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatLockOnSubsystem.generated.h"

class UCombatLockOnTargetComponent;

/**
 * Lightweight world registry for all lockable combat targets.
 * Avoids expensive TActorIterator / overlap sphere sweeps during lock-on queries.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API UCombatLockOnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterTarget(UCombatLockOnTargetComponent* TargetComp);
	void UnregisterTarget(UCombatLockOnTargetComponent* TargetComp);

	const TArray<TWeakObjectPtr<UCombatLockOnTargetComponent>>& GetRegisteredTargets() const
	{
		return RegisteredTargets;
	}

private:
	TArray<TWeakObjectPtr<UCombatLockOnTargetComponent>> RegisteredTargets;
};
