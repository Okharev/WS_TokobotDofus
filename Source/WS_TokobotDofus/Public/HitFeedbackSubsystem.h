// HitFeedbackSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CombatFeedbackTypes.h"
#include "HitFeedbackSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGlobalCombatHitSignature, const FCombatHitPayload&, HitPayload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHitStopStateSignature, float, Duration, float, TimeDilation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHitStopEndedSignature);

/**
 * World-level coordinator for BOTW HitStop (unscaled real-time freeze), Camera Shake, and global VFX/SFX dispatch.
 * Zero tick overhead when no HitStop is active.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API UHitFeedbackSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Global Designer Events (Bind in Audio Manager, Post-Process Controller, HUD, etc.) ---
	UPROPERTY(BlueprintAssignable, Category = "CombatFeedback|Events")
	FOnGlobalCombatHitSignature OnGlobalCombatHit;

	UPROPERTY(BlueprintAssignable, Category = "CombatFeedback|Events")
	FOnHitStopStateSignature OnHitStopStarted;

	UPROPERTY(BlueprintAssignable, Category = "CombatFeedback|Events")
	FOnHitStopEndedSignature OnHitStopEnded;

	/** Triggers HitStop, Camera Shake, and broadcasts OnGlobalCombatHit using real-world unscaled timing. */
	UFUNCTION(BlueprintCallable, Category = "CombatFeedback")
	void TriggerCombatFeedback(const FCombatHitPayload& Payload, const FHitFeedbackConfig& FeedbackConfig);

	/** Standalone HitStop trigger usable from Blueprints or other abilities. */
	UFUNCTION(BlueprintCallable, Category = "CombatFeedback")
	void TriggerHitStop(float RealDurationSeconds, float TimeDilation = 0.02f);

	UFUNCTION(BlueprintPure, Category = "CombatFeedback")
	bool IsHitStopActive() const { return bIsHitStopActive; }

	// ~Begin UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bIsHitStopActive; }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual void Deinitialize() override;
	// ~End UTickableWorldSubsystem

private:
	void EndHitStop();

	bool bIsHitStopActive = false;
	double HitStopEndRealTime = 0.0;
	float PreHitStopTimeDilation = 1.0f;
};