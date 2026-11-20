// CameraDirectorComponent.cpp
#include "CameraDirectorComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CameraZoneActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UCameraDirectorComponent::PushZone(ACameraZoneActor* Zone)
{
    if (!Zone) return;

    // Move to top of recency order if re-entered
    ZoneStack.Remove(Zone);
    ZoneStack.Add(Zone);

    EvaluateStack();
}

void UCameraDirectorComponent::PopZone(ACameraZoneActor* Zone)
{
    if (!Zone) return;

    ZoneStack.Remove(Zone);
    EvaluateStack(Zone);
}

void UCameraDirectorComponent::ClearAllZones(float BlendOutTime)
{
    ZoneStack.Empty();
    CurrentActiveZone.Reset();

    if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
    {
        if (APawn* Pawn = PC->GetPawn())
        {
            PC->SetViewTargetWithBlend(Pawn, BlendOutTime, VTBlend_Cubic, 2.0f, false);
        }
    }
}

ACameraZoneActor* UCameraDirectorComponent::GetTopPriorityZone()
{
    // Prune any destroyed/streamed-out zones first
    ZoneStack.RemoveAll([](const TWeakObjectPtr<ACameraZoneActor>& Ptr) { return !Ptr.IsValid(); });

    ACameraZoneActor* BestZone = nullptr;
    for (const TWeakObjectPtr<ACameraZoneActor>& Entry : ZoneStack)
    {
        ACameraZoneActor* Candidate = Entry.Get();
        // >= ensures newer zones win ties at equal priority
        if (!BestZone || Candidate->Priority >= BestZone->Priority)
        {
            BestZone = Candidate;
        }
    }
    return BestZone;
}

// Add to includes at top of CameraDirectorComponent.cpp:
#include "Camera/PlayerCameraManager.h"

void UCameraDirectorComponent::EvaluateStack(const ACameraZoneActor* ExitingZone)
{
    APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC) return;

    ACameraZoneActor* NextZone = GetTopPriorityZone();
    if (NextZone == CurrentActiveZone.Get())
    {
        return;
    }

    // Latch the outgoing movement basis if the player is actively holding a direction during the cut
    const bool bWasMoving = bHasValidBasis && (GFrameCounter - LastMoveInputFrame) <= 2;
    if (bLatchBasisOnCameraSwitch && bWasMoving)
    {
        bIsBasisLatched = true;
        bNeedsSmoothCatchup = false;
    }

    CurrentActiveZone = NextZone;

    if (NextZone)
    {
        NextZone->NotifyActivated(PC->GetPawn());
        PC->SetViewTarget(NextZone, NextZone->BlendIn.ToTransitionParams());
    }
    else if (APawn* Pawn = PC->GetPawn())
    {
        FViewTargetTransitionParams OutParams;
        if (ExitingZone)
        {
            OutParams = ExitingZone->BlendOut.ToTransitionParams();
        }
        else
        {
            OutParams.BlendTime = 0.5f;
            OutParams.BlendFunction = VTBlend_Cubic;
            OutParams.BlendExp = 2.0f;
        }

        PC->SetViewTarget(Pawn, OutParams);
    }
}

void UCameraDirectorComponent::NotifyMoveInputReleased()
{
    bIsBasisLatched = false;
    bNeedsSmoothCatchup = false;
}

void UCameraDirectorComponent::ComputeScreenBasis(const FRotator& CamRot, FVector& OutForward, FVector& OutRight) const
{
    // Derive Right from Camera Y first, then cross with World Up.
    // This stays completely gimbal-safe even if a puzzle camera points straight down (-90 pitch).
    const FRotationMatrix CamMatrix(CamRot);
    FVector Right = CamMatrix.GetUnitAxis(EAxis::Y);
    Right.Z = 0.f;

    if (!Right.Normalize())
    {
        Right = FVector::RightVector;
    }

    // In UE's left-handed Z-up space, Cross(Right, Up) = Forward
    const FVector Forward = FVector::CrossProduct(Right, FVector::UpVector).GetSafeNormal();

    OutForward = Forward;
    OutRight = Right;
}

FVector UCameraDirectorComponent::GetWorldMoveDirection(FVector2D MoveInput)
{
    if (MoveInput.SizeSquared() < 0.01f)
    {
        NotifyMoveInputReleased();
        return FVector::ZeroVector;
    }

    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !PC->PlayerCameraManager)
    {
        return FVector::ZeroVector;
    }

    FVector LiveForward, LiveRight;
    ComputeScreenBasis(PC->PlayerCameraManager->GetCameraRotation(), LiveForward, LiveRight);

    // Treat a 2+ frame gap in input as a fresh press (works even if Completed wasn't bound)
    const bool bFreshPress = !bHasValidBasis || (GFrameCounter - LastMoveInputFrame) > 2;
    LastMoveInputFrame = GFrameCounter;
    bHasValidBasis = true;

    const FVector2D InputDir = MoveInput.GetSafeNormal();
    const bool bInLookAtZone = CurrentActiveZone.IsValid() && CurrentActiveZone->LookAt.bLookAtPlayer;

    if (bFreshPress)
    {
        ActiveForward = LiveForward;
        ActiveRight = LiveRight;
        LatchedStickDir = InputDir;
        bNeedsSmoothCatchup = false;
        bIsBasisLatched = (bInLookAtZone && bLatchBasisDuringLookAt);
    }
    else if (bIsBasisLatched)
    {
        const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(StickBreakAngleDegrees));
        if (FVector2D::DotProduct(InputDir, LatchedStickDir) < CosThreshold)
        {
            // Player intentionally steered in a new direction: unlatch and blend smoothly to the live view
            bIsBasisLatched = false;
            bNeedsSmoothCatchup = true;
        }
    }
    else
    {
        LatchedStickDir = InputDir;

        if (bNeedsSmoothCatchup)
        {
            const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
            const float CurrentYaw = ActiveForward.Rotation().Yaw;
            const float TargetYaw = LiveForward.Rotation().Yaw;
            const float NewYaw = FMath::RInterpTo(
                FRotator(0.f, CurrentYaw, 0.f),
                FRotator(0.f, TargetYaw, 0.f),
                DeltaTime,
                BasisCatchupSpeed
            ).Yaw;

            ActiveForward = FRotator(0.f, NewYaw, 0.f).Vector();
            ActiveRight = FVector::CrossProduct(FVector::UpVector, ActiveForward).GetSafeNormal();

            if (FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(NewYaw, TargetYaw), 0.5f))
            {
                bNeedsSmoothCatchup = false;
            }
        }
        else
        {
            ActiveForward = LiveForward;
            ActiveRight = LiveRight;
        }
    }

    // X = Right/Left, Y = Forward/Back (standard UE5 Enhanced Input IA_Move layout)
    const FVector WorldDir = (ActiveRight * MoveInput.X) + (ActiveForward * MoveInput.Y);
    return WorldDir.GetClampedToMaxSize(1.0f);
}