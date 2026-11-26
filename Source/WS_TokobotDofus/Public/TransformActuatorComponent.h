// TransformActuatorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TransformActuatorComponent.generated.h"

class UCurveFloat;
class UActivationAggregatorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActuatorMotionEventSignature, bool, bMovingToActive);

UCLASS(ClassGroup = (MinionInteraction), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UTransformActuatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTransformActuatorComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Actuator")
	void SetTargetAlpha(float NewTargetAlpha, bool bInstant = false);

	UPROPERTY(BlueprintAssignable, Category = "Actuator|Events")
	FOnActuatorMotionEventSignature OnMotionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Actuator|Events")
	FOnActuatorMotionEventSignature OnMotionFinished;

protected:
	/** SceneComponent to move (e.g., DoorMesh or BridgeMesh). Falls back to Owner's RootComponent if empty. */
	UPROPERTY(EditAnywhere, Category = "Actuator")
	FComponentReference TargetComponentRef;

	/** Automatically binds to a sibling UActivationAggregatorComponent on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator")
	bool bAutoBindToOwnerAggregator = true;

	/** If true, tracks continuous 0..1 progress (partial bridge extension). If false, snaps target to 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator", meta = (EditCondition = "bAutoBindToOwnerAggregator"))
	bool bDriveFromProportionalProgress = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion")
	FVector RelativeLocationOffset = FVector(0.0f, 0.0f, 300.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion")
	FRotator RelativeRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion")
	FVector RelativeScaleOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion", meta = (ClampMin = "0.05", Units = "s"))
	float OpenDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion", meta = (ClampMin = "0.05", Units = "s"))
	float CloseDuration = 0.75f;

	/** Optional easing curve (0..1 -> 0..1). Linear interpolation if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Actuator|Motion")
	TObjectPtr<UCurveFloat> MotionCurve = nullptr;

private:
	UFUNCTION()
	void HandleAggregateStateChanged(bool bIsActivated, bool bInstant);

	UFUNCTION()
	void HandleAggregateProgressChanged(float NormalizedAlpha, bool bInstant);

	void ApplyTransformForAlpha(float LinearAlpha);

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TargetSceneComp = nullptr;

	FTransform InitialRelativeTransform;
	float CurrentAlpha = 0.0f;
	float TargetAlpha = 0.0f;
};