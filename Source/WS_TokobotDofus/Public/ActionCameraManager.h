// ActionCameraManager.h
#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "ActionCameraManager.generated.h"

class UActionCameraConfig;
class UCameraDirectorComponent;

USTRUCT()
struct FActionCameraState
{
	GENERATED_BODY()

	FVector PivotLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	float ArmLength = 420.f;
	float FOV = 78.f;
	FVector LocalSocketOffset = FVector(0.f, 0.f, 70.f);
};

UCLASS()
class WS_TOKOBOTDOFUS_API AActionCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "ActionCamera")
	void NotifyManualLookInput(float YawDelta, float PitchDelta);

	UFUNCTION(BlueprintCallable, Category = "ActionCamera")
	void TriggerRecenter();
	void SetLockedTarget(AActor* NewTarget);

protected:
	virtual void BeginPlay() override;
	virtual void InitializeFor(APlayerController* PC) override;
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	UPROPERTY(EditDefaultsOnly, Category = "Config")
	TObjectPtr<UActionCameraConfig> Config;

private:
	bool IsInFixedCameraZone() const;
	void EvaluateFreeOrbit(const APawn* Pawn, float DeltaTime, FActionCameraState& OutState);
	void ApplyRespectfulAutoFollow(const APawn* Pawn, float DeltaTime);
	void EvaluateZTarget(const APawn* Pawn, const AActor* Target, float DeltaTime, FActionCameraState& OutState);
	float EvaluateWhiskers(const APawn* Pawn, const FVector& Anchor, const FRotator& ViewRot, float DesiredArmLength, float DeltaTime, bool bAllowYawSteer);
	FVector ResolveCameraCollision(const APawn* Pawn, const FVector& Anchor, const FVector& DesiredLoc, float PredictiveMaxDist, float DeltaTime);

	TWeakObjectPtr<UCameraDirectorComponent> CachedDirector;
	TWeakObjectPtr<AActor> LockedTarget;
	FRotator OrbitRotation = FRotator(-15.f, 0.f, 0.f);

	bool bIsRecentering = false;
	double LastManualLookTime = -10.0;
	double LastCompressionTime = -10.0;
	float CurrentArmLength = 420.f;
	float LockOnBlendAlpha = 0.f;
};