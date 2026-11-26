#include "SquashStretchComponent.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"

USquashStretchComponent::USquashStretchComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void USquashStretchComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveTargetComponent();
}

USceneComponent* USquashStretchComponent::ResolveTargetComponent()
{
	if (IsValid(CachedTargetComponent))
	{
		return CachedTargetComponent;
	}

	if (const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		CachedTargetComponent = OwnerChar->GetMesh();
	}
	else if (AActor* OwnerActor = GetOwner())
	{
		CachedTargetComponent = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
		if (!CachedTargetComponent)
		{
			CachedTargetComponent = OwnerActor->GetRootComponent();
		}
	}

	if (IsValid(CachedTargetComponent) && !bBaseScaleCached)
	{
		BaseRelativeScale = CachedTargetComponent->GetRelativeScale3D();
		bBaseScaleCached = true;
	}

	return CachedTargetComponent;
}

void USquashStretchComponent::SetSpeedStretch(float CurrentSpeed, const FVector& OwnerLocalAxis)
{
	if (!OwnerLocalAxis.IsNearlyZero())
	{
		ActiveOwnerLocalAxis = OwnerLocalAxis.GetSafeNormal();
	}

	const float Alpha = FMath::GetMappedRangeValueClamped(
		FVector2D(StretchMinSpeed, StretchMaxSpeed),
		FVector2D(0.0f, 1.0f),
		CurrentSpeed);

	TargetDeformation = Alpha * MaxSpeedStretchAmount;
	bContinuousMode = true;

	if (!IsComponentTickEnabled())
	{
		SetComponentTickEnabled(true);
	}
}

void USquashStretchComponent::ClearContinuousStretch()
{
	bContinuousMode = false;
	TargetDeformation = 0.0f;
}

void USquashStretchComponent::TriggerImpactAftershock(
	float ImpulseVelocity,
	const FVector& OwnerLocalAxis,
	float ImmediateSquashOffset)
{
	if (!OwnerLocalAxis.IsNearlyZero())
	{
		ActiveOwnerLocalAxis = OwnerLocalAxis.GetSafeNormal();
	}

	bContinuousMode = false;
	TargetDeformation = 0.0f;
	CurrentDeformation = FMath::Clamp(ImmediateSquashOffset, MinAxisScale - 1.0f, MaxAxisScale - 1.0f);
	SpringVelocity = ImpulseVelocity;

	if (USceneComponent* TargetComp = ResolveTargetComponent())
	{
		ApplyVolumePreservingScale(TargetComp);
	}

	SetComponentTickEnabled(true);
}

void USquashStretchComponent::ResetToRest()
{
	bContinuousMode = false;
	CurrentDeformation = 0.0f;
	TargetDeformation = 0.0f;
	SpringVelocity = 0.0f;

	if (USceneComponent* TargetComp = ResolveTargetComponent())
	{
		TargetComp->SetRelativeScale3D(BaseRelativeScale);
		if (CustomPrimitiveDataStartIndex >= 0)
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(TargetComp))
			{
				Prim->SetCustomPrimitiveDataFloat(CustomPrimitiveDataStartIndex, 0.0f);
				Prim->SetCustomPrimitiveDataFloat(CustomPrimitiveDataStartIndex + 1, 0.0f);
			}
		}
	}

	SetComponentTickEnabled(false);
}

void USquashStretchComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* TargetComp = ResolveTargetComponent();
	if (!IsValid(TargetComp))
	{
		SetComponentTickEnabled(false);
		return;
	}

	// Clamp dt to keep spring numerically stable during frame hitches or HitStop transitions
	const float ClampedDt = FMath::Min(DeltaTime, 0.033f);

	// 2nd-order damped harmonic oscillator: a = -k(x - target) - c*v
	const float Displacement = CurrentDeformation - TargetDeformation;
	const float SpringAccel = (-SpringStiffness * Displacement) - (SpringDamping * SpringVelocity);
	SpringVelocity += SpringAccel * ClampedDt;
	CurrentDeformation += SpringVelocity * ClampedDt;

	ApplyVolumePreservingScale(TargetComp);

	if (!bContinuousMode &&
		FMath::Abs(Displacement) < 0.002f &&
		FMath::Abs(SpringVelocity) < 0.01f)
	{
		ResetToRest();
	}
}

void USquashStretchComponent::ApplyVolumePreservingScale(USceneComponent* TargetComp)
{
	// Volume preservation: Primary * Perp^2 == 1.0  =>  Perp = 1 / sqrt(Primary)
	const float PrimaryScale = FMath::Clamp(1.0f + CurrentDeformation, MinAxisScale, MaxAxisScale);
	const float PerpScale = 1.0f / FMath::Sqrt(PrimaryScale);

	// Account for UE ACharacter mesh relative rotation (typically -90 deg Yaw)
	const FVector MeshLocalAxis = TargetComp->GetRelativeRotation().UnrotateVector(ActiveOwnerLocalAxis).GetAbs();
	const FVector AxisWeights(
		FMath::Clamp(MeshLocalAxis.X, 0.0f, 1.0f),
		FMath::Clamp(MeshLocalAxis.Y, 0.0f, 1.0f),
		FMath::Clamp(MeshLocalAxis.Z, 0.0f, 1.0f));

	const FVector SquashMultiplier(
		FMath::Lerp(PerpScale, PrimaryScale, AxisWeights.X),
		FMath::Lerp(PerpScale, PrimaryScale, AxisWeights.Y),
		FMath::Lerp(PerpScale, PrimaryScale, AxisWeights.Z));

	TargetComp->SetRelativeScale3D(BaseRelativeScale * SquashMultiplier);

	if (CustomPrimitiveDataStartIndex >= 0)
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(TargetComp))
		{
			Prim->SetCustomPrimitiveDataFloat(CustomPrimitiveDataStartIndex, CurrentDeformation);
			Prim->SetCustomPrimitiveDataFloat(CustomPrimitiveDataStartIndex + 1, SpringVelocity);
		}
	}
}
