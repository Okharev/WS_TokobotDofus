// HitReactableInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CombatFeedbackTypes.h"
#include "HitReactableInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UHitReactableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by any enemy or destructible actor that reacts to Minion Whip / CarryRush combat hits.
 */
class WS_TOKOBOTDOFUS_API IHitReactableInterface
{
	GENERATED_BODY()

public:
	/** Processes heart damage, knockback queueing, and hit flash. Returns acceptance & kill state. */
	virtual FCombatHitResponse ReceiveCombatHit(const FCombatHitPayload& HitPayload) = 0;
};
