// BoarEnemyCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BoarEnemyTypes.h"
#include "HitReactableInterface.h"
#include "HeartHealthComponent.h"
#include "BoarEnemyCharacter.generated.h"

class UBoarRushComponent;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBoarAggroChangedSignature, bool, bIsAggroed, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBoarAIStateChangedSignature, EBoarAIState, OldState, EBoarAIState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBoarClashedSignature, AActor*, ClashedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBoarStunChangedSignature, bool, bIsStunnedVulnerable);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBoarHitDeflectedSignature, const FCombatHitPayload&, DeflectedPayload);

UCLASS(Blueprintable)
class WS_TOKOBOTDOFUS_API ABoarEnemyCharacter : public ACharacter, public IHitReactableInterface
{
	GENERATED_BODY()

public:
	ABoarEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// --- IHitReactableInterface ---
	virtual FCombatHitResponse ReceiveCombatHit(const FCombatHitPayload& HitPayload) override;

	// --- Component Accessors ---
	UFUNCTION(BlueprintPure, Category = "Boar|Combat")
	UHeartHealthComponent* GetHealthComponent() const { return HealthComp; }

	UFUNCTION(BlueprintPure, Category = "Boar|Combat")
	UBoarRushComponent* GetRushComponent() const { return RushComp; }

	// --- Config & Animation Accessors ---
	UFUNCTION(BlueprintPure, Category = "Boar|Config")
	const FBoarEnemyTuning& GetTuning() const { return Tuning; }

	UFUNCTION(BlueprintPure, Category = "Boar|Animation")
	const FBoarAnimState& GetAnimState() const { return AnimState; }

	// --- State API (Driven by ABoarAIController & StateTree) ---
	void SetAggroState(bool bNewAggroed, AActor* NewTarget);
	void SetAIState(EBoarAIState NewState);

	// --- Designer Events (Bindable in Blueprints, VFX, Audio, UI) ---
	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FBoarAggroChangedSignature OnAggroChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FBoarAIStateChangedSignature OnAIStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FBoarClashedSignature OnRushClashed;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FBoarStunChangedSignature OnStunVulnerabilityChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boar|Events")
	FBoarHitDeflectedSignature OnHitDeflected;

protected:
	// --- Optional Blueprint Hooks in BP_BoarEnemy ---
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On Aggro Changed"))
	void BP_OnAggroChanged(bool bIsAggroed, AActor* TargetActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On AI State Changed"))
	void BP_OnAIStateChanged(EBoarAIState OldState, EBoarAIState NewState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On Rush Clashed"))
	void BP_OnRushClashed(AActor* ClashedActor);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On Stun Vulnerability Changed"))
	void BP_OnStunVulnerabilityChanged(bool bIsStunnedVulnerable);

	/** Fires when an attack bounces off the Boar's armored hide (play metallic/thick hide clang VFX & SFX). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On Hit Deflected"))
	void BP_OnHitDeflected(const FCombatHitPayload& DeflectedPayload);

	UFUNCTION(BlueprintImplementableEvent, Category = "Boar|Events", meta = (DisplayName = "On Rush Hit Player"))
	void BP_OnRushHitPlayer(AActor* HitActor, float DamageDealt);

	// --- Components ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components")
	TObjectPtr<UHeartHealthComponent> HealthComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components")
	TObjectPtr<UBoarRushComponent> RushComp;

	/** Snout/tusk sweep origin attached to the capsule (+X = Actor Forward). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boar|Components")
	TObjectPtr<USceneComponent> RushHitOrigin;

	// --- Tuning & Runtime Telemetry ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boar|Config")
	FBoarEnemyTuning Tuning;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Boar|Animation")
	FBoarAnimState AnimState;

private:
	UFUNCTION()
	void HandleStaggerChanged(bool bIsStaggered);

	UFUNCTION()
	void HandleDeath(const FCombatHitPayload& KillingHit);

	UFUNCTION()
	void HandleRushClashed(AActor* ClashedActor);

	UFUNCTION()
	void HandleStunVulnerabilityChanged(bool bIsStunned);

	UFUNCTION()
	void HandleRushHitPlayer(AActor* HitActor, float DamageDealt);

	void UpdateAnimTelemetry();
};