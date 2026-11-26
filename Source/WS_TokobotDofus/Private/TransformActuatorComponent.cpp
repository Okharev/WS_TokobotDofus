// TransformActuatorComponent.cpp
#include "TransformActuatorComponent.h"
#include "ActivationAggregatorComponent.h"
#include "Curves/CurveFloat.h"

UTransformActuatorComponent::UTransformActuatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UTransformActuatorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		TargetSceneComp = Cast<USceneComponent>(TargetComponentRef.GetComponent(Owner));
		if (!TargetSceneComp)
		{
			TargetSceneComp = Owner->GetRootComponent();
		}

		if (TargetSceneComp)
		{
			InitialRelativeTransform = TargetSceneComp->GetRelativeTransform();
		}

		if (bAutoBindToOwnerAggregator)
		{
			if (UActivationAggregatorComponent* Aggregator = Owner->FindComponentByClass<UActivationAggregatorComponent>())
			{
				if (bDriveFromProportionalProgress)
				{
					Aggregator->OnAggregateProgressChanged.AddUniqueDynamic(this, &UTransformActuatorComponent::HandleAggregateProgressChanged);
				}
				else
				{
					Aggregator->OnAggregateStateChanged.AddUniqueDynamic(this, &UTransformActuatorComponent::HandleAggregateStateChanged);
				}
			}
		}
	}
}

void UTransformActuatorComponent::HandleAggregateStateChanged(bool bIsActivated, bool bInstant)
{
	SetTargetAlpha(bIsActivated ? 1.0f : 0.0f, bInstant);
}

void UTransformActuatorComponent::HandleAggregateProgressChanged(float NormalizedAlpha, bool bInstant)
{
	SetTargetAlpha(NormalizedAlpha, bInstant);
}

void UTransformActuatorComponent::SetTargetAlpha(float NewTargetAlpha, bool bInstant)
{
	TargetAlpha = FMath::Clamp(NewTargetAlpha, 0.0f, 1.0f);

	if (bInstant || FMath::IsNearlyEqual(CurrentAlpha, TargetAlpha))
	{
		CurrentAlpha = TargetAlpha;
		ApplyTransformForAlpha(CurrentAlpha);
		SetComponentTickEnabled(false);
		return;
	}

	const bool bMovingForward = TargetAlpha > CurrentAlpha;
	if (!IsComponentTickEnabled())
	{
		OnMotionStarted.Broadcast(bMovingForward);
		SetComponentTickEnabled(true);
	}
}

void UTransformActuatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const bool bOpening = TargetAlpha > CurrentAlpha;
	const float Duration = FMath::Max(bOpening ? OpenDuration : CloseDuration, KINDA_SMALL_NUMBER);
	const float Step = DeltaTime / Duration;

	CurrentAlpha = FMath::FInterpConstantTo(CurrentAlpha, TargetAlpha, 1.0f, Step);
	ApplyTransformForAlpha(CurrentAlpha);

	if (FMath::IsNearlyEqual(CurrentAlpha, TargetAlpha))
	{
		CurrentAlpha = TargetAlpha;
		ApplyTransformForAlpha(CurrentAlpha);
		SetComponentTickEnabled(false);
		OnMotionFinished.Broadcast(bOpening);
	}
}

void UTransformActuatorComponent::ApplyTransformForAlpha(float LinearAlpha)
{
	if (!TargetSceneComp)
	{
		return;
	}

	const float CurvedAlpha = MotionCurve ? MotionCurve->GetFloatValue(LinearAlpha) : LinearAlpha;

	const FVector NewLoc = InitialRelativeTransform.GetLocation() + (RelativeLocationOffset * CurvedAlpha);
	const FQuat DeltaRot = (RelativeRotationOffset * CurvedAlpha).Quaternion();
	const FQuat NewRot = InitialRelativeTransform.GetRotation() * DeltaRot;
	const FVector NewScale = InitialRelativeTransform.GetScale3D() + (RelativeScaleOffset * CurvedAlpha);

	TargetSceneComp->SetRelativeLocationAndRotation(NewLoc, NewRot);
	if (!RelativeScaleOffset.IsNearlyZero())
	{
		TargetSceneComp->SetRelativeScale3D(NewScale);
	}
}