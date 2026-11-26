#include "MinionThrowTargetSubsystem.h"
#include "MinionThrowTargetComponent.h"

void UMinionThrowTargetSubsystem::RegisterTarget(UMinionThrowTargetComponent* TargetComp)
{
	if (!IsValid(TargetComp))
	{
		return;
	}

	RegisteredTargets.AddUnique(TargetComp);
}

void UMinionThrowTargetSubsystem::UnregisterTarget(UMinionThrowTargetComponent* TargetComp)
{
	RegisteredTargets.RemoveAllSwap([TargetComp](const TWeakObjectPtr<UMinionThrowTargetComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == TargetComp;
	});
}
