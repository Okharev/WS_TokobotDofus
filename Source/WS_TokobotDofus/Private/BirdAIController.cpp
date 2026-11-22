// BirdAIController.cpp
#include "BirdAIController.h"
#include "BirdEnemyCharacter.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Components/StateTreeAIComponent.h"

ABirdAIController::ABirdAIController()
{
    BirdPerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("BirdPerceptionComp"));
    SetPerceptionComponent(*BirdPerceptionComp);

    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = 1000.f;
    SightConfig->LoseSightRadius = 1100.f;
    SightConfig->PeripheralVisionAngleDegrees = 90.f; // Half-angle = 180 deg full cone
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

    BirdPerceptionComp->ConfigureSense(*SightConfig);
    BirdPerceptionComp->SetDominantSense(UAISenseConfig_Sight::StaticClass());
    BirdPerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ABirdAIController::HandleTargetPerceptionUpdated);

    StateTreeAIComp = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAIComp"));
}

void ABirdAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (InPawn)
    {
        SpawnLocation = InPawn->GetActorLocation();
        ApplySightConfigFromPawn(InPawn);
    }

    if (StateTreeAIComp && !StateTreeAIComp->IsRunning())
    {
        StateTreeAIComp->StartLogic();
    }
}

void ABirdAIController::ApplySightConfigFromPawn(APawn* InPawn)
{
    const ABirdEnemyCharacter* Bird = Cast<ABirdEnemyCharacter>(InPawn);
    if (!Bird || !SightConfig || !BirdPerceptionComp)
    {
        return;
    }

    const FBirdEnemyTuning& Tuning = Bird->GetTuning();
    SightConfig->SightRadius = Tuning.DetectionRange;
    SightConfig->LoseSightRadius = Tuning.AggroDropRange;
    SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(Tuning.TotalVisionConeDegrees * 0.5f, 5.f, 180.f);

    BirdPerceptionComp->ConfigureSense(*SightConfig);
}

void ABirdAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    // Acquire via sight only; aggro drop is strictly enforced by the 11m distance check in FBirdStatusEvaluator
    if (!Actor || TargetActor.IsValid() || !Stimulus.WasSuccessfullySensed())
    {
        return;
    }

    const ABirdEnemyCharacter* Bird = Cast<ABirdEnemyCharacter>(GetPawn());
    const FName RequiredTag = Bird ? Bird->GetTuning().TargetTag : FName("Player");

    if (!RequiredTag.IsNone() && !Actor->ActorHasTag(RequiredTag))
    {
        return;
    }

    SetTargetActor(Actor);
}

void ABirdAIController::SetTargetActor(AActor* NewTarget)
{
    if (TargetActor.Get() == NewTarget)
    {
        return;
    }

    TargetActor = NewTarget;

    if (ABirdEnemyCharacter* Bird = Cast<ABirdEnemyCharacter>(GetPawn()))
    {
        Bird->SetAggroState(TargetActor.IsValid(), NewTarget);
    }
}

void ABirdAIController::ClearTargetActor()
{
    SetTargetActor(nullptr);
}