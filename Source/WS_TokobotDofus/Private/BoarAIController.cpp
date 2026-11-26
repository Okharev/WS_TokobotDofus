// BoarAIController.cpp
#include "BoarAIController.h"
#include "BoarEnemyCharacter.h"
#include "HeartHealthComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Components/StateTreeAIComponent.h"
#include "GameFramework/DamageType.h"

ABoarAIController::ABoarAIController()
{
	BoarPerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("BoarPerceptionComp"));
	SetPerceptionComponent(*BoarPerceptionComp);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1100.f;
	SightConfig->LoseSightRadius = 1500.f;
	SightConfig->PeripheralVisionAngleDegrees = 80.f; // Half-angle = 160 deg full cone
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	BoarPerceptionComp->ConfigureSense(*SightConfig);
	BoarPerceptionComp->SetDominantSense(UAISenseConfig_Sight::StaticClass());
	BoarPerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &ABoarAIController::HandleTargetPerceptionUpdated);

	StateTreeAIComp = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAIComp"));
}

void ABoarAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (InPawn)
	{
		SpawnLocation = InPawn->GetActorLocation();
		ApplySightConfigFromPawn(InPawn);
		BindPawnDamageEvents(InPawn);
	}

	if (StateTreeAIComp && !StateTreeAIComp->IsRunning())
	{
		StateTreeAIComp->StartLogic();
	}
}

void ABoarAIController::OnUnPossess()
{
	if (APawn* InPawn = GetPawn())
	{
		UnbindPawnDamageEvents(InPawn);
	}

	ClearTargetActor();
	Super::OnUnPossess();
}

void ABoarAIController::BindPawnDamageEvents(APawn* InPawn)
{
	if (!InPawn)
	{
		return;
	}

	if (UHeartHealthComponent* HealthComp = InPawn->FindComponentByClass<UHeartHealthComponent>())
	{
		HealthComp->OnHitReceived.AddUniqueDynamic(this, &ABoarAIController::HandlePawnCombatHit);
	}

	InPawn->OnTakeAnyDamage.AddUniqueDynamic(this, &ABoarAIController::HandlePawnTakeAnyDamage);
}

void ABoarAIController::UnbindPawnDamageEvents(APawn* InPawn)
{
	if (!InPawn)
	{
		return;
	}

	if (UHeartHealthComponent* HealthComp = InPawn->FindComponentByClass<UHeartHealthComponent>())
	{
		HealthComp->OnHitReceived.RemoveDynamic(this, &ABoarAIController::HandlePawnCombatHit);
	}

	InPawn->OnTakeAnyDamage.RemoveDynamic(this, &ABoarAIController::HandlePawnTakeAnyDamage);
}

void ABoarAIController::ApplySightConfigFromPawn(APawn* InPawn)
{
	const ABoarEnemyCharacter* Boar = Cast<ABoarEnemyCharacter>(InPawn);
	if (!Boar || !SightConfig || !BoarPerceptionComp)
	{
		return;
	}

	const FBoarEnemyTuning& Tuning = Boar->GetTuning();
	SightConfig->SightRadius = Tuning.DetectionRange;
	SightConfig->LoseSightRadius = Tuning.AggroDropRange;
	SightConfig->PeripheralVisionAngleDegrees = FMath::Clamp(Tuning.TotalVisionConeDegrees * 0.5f, 5.f, 180.f);

	BoarPerceptionComp->ConfigureSense(*SightConfig);
}

void ABoarAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// Acquire via sight; aggro drop is enforced by distance in FBoarStatusEvaluator
	if (!Actor || TargetActor.IsValid() || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	TryAggroFromAttacker(Actor);
}

void ABoarAIController::HandlePawnCombatHit(const FCombatHitPayload& HitPayload)
{
	if (HitPayload.bIsKillingBlow)
	{
		return;
	}

	AActor* Attacker = HitPayload.InstigatorActor ? HitPayload.InstigatorActor.Get() : HitPayload.DamageCauser.Get();
	TryAggroFromAttacker(Attacker);
}

void ABoarAIController::HandlePawnTakeAnyDamage(
	AActor* DamagedActor,
	float Damage,
	const UDamageType* DamageType,
	AController* InstigatedBy,
	AActor* DamageCauser)
{
	if (Damage <= 0.f)
	{
		return;
	}

	AActor* Attacker = (InstigatedBy && InstigatedBy->GetPawn())
		                   ? Cast<AActor>(InstigatedBy->GetPawn())
		                   : DamageCauser;

	TryAggroFromAttacker(Attacker);
}

void ABoarAIController::NotifyDamagedOrProvokedBy(AActor* AttackerActor)
{
	TryAggroFromAttacker(AttackerActor);
}

void ABoarAIController::TryAggroFromAttacker(AActor* AttackerActor)
{
	if (!IsValid(AttackerActor) || AttackerActor == GetPawn())
	{
		return;
	}

	if (const APawn* MyPawn = GetPawn())
	{
		if (const UHeartHealthComponent* HealthComp = MyPawn->FindComponentByClass<UHeartHealthComponent>())
		{
			if (!HealthComp->IsAlive())
			{
				return;
			}
		}
	}

	const ABoarEnemyCharacter* Boar = Cast<ABoarEnemyCharacter>(GetPawn());
	const FName RequiredTag = Boar ? Boar->GetTuning().TargetTag : FName("Player");

	if (!RequiredTag.IsNone() && !AttackerActor->ActorHasTag(RequiredTag))
	{
		return;
	}

	SetTargetActor(AttackerActor);
}

void ABoarAIController::SetTargetActor(AActor* NewTarget)
{
	if (TargetActor.Get() == NewTarget)
	{
		return;
	}

	TargetActor = NewTarget;

	if (ABoarEnemyCharacter* Boar = Cast<ABoarEnemyCharacter>(GetPawn()))
	{
		Boar->SetAggroState(TargetActor.IsValid(), NewTarget);
	}
}

void ABoarAIController::ClearTargetActor()
{
	SetTargetActor(nullptr);
}
