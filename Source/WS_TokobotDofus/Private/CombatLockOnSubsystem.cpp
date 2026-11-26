#include "CombatLockOnSubsystem.h"
#include "CombatLockOnTargetComponent.h"

void UCombatLockOnSubsystem::RegisterTarget(UCombatLockOnTargetComponent* TargetComp)
{
	if (!IsValid(TargetComp))
	{
		return;
	}

	RegisteredTargets.AddUnique(TargetComp);
}

void UCombatLockOnSubsystem::UnregisterTarget(UCombatLockOnTargetComponent* TargetComp)
{
	RegisteredTargets.RemoveAllSwap([TargetComp](const TWeakObjectPtr<UCombatLockOnTargetComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == TargetComp;
	});
}
