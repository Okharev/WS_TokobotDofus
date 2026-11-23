// ZeldaPawn.cpp
#include "ZeldaPawn.h"
#include "ActionMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

AZeldaPawn::AZeldaPawn()
{
	// Shell never ticks; kinematics run inside UActionMovementComponent
	PrimaryActorTick.bCanEverTick = false;

	CapsuleComp = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComp"));
	CapsuleComp->InitCapsuleSize(34.f, 88.f);
	CapsuleComp->SetCollisionProfileName(TEXT("Pawn"));
	// Ignore camera channel so player capsule never trips camera collision probes
	CapsuleComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// Generate overlap events so ACameraZoneActor triggers and AvoidanceSphere work seamlessly
	CapsuleComp->SetGenerateOverlapEvents(true);
	SetRootComponent(CapsuleComp);

	MeshComp = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(CapsuleComp);
	MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	MeshComp->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MovementComp = CreateDefaultSubobject<UActionMovementComponent>(TEXT("ActionMovementComp"));
	MovementComp->SetUpdatedComponent(CapsuleComp);
}

UPawnMovementComponent* AZeldaPawn::GetMovementComponent() const
{
	return MovementComp;
}

bool AZeldaPawn::IsCarrierGrounded() const
{
	return MovementComp ? MovementComp->IsGrounded() : false;
}

void AZeldaPawn::SetCarrierVelocityOverride(const FVector& PlanarVelocity, bool bEnableOverride)
{
	if (MovementComp)
	{
		MovementComp->SetVelocityOverride(PlanarVelocity, bEnableOverride);
	}
}

void AZeldaPawn::LaunchCarrier(const FVector& LaunchVelocity, bool bPreserveAirMomentum)
{
	if (MovementComp)
	{
		MovementComp->LaunchPawn(LaunchVelocity, bPreserveAirMomentum);
	}
}

FOnCarrierGroundedChanged& AZeldaPawn::GetOnCarrierGroundedChanged()
{
	check(MovementComp);
	return MovementComp->OnGroundedStateChanged;
}

FOnCarrierJumpIntercept& AZeldaPawn::GetOnCarrierJumpIntercept()
{
	check(MovementComp);
	return MovementComp->OnJumpIntercept;
}

FOnCarrierWallHit& AZeldaPawn::GetOnCarrierWallHit()
{
	check(MovementComp);
	return MovementComp->OnBlockingWallHit;
}