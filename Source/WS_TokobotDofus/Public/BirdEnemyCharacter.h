#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BirdEnemyTypes.h"
#include "HitReactableInterface.h"
#include "HeartHealthComponent.h"
#include "BirdEnemyCharacter.generated.h"

class USphereComponent;
class USceneComponent;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBirdAggroChangedSignature, bool, bIsAggroed, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBirdAIStateChangedSignature, EBirdAIState, OldState, EBirdAIState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBirdAttackStartedSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBirdAttackHitSignature, AActor*, HitActor, float, DamageDealt);

UCLASS(Blueprintable)
class WS_TOKOBOTDOFUS_API ABirdEnemyCharacter : public ACharacter, public IHitReactableInterface
{
    GENERATED_BODY()

public:
    ABirdEnemyCharacter();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    
    virtual FCombatHitResponse ReceiveCombatHit(const FCombatHitPayload& HitPayload) override;
    
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION(BlueprintPure, Category = "Bird|Combat")
    UHeartHealthComponent* GetHealthComponent() const { return HealthComp; }

    // --- Designer Events (Bindable in Blueprints, VFX, Audio, UI) ---
    UPROPERTY(BlueprintAssignable, Category = "Bird|Events")
    FBirdAggroChangedSignature OnAggroChanged;

    UPROPERTY(BlueprintAssignable, Category = "Bird|Events")
    FBirdAIStateChangedSignature OnAIStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "Bird|Events")
    FBirdAttackStartedSignature OnAttackStarted;

    UPROPERTY(BlueprintAssignable, Category = "Bird|Events")
    FBirdAttackHitSignature OnAttackHit;

    // --- Accessors for AI, AnimBP & Designers ---
    UFUNCTION(BlueprintPure, Category = "Bird|Config")
    const FBirdEnemyTuning& GetTuning() const { return Tuning; }

    UFUNCTION(BlueprintPure, Category = "Bird|Animation")
    const FBirdAnimState& GetAnimState() const { return AnimState; }

    UFUNCTION(BlueprintPure, Category = "Bird|Combat")
    bool IsAttacking() const { return AnimState.bIsAttacking; }

    // --- State & Combat API (Driven by AIController / StateTree / AnimNotifyState) ---
    void SetAggroState(bool bNewAggroed, AActor* NewTarget);
    void SetAIState(EBirdAIState NewState);

    bool StartAttack(AActor* TargetActor);
    void CancelAttack();

    void BeginAttackHitWindow(float InDamageMultiplier = 1.f);
    void PerformAttackHitTrace();
    void EndAttackHitWindow();

protected:
    // --- Blueprint Hooks for Designers (Optional overrides in BP_BirdEnemy) ---
    UFUNCTION(BlueprintImplementableEvent, Category = "Bird|Events", meta = (DisplayName = "On Aggro Changed"))
    void BP_OnAggroChanged(bool bIsAggroed, AActor* TargetActor);

    UFUNCTION(BlueprintImplementableEvent, Category = "Bird|Events", meta = (DisplayName = "On AI State Changed"))
    void BP_OnAIStateChanged(EBirdAIState OldState, EBirdAIState NewState);

    UFUNCTION(BlueprintImplementableEvent, Category = "Bird|Events", meta = (DisplayName = "On Attack Hit Target"))
    void BP_OnAttackHit(AActor* HitActor, float DamageDealt);
    
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|Components")
    TObjectPtr<UHeartHealthComponent> HealthComp;

    UFUNCTION()
    void HandleStaggerChanged(bool bIsStaggered);

    UFUNCTION()
    void HandleDeath(const FCombatHitPayload& KillingHit);

    // --- Components ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|Components")
    TObjectPtr<USphereComponent> HurtboxSphere;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bird|Components")
    TObjectPtr<USceneComponent> AttackOrigin;

    // --- Tuning & Runtime State ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bird|Config")
    FBirdEnemyTuning Tuning;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bird|Animation")
    FBirdAnimState AnimState;

private:
    void UpdateHoverAndAnimTelemetry(float DeltaTime);
    void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);
    

    void StartFallbackAttack();
    void HandleFallbackStrikeBegin();
    void HandleFallbackStrikeTick();
    void HandleFallbackAttackEnd();
    void ClearFallbackAttackTimers();

    FTimerHandle FallbackWindupTimerHandle;
    FTimerHandle FallbackStrikeTickTimerHandle;
    FTimerHandle FallbackEndTimerHandle;

    float SmoothedHoverWorldZ = 0.f;
    float LastBobOffset = 0.f;
    float LastActorYaw = 0.f;

    bool bHitWindowActive = false;
    float ActiveDamageMultiplier = 1.f;
    TSet<TWeakObjectPtr<AActor>> HitActorsThisSwing;
};