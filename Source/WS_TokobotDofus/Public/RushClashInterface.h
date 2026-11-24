// RushClashInterface.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RushClashInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class URushClashInterface : public UInterface
{
	GENERATED_BODY()
};

class WS_TOKOBOTDOFUS_API IRushClashInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat|RushClash")
	bool IsCarryRushing() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat|RushClash")
	float GetCarryRushSpeed() const;

	/** Normalized 2D rush heading for accurate head-on clash angle checks. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat|RushClash")
	FVector GetCarryRushDirection() const;

	/** Called by the Boar on a valid head-on clash to cancel the player's rush and apply recoil (0 damage). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat|RushClash")
	void OnRushClashed(AActor* ClashedOpponent, const FVector& RecoilVelocity);
};