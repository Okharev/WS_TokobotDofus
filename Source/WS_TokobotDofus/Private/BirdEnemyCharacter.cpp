// BirdEnemyCharacter.cpp
#include "BirdEnemyCharacter.h"
#include "BirdAIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

ABirdEnemyCharacter::ABirdEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // Grounded navigation-only capsule
    UCapsuleComponent* Capsule = GetCapsuleComponent();
    Capsule->InitCapsuleSize(25.f, 40.f);
    Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    // Elevate the inherited SkeletalMesh directly above the navigation capsule
    USkeletalMeshComponent* BirdMesh = GetMesh();
    BirdMesh->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
    BirdMesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    BirdMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Elevated hurtbox moves and bobs automatically with the mesh
    HurtboxSphere = CreateDefaultSubobject<USphereComponent>(TEXT("HurtboxSphere"));
    HurtboxSphere->SetupAttachment(BirdMesh);
    HurtboxSphere->InitSphereRadius(30.f);
    HurtboxSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HurtboxSphere->SetCollisionObjectType(ECC_Pawn);
    HurtboxSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    HurtboxSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    HurtboxSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    // Beak/talon trace origin (Mesh is rotated -90 yaw, so +Y points along Actor Forward)
    AttackOrigin = CreateDefaultSubobject<USceneComponent>(TEXT("AttackOrigin"));
    AttackOrigin->SetupAttachment(BirdMesh);
    AttackOrigin->SetRelativeLocation(FVector(0.f, 45.f, 0.f));

    // Aerial-feeling ground movement defaults
    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    MoveComp->bOrientRotationToMovement = true;
    MoveComp->RotationRate = FRotator(0.f, 260.f, 0.f);
    MoveComp->MaxWalkSpeed = Tuning.PatrolSpeed;
    MoveComp->MaxAcceleration = 600.f;
    MoveComp->BrakingDecelerationWalking = 400.f;
    MoveComp->MaxStepHeight = 60.f;

    BaseEyeHeight = Tuning.HoverHeight;
    AIControllerClass = ABirdAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ABirdEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();

    BaseEyeHeight = Tuning.HoverHeight;
    GetCharacterMovement()->MaxWalkSpeed = Tuning.PatrolSpeed;

    SmoothedHoverWorldZ = GetActorLocation().Z + Tuning.HoverHeight;
    LastActorYaw = GetActorRotation().Yaw;
    LastBobOffset = 0.f;

    GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
}

void ABirdEnemyCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateHoverAndAnimTelemetry(DeltaTime);
}

void ABirdEnemyCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
    // Originates AI sight traces from the elevated bird rather than the grounded capsule
    OutLocation = GetActorLocation() + FVector(0.f, 0.f, Tuning.HoverHeight);
    OutRotation = GetActorRotation();
}

#if WITH_EDITOR
void ABirdEnemyCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Live-preview hover height adjustments in the Blueprint Viewport
    if (USkeletalMeshComponent* BirdMesh = GetMesh())
    {
        BirdMesh->SetRelativeLocation(FVector(0.f, 0.f, Tuning.HoverHeight));
    }
    BaseEyeHeight = Tuning.HoverHeight;
}
#endif

void ABirdEnemyCharacter::UpdateHoverAndAnimTelemetry(float DeltaTime)
{
    const FVector ActorLoc = GetActorLocation();
    const float TargetWorldZ = ActorLoc.Z + Tuning.HoverHeight;

    SmoothedHoverWorldZ = FMath::FInterpTo(SmoothedHoverWorldZ, TargetWorldZ, DeltaTime, Tuning.HoverHeightInterpSpeed);

    const float BobOffset = FMath::Sin(GetWorld()->GetTimeSeconds() * Tuning.BobFrequency) * Tuning.BobAmplitude;
    const float RelativeZ = (SmoothedHoverWorldZ + BobOffset) - ActorLoc.Z;
    GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, RelativeZ));

    // Animation telemetry
    const float GroundSpeed = GetVelocity().Size2D();
    const float SafeMaxSpeed = FMath::Max(Tuning.ChaseSpeed, 1.f);

    AnimState.GroundSpeed = GroundSpeed;
    AnimState.NormalizedSpeed = FMath::Clamp(GroundSpeed / SafeMaxSpeed, 0.f, 1.f);
    AnimState.BobVelocity = (DeltaTime > KINDA_SMALL_NUMBER) ? (BobOffset - LastBobOffset) / DeltaTime : 0.f;
    LastBobOffset = BobOffset;

    const float CurrentYaw = GetActorRotation().Yaw;
    const float YawDelta = FMath::FindDeltaAngleDegrees(LastActorYaw, CurrentYaw);
    const float YawRate = (DeltaTime > KINDA_SMALL_NUMBER) ? (YawDelta / DeltaTime) : 0.f;
    LastActorYaw = CurrentYaw;

    const float TargetBank = FMath::Clamp((YawRate / 260.f) * Tuning.MaxBankAngle * AnimState.NormalizedSpeed,
                                          -Tuning.MaxBankAngle, Tuning.MaxBankAngle);
    AnimState.BankAngle = FMath::FInterpTo(AnimState.BankAngle, TargetBank, DeltaTime, 8.f);
}

void ABirdEnemyCharacter::SetAggroState(bool bNewAggroed, AActor* NewTarget)
{
    if (AnimState.bIsAggroed == bNewAggroed)
    {
        return;
    }

    AnimState.bIsAggroed = bNewAggroed;
    GetCharacterMovement()->MaxWalkSpeed = bNewAggroed ? Tuning.ChaseSpeed : Tuning.PatrolSpeed;

    OnAggroChanged.Broadcast(bNewAggroed, NewTarget);
    BP_OnAggroChanged(bNewAggroed, NewTarget);
}

void ABirdEnemyCharacter::SetAIState(EBirdAIState NewState)
{
    if (AnimState.CurrentAIState == NewState)
    {
        return;
    }

    const EBirdAIState OldState = AnimState.CurrentAIState;
    AnimState.CurrentAIState = NewState;

    OnAIStateChanged.Broadcast(OldState, NewState);
    BP_OnAIStateChanged(OldState, NewState);
}

bool ABirdEnemyCharacter::StartAttack(AActor* TargetActor)
{
    UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
    if (Tuning.AttackMontage && AnimInstance)
    {
        const float Duration = AnimInstance->Montage_Play(Tuning.AttackMontage);
        if (Duration > 0.f)
        {
            AnimState.bIsAttacking = true;

            FOnMontageEnded EndDelegate;
            EndDelegate.BindUObject(this, &ABirdEnemyCharacter::HandleAttackMontageEnded);
            AnimInstance->Montage_SetEndDelegate(EndDelegate, Tuning.AttackMontage);

            OnAttackStarted.Broadcast(TargetActor);
            return true;
        }
    }

    // Prototype fallback: runs timed hit trace when no AnimBP/Montage exists yet
    AnimState.bIsAttacking = true;
    OnAttackStarted.Broadcast(TargetActor);

    const float TotalDuration = FMath::Max(Tuning.FallbackAttackDuration, 0.1f);
    GetWorldTimerManager().SetTimer(
        FallbackHitTimerHandle,
        this,
        &ABirdEnemyCharacter::HandleFallbackAttackHit,
        TotalDuration * 0.5f,
        false
    );
    GetWorldTimerManager().SetTimer(
        FallbackEndTimerHandle,
        this,
        &ABirdEnemyCharacter::HandleFallbackAttackEnded,
        TotalDuration,
        false
    );

    return true;
}

void ABirdEnemyCharacter::CancelAttack()
{
    GetWorldTimerManager().ClearTimer(FallbackHitTimerHandle);
    GetWorldTimerManager().ClearTimer(FallbackEndTimerHandle);

    EndAttackHitWindow();
    AnimState.bIsAttacking = false;

    if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
    {
        if (Tuning.AttackMontage && AnimInstance->Montage_IsPlaying(Tuning.AttackMontage))
        {
            AnimInstance->Montage_Stop(0.15f, Tuning.AttackMontage);
        }
    }
}

void ABirdEnemyCharacter::HandleFallbackAttackHit()
{
    if (!AnimState.bIsAttacking)
    {
        return;
    }

    BeginAttackHitWindow(1.f);
    PerformAttackHitTrace();
    EndAttackHitWindow();
}

void ABirdEnemyCharacter::HandleFallbackAttackEnded()
{
    AnimState.bIsAttacking = false;
}

void ABirdEnemyCharacter::HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (Montage == Tuning.AttackMontage)
    {
        EndAttackHitWindow();
        AnimState.bIsAttacking = false;
    }
}

void ABirdEnemyCharacter::BeginAttackHitWindow(float InDamageMultiplier)
{
    bHitWindowActive = true;
    ActiveDamageMultiplier = FMath::Max(InDamageMultiplier, 0.f);
    HitActorsThisSwing.Reset();
}



void ABirdEnemyCharacter::PerformAttackHitTrace()
{
    if (!bHitWindowActive || !AttackOrigin)
    {
        return;
    }

    const FVector Start = AttackOrigin->GetComponentLocation();
    const FVector End = Start + (GetActorForwardVector() * Tuning.AttackTraceDistance);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BirdAttackTrace), false, this);
    TArray<FHitResult> HitResults;

    const bool bHit = GetWorld()->SweepMultiByChannel(
        HitResults,
        Start,
        End,
        FQuat::Identity,
        ECC_Pawn,
        FCollisionShape::MakeSphere(Tuning.AttackTraceRadius),
        QueryParams
    );

    if (Tuning.bDrawDebugTraces)
    {
        DrawDebugSphere(GetWorld(), End, Tuning.AttackTraceRadius, 12, bHit ? FColor::Red : FColor::Green, false, 0.1f);
    }

    if (!bHit)
    {
        return;
    }

    for (const FHitResult& Hit : HitResults)
    {
        AActor* HitActor = Hit.GetActor();
        if (!HitActor || HitActorsThisSwing.Contains(HitActor))
        {
            continue;
        }

        if (!Tuning.TargetTag.IsNone() && !HitActor->ActorHasTag(Tuning.TargetTag))
        {
            continue;
        }

        HitActorsThisSwing.Add(HitActor);

        const float FinalDamage = Tuning.BaseDamage * ActiveDamageMultiplier;
        UGameplayStatics::ApplyDamage(HitActor, FinalDamage, GetController(), this, UDamageType::StaticClass());

        OnAttackHit.Broadcast(HitActor, FinalDamage);
        BP_OnAttackHit(HitActor, FinalDamage);
    }
}

void ABirdEnemyCharacter::EndAttackHitWindow()
{
    bHitWindowActive = false;
    HitActorsThisSwing.Reset();
}