// VoidHazardVolume.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoidRecoveryComponent.h"
#include "VoidHazardVolume.generated.h"

class UBoxComponent;
class UArrowComponent;

UCLASS()
class WS_TOKOBOTDOFUS_API AVoidHazardVolume : public AActor
{
	GENERATED_BODY()

public:
	AVoidHazardVolume();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> HazardBox;

	/** Optional visual marker in editor: if bUseOverrideRespawn is enabled, player respawns here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> OverrideRespawnMarker;

	/** Per-pit void settings (heart penalty, fade color, minion detach policy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoidHazard")
	FVoidFallRequest FallSettings;

private:
	UFUNCTION()
	void OnHazardBeginOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};