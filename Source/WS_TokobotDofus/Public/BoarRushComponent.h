// BoarRushComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BoarEnemyTypes.h"
#include "BoarRushComponent.generated.h"

class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBoarPhaseFinishedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoarRushClashedSignature, AActor*, ClashedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBoarStunVulnerabilityChangedSignature, bool, bIsStunnedVulnerable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBoarRushHitSignature, AActor*, HitActor, float, DamageDealt);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class WS_TOKOBOTDOFUS_API UBoarRushComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBoarRushComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Called by ABoarEnemyCharacter::BeginPlay to bind tuning and the tusk/snout sweep origin. */
	void Initialize(const FBoarEnemyTuning* InTuning, USceneComponent* InRushOrigin);

	// --- StateTree & Character Commands ---
	bool StartChargeUp(AActor* Target);
	bool ExecuteRush();
	void CancelActiveAction();
	void EnterStunnedVulnerable(float DurationOverride = -1.f);
	void EndStunnedVulnerable();

	// --- Clash Arbitration (Idempotent: safe whether Boar sweep or Player ReceiveCombatHit runs first) ---
	bool CanClashWithActor(AActor* Candidate) const;
	bool TryResolveRushClash(AActor* Candidate);

	// --- Queries ---
	UFUNCTION(BlueprintPure, Category = "Boar|Rush")
	bool IsChargingUp() const { return Phase == ERushPhase::ChargingUp; }

	UFUNCTION(BlueprintPure, Category = "Boar|Rush")
	bool IsRushing() const { return Phase == ERushPhase::Rushing; }

	UFUNCTION(BlueprintPure, Category = "Boar|Rush")
	bool IsStunnedVulnerable() const { return bIsStunnedVulnerable; }

	UFUNCTION(BlueprintPure, Category = "Boar|Rush")
	float GetChargeUpAlpha() const { return ChargeUpAlpha; }

	UFUNCTION(BlueprintPure, Category = "Boar|Rush")
	float GetRushProgressAlpha() const;

	// --- Designer & Character Events ---
	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FOnBoarPhaseFinishedSignature OnChargeUpCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FOnBoarPhaseFinishedSignature OnRushFinished;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FOnBoarRushClashedSignature OnRushClashed;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FOnBoarStunVulnerabilityChangedSignature OnStunVulnerabilityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FOnBoarRushHitSignature OnRushHitPlayer;

private:
	enum class ERushPhase : uint8
	{
		None,
		ChargingUp,
		Rushing
	};

	void TickChargeUp(float DeltaTime);
	void TickRush(float DeltaTime);
	void SweepRushHitbox();
	void ApplyRushHitToTarget(AActor* TargetActor, const FHitResult& Hit);
	void StopRushInternal(bool bBroadcastFinished);
	UObject* ResolveRushClashObject(AActor* Candidate) const;

	UFUNCTION()
	void HandleCapsuleHit(
		UPrimitiveComponent* HitComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	const FBoarEnemyTuning* Tuning = nullptr;
	TWeakObjectPtr<ACharacter> OwnerChar;
	TWeakObjectPtr<USceneComponent> RushOrigin;
	TWeakObjectPtr<AActor> ChargeTarget;

	ERushPhase Phase = ERushPhase::None;
	bool bIsStunnedVulnerable = false;

	float ChargeUpElapsed = 0.f;
	float ChargeUpAlpha = 0.f;

	FVector LockedRushDir = FVector::ForwardVector;
	FVector PrevRushLocation = FVector::ZeroVector;
	float DistanceTraveled = 0.f;

	float SavedMaxWalkSpeed = 240.f;
	float SavedMaxAcceleration = 1500.f;
	bool bSavedOrientToMovement = true;

	TSet<TWeakObjectPtr<AActor>> HitActorsThisRush;
	FTimerHandle StunTimerHandle;
};