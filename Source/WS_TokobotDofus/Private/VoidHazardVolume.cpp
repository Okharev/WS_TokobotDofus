// VoidHazardVolume.cpp
#include "VoidHazardVolume.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"

AVoidHazardVolume::AVoidHazardVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	HazardBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HazardBox"));
	SetRootComponent(HazardBox);
	HazardBox->SetBoxExtent(FVector(500.0f, 500.0f, 100.0f));
	HazardBox->SetCollisionProfileName(TEXT("Trigger"));
	HazardBox->SetGenerateOverlapEvents(true);

	OverrideRespawnMarker = CreateDefaultSubobject<UArrowComponent>(TEXT("OverrideRespawnMarker"));
	OverrideRespawnMarker->SetupAttachment(HazardBox);
	OverrideRespawnMarker->SetRelativeLocation(FVector(0.0f, 0.0f, 300.0f));
	OverrideRespawnMarker->ArrowColor = FColor::Cyan;
}

void AVoidHazardVolume::BeginPlay()
{
	Super::BeginPlay();
	HazardBox->OnComponentBeginOverlap.AddDynamic(this, &AVoidHazardVolume::OnHazardBeginOverlap);
}

void AVoidHazardVolume::OnHazardBeginOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!IsValid(OtherActor))
	{
		return;
	}

	if (UVoidRecoveryComponent* VoidComp = OtherActor->FindComponentByClass<UVoidRecoveryComponent>())
	{
		FVoidFallRequest Request = FallSettings;
		if (Request.bUseOverrideRespawn && OverrideRespawnMarker)
		{
			Request.OverrideRespawnTransform = OverrideRespawnMarker->GetComponentTransform();
		}

		VoidComp->TriggerVoidFall(Request);
	}
}