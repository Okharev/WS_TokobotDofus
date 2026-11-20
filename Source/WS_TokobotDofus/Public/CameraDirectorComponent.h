// CameraDirectorComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraDirectorComponent.generated.h"

class ACameraZoneActor;

UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UCameraDirectorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Retro Camera")
    void PushZone(ACameraZoneActor* Zone);

    UFUNCTION(BlueprintCallable, Category = "Retro Camera")
    void PopZone(ACameraZoneActor* Zone);

    UFUNCTION(BlueprintCallable, Category = "Retro Camera")
    void ClearAllZones(float BlendOutTime = 0.5f);

    UFUNCTION(BlueprintPure, Category = "Retro Camera")
    bool IsInFixedCameraZone() const { return CurrentActiveZone.IsValid(); }

    // Call from IA_Move (Triggered): converts 2D input (X = Right, Y = Forward) into a world move vector
    UFUNCTION(BlueprintCallable, Category = "Retro Camera|Movement")
    FVector GetWorldMoveDirection(FVector2D MoveInput);

    // Call from IA_Move (Completed / Canceled) to immediately refresh direction on stick release
    UFUNCTION(BlueprintCallable, Category = "Retro Camera|Movement")
    void NotifyMoveInputReleased();

    // Keeps the previous movement direction across camera cuts until stick is released or turned
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement")
    bool bLatchBasisOnCameraSwitch = true;

    // Keeps movement straight while holding steady in a Look-At camera (prevents orbiting/flipping)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement")
    bool bLatchBasisDuringLookAt = true;

    // Turning the stick past this angle (degrees) breaks the latch and smoothly aligns to the new view
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement", meta = (ClampMin = "5.0", ClampMax = "180.0"))
    float StickBreakAngleDegrees = 35.0f;

    // How fast the basis catches up to the live camera after breaking a latch mid-hold
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Camera|Movement", meta = (ClampMin = "1.0"))
    float BasisCatchupSpeed = 8.0f;

private:
    void EvaluateStack(const ACameraZoneActor* ExitingZone = nullptr);
    ACameraZoneActor* GetTopPriorityZone();
    void ComputeScreenBasis(const FRotator& CamRot, FVector& OutForward, FVector& OutRight) const;

    UPROPERTY()
    TArray<TWeakObjectPtr<ACameraZoneActor>> ZoneStack;

    UPROPERTY()
    TWeakObjectPtr<ACameraZoneActor> CurrentActiveZone;

    // Movement latching state
    FVector ActiveForward = FVector::ForwardVector;
    FVector ActiveRight = FVector::RightVector;
    FVector2D LatchedStickDir = FVector2D::ZeroVector;
    uint64 LastMoveInputFrame = 0;
    bool bHasValidBasis = false;
    bool bIsBasisLatched = false;
    bool bNeedsSmoothCatchup = false;
};