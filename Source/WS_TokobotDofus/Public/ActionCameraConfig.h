#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ActionCameraConfig.generated.h"

USTRUCT(BlueprintType)
struct FCameraWhiskerDef
{
    GENERATED_BODY()

    /** Positive rotates arm counter-clockwise around player (-Y screen-left); negative is screen-right. */
    UPROPERTY(EditDefaultsOnly)
    float YawOffsetDeg = 20.f;

    /** Fraction of desired arm length to probe (typically 0.85 - 1.0). */
    UPROPERTY(EditDefaultsOnly)
    float LengthScale = 0.92f;

    /** Relative steering influence (inner whiskers should weigh higher than outer whiskers). */
    UPROPERTY(EditDefaultsOnly)
    float SteeringWeight = 1.f;
};

UCLASS(BlueprintType)
class UActionCameraConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // --- Base Orbit ---
    UPROPERTY(EditDefaultsOnly, Category = "Orbit")
    float DefaultArmLength = 420.f;

    UPROPERTY(EditDefaultsOnly, Category = "Orbit")
    float DefaultFOV = 78.f;

    UPROPERTY(EditDefaultsOnly, Category = "Orbit")
    float MinPitch = -72.f;

    UPROPERTY(EditDefaultsOnly, Category = "Orbit")
    float MaxPitch = 55.f;

    UPROPERTY(EditDefaultsOnly, Category = "Orbit")
    FVector FreeSocketOffset = FVector(0.f, 0.f, 70.f);

    // --- Respectful Auto-Follow ---
    /** Seconds after manual stick/mouse look before auto-follow is allowed to resume. */
    UPROPERTY(EditDefaultsOnly, Category = "AutoFollow")
    float ManualLookSuppressTime = 1.4f;

    /** Minimum planar speed (cm/s) required to trigger gentle yaw drift. */
    UPROPERTY(EditDefaultsOnly, Category = "AutoFollow")
    float MinSpeedForAutoFollow = 140.f;

    /** Max degrees/sec when running perpendicular to the camera. */
    UPROPERTY(EditDefaultsOnly, Category = "AutoFollow")
    float AutoFollowMaxYawRate = 55.f;

    /** Suppress auto-follow when Dot(MoveDir, CamForward) falls below this (running toward lens). */
    UPROPERTY(EditDefaultsOnly, Category = "AutoFollow")
    float TowardCameraDeadzoneDot = -0.75f;

    // --- Multi-Feeler Whiskers ---
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Whiskers")
    TArray<FCameraWhiskerDef> Whiskers = {
        { -18.f, 0.95f, 1.0f },
        {  18.f, 0.95f, 1.0f },
        { -38.f, 0.80f, 0.55f },
        {  38.f, 0.80f, 0.55f }
    };

    UPROPERTY(EditDefaultsOnly, Category = "Collision|Whiskers")
    float WhiskerProbeRadius = 10.f;

    /** Max degrees/sec the whiskers can nudge yaw to glide past obstacles. */
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Whiskers")
    float WhiskerMaxYawNudgeRate = 70.f;

    /** Minimum player speed to allow whisker yaw steering (prevents idle drift). */
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Whiskers")
    float MinSpeedForWhiskerSteer = 80.f;

    /** How strongly bilateral whisker hits pre-compress arm length in tight corridors (0 - 1). */
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Whiskers")
    float CorridorSqueezeAlpha = 0.65f;

    // --- Center Probe & Hysteresis ---
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float CenterProbeRadius = 14.f;

    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float WallPullInPadding = 8.f;

    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float MinArmLength = 50.f;

    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float PullInInterpSpeed = 28.f;

    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float PullOutInterpSpeed = 5.5f;

    /** Hold compressed arm length for this duration after an obstacle clears to stop pole-popping. */
    UPROPERTY(EditDefaultsOnly, Category = "Collision|Hysteresis")
    float PullOutHysteresisDelay = 0.18f;
};