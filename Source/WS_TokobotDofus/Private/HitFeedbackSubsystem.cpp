// HitFeedbackSubsystem.cpp
#include "HitFeedbackSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Engine/World.h"

void UHitFeedbackSubsystem::TriggerCombatFeedback(const FCombatHitPayload& Payload, const FHitFeedbackConfig& FeedbackConfig)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float DesiredHitStop = Payload.bIsKillingBlow
		? FeedbackConfig.KillHitStopDuration
		: FeedbackConfig.HitStopDuration;

	if (DesiredHitStop > KINDA_SMALL_NUMBER)
	{
		TriggerHitStop(DesiredHitStop, FeedbackConfig.HitStopTimeDilation);
	}

	// Play Camera Shake on the local player controller
	TSubclassOf<UCameraShakeBase> ShakeClass = (Payload.bIsKillingBlow && FeedbackConfig.KillCameraShake)
		? FeedbackConfig.KillCameraShake
		: FeedbackConfig.HitCameraShake;

	if (ShakeClass && FeedbackConfig.CameraShakeScale > KINDA_SMALL_NUMBER)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(ShakeClass, FeedbackConfig.CameraShakeScale);
			}
		}
	}

	OnGlobalCombatHit.Broadcast(Payload);
}

void UHitFeedbackSubsystem::TriggerHitStop(float RealDurationSeconds, float TimeDilation)
{
	UWorld* World = GetWorld();
	if (!World || RealDurationSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float ClampedDilation = FMath::Clamp(TimeDilation, 0.005f, 0.99f);
	const double NowReal = World->GetRealTimeSeconds();
	const double CandidateEndTime = NowReal + static_cast<double>(RealDurationSeconds);

	if (!bIsHitStopActive)
	{
		PreHitStopTimeDilation = UGameplayStatics::GetGlobalTimeDilation(World);
		bIsHitStopActive = true;
		HitStopEndRealTime = CandidateEndTime;
		UGameplayStatics::SetGlobalTimeDilation(World, ClampedDilation);
		OnHitStopStarted.Broadcast(RealDurationSeconds, ClampedDilation);
	}
	else
	{
		// Multiple whip links hitting simultaneously: extend freeze cleanly without overwriting PreHitStopTimeDilation
		HitStopEndRealTime = FMath::Max(HitStopEndRealTime, CandidateEndTime);
		if (ClampedDilation < UGameplayStatics::GetGlobalTimeDilation(World))
		{
			UGameplayStatics::SetGlobalTimeDilation(World, ClampedDilation);
		}
	}
}

void UHitFeedbackSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !bIsHitStopActive)
	{
		return;
	}

	if (World->GetRealTimeSeconds() >= HitStopEndRealTime)
	{
		EndHitStop();
	}
}

void UHitFeedbackSubsystem::EndHitStop()
{
	if (!bIsHitStopActive)
	{
		return;
	}

	bIsHitStopActive = false;

	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, PreHitStopTimeDilation);
	}

	OnHitStopEnded.Broadcast();
}

void UHitFeedbackSubsystem::Deinitialize()
{
	if (bIsHitStopActive)
	{
		bIsHitStopActive = false;
		if (UWorld* World = GetWorld())
		{
			UGameplayStatics::SetGlobalTimeDilation(World, PreHitStopTimeDilation);
		}
	}

	Super::Deinitialize();
}

TStatId UHitFeedbackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHitFeedbackSubsystem, STATGROUP_Tickables);
}