// CameraZoneActor.cpp
#include "CameraZoneActor.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "CameraDirectorComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ACameraZoneActor::ACameraZoneActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
    TriggerBox->SetupAttachment(SceneRoot);
    TriggerBox->SetBoxExtent(FVector(300.f, 300.f, 200.f));
    TriggerBox->SetCollisionProfileName(TEXT("Trigger"));

    FixedCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FixedCamera"));
    FixedCamera->SetupAttachment(SceneRoot);
    FixedCamera->SetRelativeLocation(FVector(-400.f, 0.f, 300.f));
    FixedCamera->SetRelativeRotation(FRotator(-30.f, 0.f, 0.f));
}

void ACameraZoneActor::BeginPlay()
{
    Super::BeginPlay();
    TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACameraZoneActor::OnTriggerBeginOverlap);
    TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ACameraZoneActor::OnTriggerEndOverlap);
}

void ACameraZoneActor::NotifyActivated(AActor* InTargetActor)
{
    TrackedTarget = InTargetActor;
    bHasInitializedLook = false;
}

void ACameraZoneActor::SetLookAtPlayer(bool bEnable, bool bSnapImmediately)
{
    LookAt.bLookAtPlayer = bEnable;
    if (bSnapImmediately)
    {
        bHasInitializedLook = false;
    }
}

void ACameraZoneActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    FixedCamera->GetCameraView(DeltaTime, OutResult);

    if (!LookAt.bLookAtPlayer || !TrackedTarget.IsValid())
    {
        return;
    }

    const FVector DesiredTargetLoc = TrackedTarget->GetActorLocation() + LookAt.TargetOffset;

    if (!bHasInitializedLook)
    {
        PreviousDesiredTargetLoc = DesiredTargetLoc;
        CurrentTargetLoc = DesiredTargetLoc;
        CurrentLookRotation = LookAt.bSnapAimOnEnter
            ? (CurrentTargetLoc - OutResult.Location).GetSafeNormal().Rotation()
            : OutResult.Rotation;
        bHasInitializedLook = true;
    }

    if (DeltaTime > UE_KINDA_SMALL_NUMBER)
    {
        // 1. Target position lag (mirrors USpringArmComponent::UpdateDesiredArmLocation)
        if (LookAt.bEnableTargetLag && LookAt.TargetLagSpeed > 0.f)
        {
            if (LookAt.bUseLagSubstepping && DeltaTime > LookAt.LagMaxTimeStep)
            {
                const FVector TargetVelocity = (DesiredTargetLoc - PreviousDesiredTargetLoc) / DeltaTime;
                FVector LerpTarget = PreviousDesiredTargetLoc;
                float RemainingTime = DeltaTime;

                while (RemainingTime > UE_KINDA_SMALL_NUMBER)
                {
                    const float Step = FMath::Min(LookAt.LagMaxTimeStep, RemainingTime);
                    LerpTarget += TargetVelocity * Step;
                    RemainingTime -= Step;

                    CurrentTargetLoc = FMath::VInterpTo(CurrentTargetLoc, LerpTarget, Step, LookAt.TargetLagSpeed);
                }
            }
            else
            {
                CurrentTargetLoc = FMath::VInterpTo(CurrentTargetLoc, DesiredTargetLoc, DeltaTime, LookAt.TargetLagSpeed);
            }

            if (LookAt.TargetLagMaxDistance > 0.f)
            {
                const FVector FromDesired = CurrentTargetLoc - DesiredTargetLoc;
                if (FromDesired.SizeSquared() > FMath::Square(LookAt.TargetLagMaxDistance))
                {
                    CurrentTargetLoc = DesiredTargetLoc + FromDesired.GetClampedToMaxSize(LookAt.TargetLagMaxDistance);
                }
            }
        }
        else
        {
            CurrentTargetLoc = DesiredTargetLoc;
        }

        PreviousDesiredTargetLoc = DesiredTargetLoc;

        // 2. Rotation lag toward the lagged target point
        const FRotator DesiredRot = (CurrentTargetLoc - OutResult.Location).GetSafeNormal().Rotation();

        if (LookAt.bEnableRotationLag && LookAt.RotationLagSpeed > 0.f)
        {
            if (LookAt.bUseLagSubstepping && DeltaTime > LookAt.LagMaxTimeStep)
            {
                float RemainingTime = DeltaTime;
                while (RemainingTime > UE_KINDA_SMALL_NUMBER)
                {
                    const float Step = FMath::Min(LookAt.LagMaxTimeStep, RemainingTime);
                    RemainingTime -= Step;
                    CurrentLookRotation = FMath::RInterpTo(CurrentLookRotation, DesiredRot, Step, LookAt.RotationLagSpeed);
                }
            }
            else
            {
                CurrentLookRotation = FMath::RInterpTo(CurrentLookRotation, DesiredRot, DeltaTime, LookAt.RotationLagSpeed);
            }
        }
        else
        {
            CurrentLookRotation = DesiredRot;
        }
    }

    OutResult.Rotation = CurrentLookRotation;
}

void ACameraZoneActor::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    APawn* Pawn = Cast<APawn>(OtherActor);
    if (!Pawn || !Pawn->IsLocallyControlled()) return;

    if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
    {
        if (UCameraDirectorComponent* Director = PC->FindComponentByClass<UCameraDirectorComponent>())
        {
            Director->PushZone(this);
        }
    }
}

void ACameraZoneActor::OnTriggerEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    APawn* Pawn = Cast<APawn>(OtherActor);
    if (!Pawn || !Pawn->IsLocallyControlled()) return;

    if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
    {
        if (UCameraDirectorComponent* Director = PC->FindComponentByClass<UCameraDirectorComponent>())
        {
            Director->PopZone(this);
        }
    }
}