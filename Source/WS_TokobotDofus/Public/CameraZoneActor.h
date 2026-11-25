// CameraZoneActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "CameraZoneActor.generated.h"

class UBoxComponent;
class UCameraComponent;
class UCameraDirectorComponent;

USTRUCT(BlueprintType)
struct FCameraZoneBlendSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0"))
	float BlendTime = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	TEnumAsByte<EViewTargetBlendFunction> BlendFunction = VTBlend_Cubic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0"))
	float BlendExp = 2.0f;

	// Locks the outgoing view target frame during the blend (useful for snappy retro transitions)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	bool bLockOutgoing = false;

	FViewTargetTransitionParams ToTransitionParams() const
	{
		FViewTargetTransitionParams Params;
		Params.BlendTime = BlendTime;
		Params.BlendFunction = BlendFunction;
		Params.BlendExp = BlendExp;
		Params.bLockOutgoing = bLockOutgoing;
		return Params;
	}
};

USTRUCT(BlueprintType)
struct FCameraZoneLookAtSettings
{
	GENERATED_BODY()

	// False = completely static angle; True = fixed camera position tracking the player
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt")
	bool bLookAtPlayer = false;

	// Offset from player root (e.g., Z = +60 to aim at chest/head instead of feet)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt", meta = (EditCondition = "bLookAtPlayer"))
	FVector TargetOffset = FVector(0.f, 0.f, 60.f);

	// When Z-Targeting inside a LookAt zone, bias the look-at point toward the locked enemy to frame the duel
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|LockOn", meta = (EditCondition = "bLookAtPlayer"))
	bool bFrameCombatLockTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|LockOn", meta = (EditCondition = "bLookAtPlayer && bFrameCombatLockTarget", ClampMin = "0.0", ClampMax = "0.75"))
	float CombatLockFocusWeight = 0.35f;

	// Mirrors USpringArmComponent::bEnableCameraLag (trails a virtual aim point behind the player)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer"))
	bool bEnableTargetLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer && bEnableTargetLag", ClampMin = "0.0"))
	float TargetLagSpeed = 6.0f;

	// Max world-unit distance the aim point can fall behind the player (0 = unclamped)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer && bEnableTargetLag", ClampMin = "0.0"))
	float TargetLagMaxDistance = 150.0f;

	// Mirrors USpringArmComponent::bEnableCameraRotationLag
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer"))
	bool bEnableRotationLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer && bEnableRotationLag", ClampMin = "0.0"))
	float RotationLagSpeed = 8.0f;

	// Framerate-independent lag substepping (matches USpringArmComponent::bUseCameraLagSubstepping)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer && (bEnableTargetLag || bEnableRotationLag)"))
	bool bUseLagSubstepping = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt|Lag", meta = (EditCondition = "bLookAtPlayer && bUseLagSubstepping", ClampMin = "0.005", ClampMax = "0.5"))
	float LagMaxTimeStep = 1.f / 60.f;

	// True = immediately aim at player on zone entry; False = pan smoothly from the authored FixedCamera angle
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LookAt", meta = (EditCondition = "bLookAtPlayer"))
	bool bSnapAimOnEnter = true;
};

UCLASS()
class WS_TOKOBOTDOFUS_API ACameraZoneActor : public AActor
{
	GENERATED_BODY()

public:
	ACameraZoneActor();

	virtual void BeginPlay() override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	void NotifyActivated(AActor* InTargetActor);

	// Allows Blueprints/puzzles to toggle tracking mid-zone (e.g., pan to a door then back to player)
	UFUNCTION(BlueprintCallable, Category = "Retro Camera")
	void SetLookAtPlayer(bool bEnable, bool bSnapImmediately = false);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Stack")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Blend")
	FCameraZoneBlendSettings BlendIn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Blend")
	FCameraZoneBlendSettings BlendOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Tracking")
	FCameraZoneLookAtSettings LookAt;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FixedCamera;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	TWeakObjectPtr<AActor> TrackedTarget;
	TWeakObjectPtr<UCameraDirectorComponent> CachedDirector;
	FVector PreviousDesiredTargetLoc = FVector::ZeroVector;
	FVector CurrentTargetLoc = FVector::ZeroVector;
	FRotator CurrentLookRotation = FRotator::ZeroRotator;
	bool bHasInitializedLook = false;
};