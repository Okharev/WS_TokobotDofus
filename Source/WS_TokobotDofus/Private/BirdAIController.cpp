// BirdAIController.cpp
#include "BirdAIController.h"
#include "BirdEnemyCharacter.h"
#include "HeartHealthComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Components/StateTreeAIComponent.h"
#include "GameFramework/DamageType.h"

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
		BindPawnDamageEvents(InPawn);
	}

	if (StateTreeAIComp && !StateTreeAIComp->IsRunning())
	{
		StateTreeAIComp->StartLogic();
	}
}

void ABirdAIController::OnUnPossess()
{
	if (APawn* InPawn = GetPawn())
	{
		UnbindPawnDamageEvents(InPawn);
	}

	ClearTargetActor();
	Super::OnUnPossess();
}

void ABirdAIController::BindPawnDamageEvents(APawn* InPawn)
{
	if (!InPawn)
	{
		return;
	}

	// Primary path: BOTW heart/whip/rush combat pipeline
	if (UHeartHealthComponent* HealthComp = InPawn->FindComponentByClass<UHeartHealthComponent>())
	{
		HealthComp->OnHitReceived.AddUniqueDynamic(this, &ABirdAIController::HandlePawnCombatHit);
	}

	// Fallback path: standard UE5 ApplyDamage / ApplyPointDamage (projectiles, traps, etc.)
	InPawn->OnTakeAnyDamage.AddUniqueDynamic(this, &ABirdAIController::HandlePawnTakeAnyDamage);
}

void ABirdAIController::UnbindPawnDamageEvents(APawn* InPawn)
{
	if (!InPawn)
	{
		return;
	}

	if (UHeartHealthComponent* HealthComp = InPawn->FindComponentByClass<UHeartHealthComponent>())
	{
		HealthComp->OnHitReceived.RemoveDynamic(this, &ABirdAIController::HandlePawnCombatHit);
	}

	InPawn->OnTakeAnyDamage.RemoveDynamic(this, &ABirdAIController::HandlePawnTakeAnyDamage);
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
	// Acquire via sight only; aggro drop is enforced by the 11m distance check in FBirdStatusEvaluator
	if (!Actor || TargetActor.IsValid() || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	TryAggroFromAttacker(Actor);
}

void ABirdAIController::HandlePawnCombatHit(const FCombatHitPayload& HitPayload)
{
	if (HitPayload.bIsKillingBlow)
	{
		return;
	}

	// InstigatorActor is the Player (even when DamageCauser is an individual whip Minion)
	AActor* Attacker = HitPayload.InstigatorActor ? HitPayload.InstigatorActor.Get() : HitPayload.DamageCauser.Get();
	TryAggroFromAttacker(Attacker);
}

void ABirdAIController::HandlePawnTakeAnyDamage(
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

	// Prefer the instigating controller's Pawn (Player) over DamageCauser (e.g., Minion or Projectile)
	AActor* Attacker = (InstigatedBy && InstigatedBy->GetPawn())
		                   ? Cast<AActor>(InstigatedBy->GetPawn())
		                   : DamageCauser;

	TryAggroFromAttacker(Attacker);
}

void ABirdAIController::TryAggroFromAttacker(AActor* AttackerActor)
{
	if (!IsValid(AttackerActor) || AttackerActor == GetPawn())
	{
		return;
	}

	// Ignore hits if the bird is already dead
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

	const ABirdEnemyCharacter* Bird = Cast<ABirdEnemyCharacter>(GetPawn());
	const FName RequiredTag = Bird ? Bird->GetTuning().TargetTag : FName("Player");

	if (!RequiredTag.IsNone() && !AttackerActor->ActorHasTag(RequiredTag))
	{
		return;
	}

	SetTargetActor(AttackerActor);
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
