// LevelActivatableInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LevelActivatableInterface.generated.h"

USTRUCT(BlueprintType)
struct FLevelActivationPayload
{
	GENERATED_BODY()

	/** The trigger/relay actor or component driving this signal (used for N-to-1 tracking). */
	UPROPERTY(BlueprintReadWrite, Category = "Activation")
	TObjectPtr<UObject> SourceObject = nullptr;

	/** Binary active state (e.g., threshold met, switch toggled ON). */
	UPROPERTY(BlueprintReadWrite, Category = "Activation")
	bool bActive = false;

	/** Normalized 0..1 progress (e.g., 2/4 latched minions = 0.5 for partial bridge extension). */
	UPROPERTY(BlueprintReadWrite, Category = "Activation")
	float NormalizedValue = 0.0f;

	/** If true, snap immediately to the target state without animation/VFX (checkpoint or save load). */
	UPROPERTY(BlueprintReadWrite, Category = "Activation")
	bool bInstant = false;
};

UINTERFACE(MinimalAPI, BlueprintType)
class ULevelActivatableInterface : public UInterface
{
	GENERATED_BODY()
};

class WS_TOKOBOTDOFUS_API ILevelActivatableInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "LevelActivation")
	void ReceiveActivationSignal(const FLevelActivationPayload& Payload);
};
