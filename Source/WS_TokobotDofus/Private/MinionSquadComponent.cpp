// MinionSquadComponent.cpp
#include "MinionSquadComponent.h"
#include "MinionAgentInterface.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

UMinionSquadComponent::UMinionSquadComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetRelativeLocation(FVector(-160.0f, 0.0f, 0.0f));
}

void UMinionSquadComponent::BeginPlay()
{
	Super::BeginPlay();

	SetComponentTickEnabled(false);

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (GetRelativeLocation().SizeSquared2D() < FMath::Square(PlayerEvadeRadius + 20.0f))
	{
		SetRelativeLocation(FVector(-(PlayerEvadeRadius + 65.0f), 0.0f, 0.0f));
	}

	LastAnchorLocation = GetComponentLocation();
	LastAnchorRotation = GetComponentRotation();
	bForceNextUpdate = true;

	AvoidanceSphere = NewObject<USphereComponent>(OwnerActor, TEXT("PlayerMinionAvoidanceSphere"));
	if (AvoidanceSphere)
	{
		AvoidanceSphere->SetupAttachment(OwnerActor->GetRootComponent());
		AvoidanceSphere->SetSphereRadius(PlayerEvadeRadius);
		AvoidanceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		AvoidanceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		AvoidanceSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		AvoidanceSphere->RegisterComponent();
		AvoidanceSphere->OnComponentBeginOverlap.AddDynamic(this, &UMinionSquadComponent::OnPlayerAvoidanceOverlap);
	}

	for (int32 ArmIdx = 0; ArmIdx < 2; ++ArmIdx)
	{
		const FName SphereName = (ArmIdx == 0) ? TEXT("RightHandChainAnchor") : TEXT("LeftHandChainAnchor");
		USphereComponent* AnchorSphere = NewObject<USphereComponent>(OwnerActor, SphereName);
		if (AnchorSphere)
		{
			AnchorSphere->SetupAttachment(OwnerActor->GetRootComponent());
			AnchorSphere->SetSphereRadius(10.0f);
			AnchorSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			AnchorSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
			AnchorSphere->SetSimulatePhysics(false);
			AnchorSphere->RegisterComponent();
			HandAnchorSpheres[ArmIdx] = AnchorSphere;
		}
	}

	UpdateHandAnchorTransforms(0.0f);

	TArray<TObjectPtr<AActor>> Preassigned = MoveTemp(ActiveMinions);
	ActiveMinions.Empty();
	for (AActor* Candidate : Preassigned)
	{
		RegisterMinion(Candidate);
	}

	if (bAutoRegisterMinionsOnBeginPlay && ActiveMinions.IsEmpty())
	{
		AutoRegisterMinionsInWorld();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			SquadTimerHandle,
			this,
			&UMinionSquadComponent::EvaluateSquadAndAvoidance,
			UpdateInterval,
			true);
	}
}

void UMinionSquadComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CurrentStance == EMinionSquadStance::AttackWhip)
	{
		DestroyChaosPhysicsChains();
	}
	else if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		ExitCarryRushMode();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SquadTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UMinionSquadComponent::BroadcastDebug(const FString& Message, FColor ScreenColor, float Duration) const
{
	OnSquadDebugMessage.Broadcast(Message);

	if (bPrintDebugToScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, Duration, ScreenColor, FString::Printf(TEXT("[PikminSquad] %s"), *Message));
	}
	UE_LOG(LogTemp, Log, TEXT("[PikminSquad] %s"), *Message);
}

void UMinionSquadComponent::UpdateHandAnchorTransforms(float ExtraOrbitYaw)
{
	if (HandAnchorSpheres[0])
	{
		const FVector RightPos = WhipConfig.RightArmLocalOffset.RotateAngleAxis(ExtraOrbitYaw, FVector::UpVector);
		const FRotator RightRot(0.0f, WhipConfig.RightArmRestYawOffset + ExtraOrbitYaw, 0.0f);
		HandAnchorSpheres[0]->SetRelativeLocationAndRotation(RightPos, RightRot);
	}

	if (HandAnchorSpheres[1])
	{
		const FVector BaseLeftOffset = WhipConfig.bDualArmWhip ? WhipConfig.LeftArmLocalOffset : WhipConfig.RightArmLocalOffset;
		const float BaseLeftYaw = WhipConfig.bDualArmWhip ? WhipConfig.LeftArmRestYawOffset : WhipConfig.RightArmRestYawOffset;

		const FVector LeftPos = BaseLeftOffset.RotateAngleAxis(ExtraOrbitYaw, FVector::UpVector);
		const FRotator LeftRot(0.0f, BaseLeftYaw + ExtraOrbitYaw, 0.0f);
		HandAnchorSpheres[1]->SetRelativeLocationAndRotation(LeftPos, LeftRot);
	}
}

void UMinionSquadComponent::SortMinionsForWhipArms()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || ActiveMinions.Num() <= 1)
	{
		return;
	}

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	const FVector PlayerRight = OwnerActor->GetActorRightVector();

	if (!WhipConfig.bDualArmWhip)
	{
		ActiveMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared2D(B.GetActorLocation(), PlayerLoc);
		});
		return;
	}

	TArray<TObjectPtr<AActor>> SortedBySide = ActiveMinions;
	SortedBySide.Sort([&PlayerLoc, &PlayerRight](const AActor& A, const AActor& B)
	{
		const float DotA = FVector::DotProduct(A.GetActorLocation() - PlayerLoc, PlayerRight);
		const float DotB = FVector::DotProduct(B.GetActorLocation() - PlayerLoc, PlayerRight);
		return DotA > DotB;
	});

	const int32 Total = SortedBySide.Num();
	const int32 RightCount = (Total + 1) / 2;

	TArray<TObjectPtr<AActor>, TInlineAllocator<8>> RightArmPool;
	TArray<TObjectPtr<AActor>, TInlineAllocator<8>> LeftArmPool;

	for (int32 i = 0; i < Total; ++i)
	{
		if (i < RightCount)
		{
			RightArmPool.Add(SortedBySide[i]);
		}
		else
		{
			LeftArmPool.Add(SortedBySide[i]);
		}
	}

	const FVector RightAnchor = HandAnchorSpheres[0] ? HandAnchorSpheres[0]->GetComponentLocation() : PlayerLoc;
	const FVector LeftAnchor = HandAnchorSpheres[1] ? HandAnchorSpheres[1]->GetComponentLocation() : PlayerLoc;

	RightArmPool.Sort([&RightAnchor](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), RightAnchor) < FVector::DistSquared2D(B.GetActorLocation(), RightAnchor);
	});

	LeftArmPool.Sort([&LeftAnchor](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared2D(A.GetActorLocation(), LeftAnchor) < FVector::DistSquared2D(B.GetActorLocation(), LeftAnchor);
	});

	ActiveMinions.Empty(Total);
	const int32 MaxArmLen = FMath::Max(RightArmPool.Num(), LeftArmPool.Num());
	for (int32 LinkIdx = 0; LinkIdx < MaxArmLen; ++LinkIdx)
	{
		if (LinkIdx < RightArmPool.Num())
		{
			ActiveMinions.Add(RightArmPool[LinkIdx]);
		}
		if (LinkIdx < LeftArmPool.Num())
		{
			ActiveMinions.Add(LeftArmPool[LinkIdx]);
		}
	}
}

void UMinionSquadComponent::BuildChaosPhysicsChains()
{
	DestroyChaosPhysicsChains();

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || ActiveMinions.IsEmpty())
	{
		return;
	}

	UpdateHandAnchorTransforms(0.0f);

	const int32 MinionCount = ActiveMinions.Num();
	PrevMinionLocations.SetNumUninitialized(MinionCount);
	InitialMinionHeights.SetNumUninitialized(MinionCount);

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		if (!IsValid(MinionActor))
		{
			continue;
		}

		const int32 ArmIndex = WhipConfig.bDualArmWhip ? (i % 2) : 0;
		const int32 LinkIndexInArm = WhipConfig.bDualArmWhip ? (i / 2) : i;
		USphereComponent* HandSphere = HandAnchorSpheres[ArmIndex];

		const FRotator OutwardArmRot = HandSphere ? HandSphere->GetComponentRotation() : OwnerActor->GetActorRotation();
		const FVector HandWorldLoc = HandSphere ? HandSphere->GetComponentLocation() : OwnerActor->GetActorLocation();
		const float StartZ = MinionActor->GetActorLocation().Z;

		InitialMinionHeights[i] = StartZ;

		const float ChainDist = WhipConfig.StartOffsetFromArm + (LinkIndexInArm * WhipConfig.LinkSpacing);
		const FVector IdealStartPos = HandWorldLoc + (OutwardArmRot.Vector() * ChainDist);
		MinionActor->SetActorLocationAndRotation(
			FVector(IdealStartPos.X, IdealStartPos.Y, StartZ),
			FRotator(0.0f, OutwardArmRot.Yaw, 0.0f),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);

		PrevMinionLocations[i] = MinionActor->GetActorLocation();

		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetWhipModeEnabled(true);
		}

		if (UPrimitiveComponent* MinionPrim = Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()))
		{
			MinionPrim->SetLinearDamping(WhipConfig.ChaosLinearDamping);
			MinionPrim->SetAngularDamping(8.0f);
		}
	}

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* ChildMinion = ActiveMinions[i];
		if (!IsValid(ChildMinion))
		{
			continue;
		}

		UPrimitiveComponent* ChildPrim = Cast<UPrimitiveComponent>(ChildMinion->GetRootComponent());
		if (!ChildPrim)
		{
			continue;
		}

		const int32 ArmIndex = WhipConfig.bDualArmWhip ? (i % 2) : 0;
		const int32 LinkIndexInArm = WhipConfig.bDualArmWhip ? (i / 2) : i;
		const int32 ParentMinionIdx = WhipConfig.bDualArmWhip ? (i - 2) : (i - 1);

		UPrimitiveComponent* ParentPrim = nullptr;
		float TargetLinkDistance = WhipConfig.LinkSpacing;

		if (LinkIndexInArm == 0 || ParentMinionIdx < 0)
		{
			ParentPrim = HandAnchorSpheres[ArmIndex];
			TargetLinkDistance = WhipConfig.StartOffsetFromArm;
		}
		else if (ActiveMinions.IsValidIndex(ParentMinionIdx) && IsValid(ActiveMinions[ParentMinionIdx]))
		{
			ParentPrim = Cast<UPrimitiveComponent>(ActiveMinions[ParentMinionIdx]->GetRootComponent());
			TargetLinkDistance = WhipConfig.LinkSpacing;
		}

		if (!ParentPrim)
		{
			continue;
		}

		UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(OwnerActor);
		if (!Constraint)
		{
			continue;
		}

		Constraint->AttachToComponent(ParentPrim, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Constraint->RegisterComponent();
		Constraint->SetDisableCollision(true);

		Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Limited, WhipConfig.LinearConstraintSlack);
		Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Limited, WhipConfig.LinearConstraintSlack);
		Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Limited, WhipConfig.MaxVerticalSlack);

		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Stiffness = WhipConfig.LinearSpringStiffness;
		Constraint->ConstraintInstance.ProfileInstance.LinearLimit.Damping = WhipConfig.LinearSpringDamping;

		Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, WhipConfig.MaxJointBendAngleDegrees);
		Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, 10.0f);
		Constraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);

		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.bSoftConstraint = true;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Stiffness = WhipConfig.AngularBendStiffness;
		Constraint->ConstraintInstance.ProfileInstance.ConeLimit.Damping = WhipConfig.AngularBendDamping;

		Constraint->SetConstrainedComponents(ParentPrim, NAME_None, ChildPrim, NAME_None);

		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame1, FVector(TargetLinkDistance, 0.0f, 0.0f));
		Constraint->SetConstraintReferencePosition(EConstraintFrame::Frame2, FVector::ZeroVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame1, FVector::ForwardVector, FVector::RightVector);
		Constraint->SetConstraintReferenceOrientation(EConstraintFrame::Frame2, FVector::ForwardVector, FVector::RightVector);

		Constraint->SetLinearPositionDrive(true, true, true);
		Constraint->SetLinearVelocityDrive(true, true, true);
		Constraint->SetLinearPositionTarget(FVector::ZeroVector);
		Constraint->SetLinearVelocityTarget(FVector::ZeroVector);
		Constraint->SetLinearDriveParams(WhipConfig.LinearSpringStiffness, WhipConfig.LinearSpringDamping, 0.0f);

		Constraint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
		Constraint->SetAngularOrientationDrive(true, true);
		Constraint->SetAngularVelocityDrive(true, true);
		Constraint->SetAngularOrientationTarget(FRotator::ZeroRotator);
		Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
		Constraint->SetAngularDriveParams(WhipConfig.AngularBendStiffness, WhipConfig.AngularBendDamping, 0.0f);

		ActiveChainConstraints.Add(Constraint);
	}
}

void UMinionSquadComponent::DestroyChaosPhysicsChains()
{
	for (UPhysicsConstraintComponent* Constraint : ActiveChainConstraints)
	{
		if (IsValid(Constraint))
		{
			Constraint->BreakConstraint();
			Constraint->DestroyComponent();
		}
	}
	ActiveChainConstraints.Empty();

	for (AActor* MinionActor : ActiveMinions)
	{
		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetWhipModeEnabled(false);
		}
	}

	UpdateHandAnchorTransforms(0.0f);
}

// ============================================================================
// CARRY RUSH SPELL IMPLEMENTATION
// ============================================================================

void UMinionSquadComponent::EnterCarryRushMode()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	CarryRushDirection = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	if (CarryRushDirection.IsNearlyZero())
	{
		CarryRushDirection = FVector::ForwardVector;
	}

	CarryRushElapsedTime = 0.0f;
	CurrentRushSpeed = CarryRushConfig.InitialSpeed;
	HitActorTimestamps.Reset();
	RushIgnoredEnemies.Empty();

	// Cache the player's skeletal mesh Z so we can smoothly lift the player onto the minions' backs
	if (ACharacter* PlayerChar = Cast<ACharacter>(OwnerActor))
	{
		if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
		{
			if (!bPlayerMeshZModified)
			{
				CachedPlayerMeshBaseZ = MeshComp->GetRelativeLocation().Z;
				bPlayerMeshZModified = true;
			}
		}
	}

	UPrimitiveComponent* OwnerRoot = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());

	for (AActor* MinionActor : ActiveMinions)
	{
		if (!IsValid(MinionActor))
		{
			continue;
		}

		if (OwnerRoot)
		{
			OwnerRoot->IgnoreActorWhenMoving(MinionActor, true);
		}
		if (UPrimitiveComponent* MinionRoot = Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()))
		{
			MinionRoot->IgnoreActorWhenMoving(OwnerActor, true);
		}

		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetCarryRushModeEnabled(true);
		}
	}

	BroadcastDebug(TEXT("CARRY RUSH SPELL STARTED! Gathering & Charging..."), FColor::Cyan, 2.5f);
}

void UMinionSquadComponent::ExitCarryRushMode()
{
	AActor* OwnerActor = GetOwner();
	UPrimitiveComponent* OwnerRoot = OwnerActor ? Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent()) : nullptr;

	// Restore the Player SkeletalMesh's original relative Z height
	if (bPlayerMeshZModified)
	{
		if (ACharacter* PlayerChar = Cast<ACharacter>(OwnerActor))
		{
			if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
			{
				FVector RelLoc = MeshComp->GetRelativeLocation();
				RelLoc.Z = CachedPlayerMeshBaseZ;
				MeshComp->SetRelativeLocation(RelLoc);
			}
		}
		bPlayerMeshZModified = false;
	}

	// Re-enable collision with any enemies we plowed through during the rush
	if (OwnerRoot)
	{
		for (const TWeakObjectPtr<AActor>& IgnoredEnemy : RushIgnoredEnemies)
		{
			if (IgnoredEnemy.IsValid())
			{
				OwnerRoot->IgnoreActorWhenMoving(IgnoredEnemy.Get(), false);
			}
		}
	}
	RushIgnoredEnemies.Empty();
	CurrentRushSpeed = 0.0f;

	for (AActor* MinionActor : ActiveMinions)
	{
		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetCarryRushModeEnabled(false);
		}
	}
}

bool UMinionSquadComponent::StartCarryRushSpell()
{
	if (ActiveMinions.IsEmpty())
	{
		BroadcastDebug(TEXT("Cannot Start Carry Rush: No active minions!"), FColor::Red, 2.0f);
		return false;
	}

	SetSquadStance(EMinionSquadStance::CarryRush);
	return true;
}

void UMinionSquadComponent::StopCarryRushSpell()
{
	if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		SetSquadStance(EMinionSquadStance::Follow);
	}
}

FVector UMinionSquadComponent::CalculateCarryPalanquinLocation(
	int32 MinionIdx,
	int32 TotalCount,
	const FVector& PlayerLoc,
	const FVector& ForwardDir,
	const FVector& RightDir) const
{
	// Non-stacking honeycomb/grid layout directly underneath the player:
	// Up to 4 columns wide, 2 rows deep, centered under the player so minions pack shoulder-to-shoulder without overlapping.
	const int32 Cols = FMath::Min(TotalCount, 4);
	const int32 Row = MinionIdx / 4;
	const int32 Col = MinionIdx % 4;
	const int32 ColsInThisRow = (Row == 0) ? Cols : FMath::Max(1, TotalCount - 4);

	const float ColCenterOffset = (ColsInThisRow - 1) * 0.5f;
	const float RowCenterOffset = (TotalCount > 4) ? 0.5f : 0.0f;

	// Honeycomb stagger on Row 1 so minions interlock cleanly without stacking
	const float HoneycombStagger = (Row % 2 == 1 && ColsInThisRow == Cols) ? (CarryRushConfig.PalanquinColSpacing * 0.25f) : 0.0f;

	const float LocalForward = CarryRushConfig.PalanquinForwardOffset - ((Row - RowCenterOffset) * CarryRushConfig.PalanquinRowSpacing);
	const float LocalRight = ((Col - ColCenterOffset) * CarryRushConfig.PalanquinColSpacing) + HoneycombStagger;

	// Position at the floor level of the player's feet
	float TargetZ = PlayerLoc.Z;
	if (const ACharacter* PlayerChar = Cast<ACharacter>(GetOwner()))
	{
		if (const UCapsuleComponent* PlayerCap = PlayerChar->GetCapsuleComponent())
		{
			const float PlayerBottomZ = PlayerLoc.Z - PlayerCap->GetScaledCapsuleHalfHeight();
			TargetZ = PlayerBottomZ + 45.0f; // Standard minion half-height
		}
	}

	return FVector(
		PlayerLoc.X + (ForwardDir.X * LocalForward) + (RightDir.X * LocalRight),
		PlayerLoc.Y + (ForwardDir.Y * LocalForward) + (RightDir.Y * LocalRight),
		TargetZ);
}

void UMinionSquadComponent::PerformCarryRushEnemySweep(const FVector& SweepStart, const FVector& SweepEnd)
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwner();
	if (!World || !PlayerActor)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CarryRushEnemySweep), false, PlayerActor);
	QueryParams.AddIgnoredActor(PlayerActor);
	for (AActor* SquadMinion : ActiveMinions)
	{
		if (IsValid(SquadMinion))
		{
			QueryParams.AddIgnoredActor(SquadMinion);
		}
	}

	// Sweep slightly ahead of the player so enemies are rammed & ignored BEFORE the player's root capsule hits them
	const FVector ExtendedEnd = SweepEnd + (CarryRushDirection * 45.0f);

	TArray<FHitResult> HitResults;
	const bool bHit = World->SweepMultiByChannel(
		HitResults,
		SweepStart,
		ExtendedEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(CarryRushConfig.RamHitboxRadius),
		QueryParams);

	if (!bHit)
	{
		return;
	}

	UPrimitiveComponent* PlayerRoot = Cast<UPrimitiveComponent>(PlayerActor->GetRootComponent());
	const double CurrentTime = World->GetTimeSeconds();

	// Calculate speed-scaled ram damage
	const float SpeedAlpha = FMath::GetMappedRangeValueClamped(
		FVector2D(CarryRushConfig.InitialSpeed, CarryRushConfig.MaxSpeed),
		FVector2D(0.0f, 1.0f),
		CurrentRushSpeed);
	const float RamDamage = CarryRushConfig.BaseRamDamage + (SpeedAlpha * CarryRushConfig.MaxSpeedBonusDamage);

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || HitActor == PlayerActor)
		{
			continue;
		}

		// Only treat Pawns/Characters (or actors on the Pawn channel) as plowable enemies; walls are handled by the movement sweep
		if (!HitActor->IsA<APawn>())
		{
			continue;
		}

		// Make the player's root capsule ignore this enemy so we plow straight through without stopping!
		if (PlayerRoot)
		{
			PlayerRoot->IgnoreActorWhenMoving(HitActor, true);
			RushIgnoredEnemies.AddUnique(HitActor);
		}

		if (const double* LastHit = HitActorTimestamps.Find(HitActor))
		{
			if ((CurrentTime - *LastHit) < 0.4)
			{
				continue;
			}
		}
		HitActorTimestamps.Add(HitActor, CurrentTime);

		// Apply Engine PointDamage if enabled
		if (WhipConfig.bApplyEnginePointDamage)
		{
			APawn* InstigatorPawn = Cast<APawn>(PlayerActor);
			AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
			TSubclassOf<UDamageType> DmgType = WhipConfig.DamageTypeClass
				? WhipConfig.DamageTypeClass
				: TSubclassOf<UDamageType>(UDamageType::StaticClass());

			UGameplayStatics::ApplyPointDamage(
				HitActor,
				RamDamage,
				CarryRushDirection,
				Hit,
				InstigatorController,
				PlayerActor,
				DmgType);
		}

		// Knock the enemy outward to the side and upward like a bowling pin!
		if (ACharacter* HitEnemyChar = Cast<ACharacter>(HitActor))
		{
			const FVector ToEnemy = (HitEnemyChar->GetActorLocation() - PlayerActor->GetActorLocation()).GetSafeNormal2D();
			const FVector KnockDir = (CarryRushDirection * 0.5f + ToEnemy * 0.5f).GetSafeNormal2D();
			const FVector LaunchVel = (KnockDir * CarryRushConfig.EnemyKnockbackStrength) + FVector(0.0f, 0.0f, 380.0f);
			HitEnemyChar->LaunchCharacter(LaunchVel, true, true);
		}

		OnCarryRushEnemyHit.Broadcast(HitActor, RamDamage, CurrentRushSpeed, Hit);

		if (bDebugDrawSlots)
		{
			DrawDebugSphere(World, Hit.ImpactPoint, 35.0f, 12, FColor::Red, false, 1.0f, 0, 3.0f);
		}

		BroadcastDebug(
			FString::Printf(TEXT("Carry Rush Plowed %s for %.1f Dmg (Speed: %.0f)!"), *HitActor->GetName(), RamDamage, CurrentRushSpeed),
			FColor::Red,
			2.0f);
	}
}

void UMinionSquadComponent::UpdateCarryRush(float DeltaTime)
{
	AActor* PlayerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!PlayerActor || !World)
	{
		return;
	}

	for (int32 i = ActiveMinions.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(ActiveMinions[i]))
		{
			ActiveMinions.RemoveAtSwap(i);
		}
	}

	const int32 MinionCount = ActiveMinions.Num();
	if (MinionCount == 0)
	{
		StopCarryRushSpell();
		return;
	}

	CarryRushElapsedTime += DeltaTime;

	// Check safety duration timeout if configured (> 0)
	if (CarryRushConfig.MaxRushDuration > 0.0f && CarryRushElapsedTime >= CarryRushConfig.MaxRushDuration)
	{
		BroadcastDebug(TEXT("Carry Rush finished (Max Duration reached)."), FColor::Yellow, 2.0f);
		StopCarryRushSpell();
		return;
	}

	ACharacter* PlayerChar = Cast<ACharacter>(PlayerActor);
	const FRotator RushRotation = CarryRushDirection.Rotation();
	PlayerActor->SetActorRotation(RushRotation);

	// Smoothly lift the player's visual mesh onto the minions' shoulders during GatherDuration
	if (PlayerChar && bPlayerMeshZModified)
	{
		if (USkeletalMeshComponent* MeshComp = PlayerChar->GetMesh())
		{
			const float GatherAlpha = (CarryRushConfig.GatherDuration > SMALL_NUMBER)
				? FMath::Clamp(CarryRushElapsedTime / CarryRushConfig.GatherDuration, 0.0f, 1.0f)
				: 1.0f;

			FVector RelLoc = MeshComp->GetRelativeLocation();
			RelLoc.Z = FMath::Lerp(CachedPlayerMeshBaseZ, CachedPlayerMeshBaseZ + CarryRushConfig.PlayerMeshLiftOffsetZ, GatherAlpha);
			MeshComp->SetRelativeLocation(RelLoc);
		}
	}

	// Phase 2: Once GatherDuration completes, accelerate straight ahead and check for enemies/walls
	if (CarryRushElapsedTime >= CarryRushConfig.GatherDuration)
	{
		CurrentRushSpeed = FMath::Min(
			CurrentRushSpeed + (CarryRushConfig.Acceleration * DeltaTime),
			CarryRushConfig.MaxSpeed);

		const FVector StartPlayerLoc = PlayerActor->GetActorLocation();
		const FVector DesiredStep = CarryRushDirection * (CurrentRushSpeed * DeltaTime);
		const FVector DesiredEndLoc = StartPlayerLoc + DesiredStep;

		// Pass 1: Sweep for enemies in front of the rush and plow through them
		PerformCarryRushEnemySweep(StartPlayerLoc, DesiredEndLoc);

		// Pass 2: Move the player forward with sweep enabled to detect walls
		FHitResult MoveHit;
		PlayerActor->AddActorWorldOffset(DesiredStep, true, &MoveHit);

		if (MoveHit.bBlockingHit)
		{
			// Check if the hit surface is a steep wall (ImpactNormal.Z < WallNormalZThreshold)
			const bool bIsWall = FMath::Abs(MoveHit.ImpactNormal.Z) < CarryRushConfig.WallNormalZThreshold;

			if (bIsWall)
			{
				const float CrashSpeed = CurrentRushSpeed;
				if (bDebugDrawSlots)
				{
					DrawDebugSphere(World, MoveHit.ImpactPoint, 45.0f, 16, FColor::Orange, false, 2.0f, 0, 4.0f);
				}

				BroadcastDebug(
					FString::Printf(TEXT("CARRY RUSH HIT WALL (%s) at Speed %.0f!"), *GetNameSafe(MoveHit.GetActor()), CrashSpeed),
					FColor::Orange,
					3.0f);

				// Apply recoil bounce to the player on wall impact
				if (PlayerChar && CarryRushConfig.WallBounceBackStrength > 0.0f)
				{
					const FVector BounceVel = (MoveHit.ImpactNormal.GetSafeNormal2D() * CarryRushConfig.WallBounceBackStrength) + FVector(0.0f, 0.0f, 260.0f);
					PlayerChar->LaunchCharacter(BounceVel, true, true);
				}

				OnCarryRushWallImpact.Broadcast(MoveHit.ImpactPoint, MoveHit.ImpactNormal, CrashSpeed, MoveHit);
				StopCarryRushSpell();
				return;
			}
			else
			{
				// It's a walkable ramp/slope: slide along the slope plane and keep rushing!
				const FVector RemainingDelta = DesiredStep * (1.0f - MoveHit.Time);
				const FVector SlopeSlideDelta = FVector::VectorPlaneProject(RemainingDelta, MoveHit.ImpactNormal);
				FHitResult SlideHit;
				PlayerActor->AddActorWorldOffset(SlopeSlideDelta, true, &SlideHit);

				if (SlideHit.bBlockingHit && FMath::Abs(SlideHit.ImpactNormal.Z) < CarryRushConfig.WallNormalZThreshold)
				{
					OnCarryRushWallImpact.Broadcast(SlideHit.ImpactPoint, SlideHit.ImpactNormal, CurrentRushSpeed, SlideHit);
					StopCarryRushSpell();
					return;
				}
			}
		}
	}

	// Update all minions to stay locked in their non-stacking palanquin grid underneath the player
	const FVector UpdatedPlayerLoc = PlayerActor->GetActorLocation();
	const FVector RightDir = FVector::CrossProduct(FVector::UpVector, CarryRushDirection).GetSafeNormal();
	const float InterpSpeed = (CarryRushElapsedTime < CarryRushConfig.GatherDuration) ? 28.0f : 45.0f;

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		const FVector TargetCarrySlot = CalculateCarryPalanquinLocation(i, MinionCount, UpdatedPlayerLoc, CarryRushDirection, RightDir);
		const FVector NewMinionLoc = FMath::VInterpTo(MinionActor->GetActorLocation(), TargetCarrySlot, DeltaTime, InterpSpeed);

		MinionActor->SetActorLocationAndRotation(NewMinionLoc, RushRotation, false);

		if (bDebugDrawSlots)
		{
			DrawDebugSphere(World, TargetCarrySlot, 16.0f, 8, FColor::Cyan, false, -1.0f);
		}
	}

	if (bDebugDrawSlots)
	{
		DrawDebugSphere(World, UpdatedPlayerLoc + (CarryRushDirection * 35.0f), CarryRushConfig.RamHitboxRadius, 16, FColor::Yellow, false, -1.0f);
	}
}

// ============================================================================
// STANCE SWITCHING & WHIP / NAVMESH UPDATES
// ============================================================================

void UMinionSquadComponent::SetSquadStance(EMinionSquadStance NewStance)
{
	if (CurrentStance == NewStance)
	{
		return;
	}

	// Clean up previous stance first
	if (CurrentStance == EMinionSquadStance::AttackWhip)
	{
		bIsSpinAttacking = false;
		bIsContinuousTornadoActive = false;
		DestroyChaosPhysicsChains();
	}
	else if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		ExitCarryRushMode();
	}

	CurrentStance = NewStance;

	if (CurrentStance == EMinionSquadStance::AttackWhip)
	{
		SortMinionsForWhipArms();
		BuildChaosPhysicsChains();

		HitActorTimestamps.Reset();
		ContinuousTornadoAngleOffset = 0.0f;
		SetComponentTickEnabled(true);

		BroadcastDebug(TEXT("Shifted to DAMPED STIFF CHAIN Stance"), FColor::Orange, 2.5f);
	}
	else if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		EnterCarryRushMode();
		SetComponentTickEnabled(true);
	}
	else
	{
		SetComponentTickEnabled(false);
		bForceNextUpdate = true;
		EvaluateSquadAndAvoidance();
		BroadcastDebug(TEXT("Shifted to FOLLOW Stance"), FColor::Green, 2.5f);
	}

	OnSquadStanceChanged.Broadcast(CurrentStance);
}

EMinionSquadStance UMinionSquadComponent::ToggleSquadStance()
{
	SetSquadStance(CurrentStance == EMinionSquadStance::Follow
		? EMinionSquadStance::AttackWhip
		: EMinionSquadStance::Follow);
	return CurrentStance;
}

bool UMinionSquadComponent::StartWhipSpinAttack(bool bAutoEnterAttackStance)
{
	if (ActiveMinions.IsEmpty())
	{
		return false;
	}

	if (CurrentStance != EMinionSquadStance::AttackWhip)
	{
		if (!bAutoEnterAttackStance)
		{
			return false;
		}
		SetSquadStance(EMinionSquadStance::AttackWhip);
	}

	bIsSpinAttacking = true;
	SpinElapsedTime = 0.0f;
	HitActorTimestamps.Reset();

	BroadcastDebug(TEXT("Damped Chain Tornado Spin Triggered!"), FColor::Magenta, 1.5f);
	return true;
}

void UMinionSquadComponent::SetContinuousTornadoSpin(bool bEnableContinuousSpin, bool bAutoEnterAttackStance)
{
	if (bEnableContinuousSpin)
	{
		if (ActiveMinions.IsEmpty())
		{
			return;
		}

		if (CurrentStance != EMinionSquadStance::AttackWhip)
		{
			if (!bAutoEnterAttackStance)
			{
				return;
			}
			SetSquadStance(EMinionSquadStance::AttackWhip);
		}

		bIsContinuousTornadoActive = true;
		bIsSpinAttacking = false;
		HitActorTimestamps.Reset();
	}
	else
	{
		bIsContinuousTornadoActive = false;
		ContinuousTornadoAngleOffset = 0.0f;
		UpdateHandAnchorTransforms(0.0f);
	}
}

void UMinionSquadComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (CurrentStance == EMinionSquadStance::AttackWhip)
	{
		UpdateAttackWhip(DeltaTime);
	}
	else if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		UpdateCarryRush(DeltaTime);
	}
}

void UMinionSquadComponent::UpdateAttackWhip(float DeltaTime)
{
	AActor* PlayerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!PlayerActor || !World)
	{
		return;
	}

	const int32 MinionCount = ActiveMinions.Num();
	if (MinionCount == 0)
	{
		return;
	}

	if (PrevMinionLocations.Num() != MinionCount || InitialMinionHeights.Num() != MinionCount)
	{
		PrevMinionLocations.SetNumUninitialized(MinionCount);
		InitialMinionHeights.SetNumUninitialized(MinionCount);
		for (int32 i = 0; i < MinionCount; ++i)
		{
			const FVector Loc = IsValid(ActiveMinions[i]) ? ActiveMinions[i]->GetActorLocation() : PlayerActor->GetActorLocation();
			PrevMinionLocations[i] = Loc;
			InitialMinionHeights[i] = Loc.Z;
		}
	}

	const float DirectionSign = (WhipConfig.SpinDirection >= 0.0f) ? 1.0f : -1.0f;
	float OrbitAngleYaw = 0.0f;

	if (bIsSpinAttacking)
	{
		SpinElapsedTime += DeltaTime;
		if (SpinElapsedTime >= WhipConfig.SpinDuration)
		{
			bIsSpinAttacking = false;
			SpinElapsedTime = 0.0f;
			UpdateHandAnchorTransforms(0.0f);
		}
		else
		{
			const float Progress = FMath::Clamp(SpinElapsedTime / FMath::Max(WhipConfig.SpinDuration, 0.05f), 0.0f, 1.0f);
			const float EasedProgress = FMath::InterpEaseInOut(0.0f, 1.0f, Progress, 1.5f);
			OrbitAngleYaw = EasedProgress * WhipConfig.SpinRevolutions * 360.0f * DirectionSign;
		}
	}
	else if (bIsContinuousTornadoActive)
	{
		ContinuousTornadoAngleOffset = FMath::Fmod(
			ContinuousTornadoAngleOffset + (WhipConfig.ContinuousTornadoSpeedDegPerSec * DirectionSign * DeltaTime),
			360.0f);
		OrbitAngleYaw = ContinuousTornadoAngleOffset;
	}

	const bool bAnySpinActive = bIsSpinAttacking || bIsContinuousTornadoActive;

	UpdateHandAnchorTransforms(OrbitAngleYaw);

	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const float ReferenceHeightZ = PlayerLocation.Z;

	FVector PrevDebugPos[2] = {
		HandAnchorSpheres[0] ? HandAnchorSpheres[0]->GetComponentLocation() : PlayerLocation,
		HandAnchorSpheres[1] ? HandAnchorSpheres[1]->GetComponentLocation() : PlayerLocation
	};

	for (int32 i = 0; i < MinionCount; ++i)
	{
		AActor* MinionActor = ActiveMinions[i];
		if (!IsValid(MinionActor))
		{
			continue;
		}

		const int32 ArmIndex = WhipConfig.bDualArmWhip ? (i % 2) : 0;
		const int32 LinkIndexInArm = WhipConfig.bDualArmWhip ? (i / 2) : i;
		const FVector CurrentLoc = MinionActor->GetActorLocation();

		if (UPrimitiveComponent* MinionPrim = Cast<UPrimitiveComponent>(MinionActor->GetRootComponent()))
		{
			if (MinionPrim->IsSimulatingPhysics())
			{
				const FVector PhysicsVel = MinionPrim->GetPhysicsLinearVelocity();

				const float TargetZ = InitialMinionHeights.IsValidIndex(i) ? InitialMinionHeights[i] : ReferenceHeightZ;
				const float HeightError = CurrentLoc.Z - TargetZ;
				const float VerticalSpringAccel =
					(-HeightError * WhipConfig.VerticalHeightStiffness) -
					(PhysicsVel.Z * WhipConfig.VerticalHeightDamping) +
					980.0f;

				FVector TotalAccel(0.0f, 0.0f, VerticalSpringAccel);

				if (bAnySpinActive)
				{
					const FVector RadialOut = (CurrentLoc - PlayerLocation).GetSafeNormal2D();
					const FVector TangentDir = FVector::CrossProduct(FVector::UpVector, RadialOut) * DirectionSign;

					TotalAccel += (TangentDir * WhipConfig.TornadoWhipForce) +
						(RadialOut * (WhipConfig.TornadoWhipForce * 0.85f));
				}

				MinionPrim->AddForce(TotalAccel, NAME_None, true);

				const float Speed2D = PhysicsVel.Size2D();
				if (bAnySpinActive || Speed2D >= WhipConfig.MinPhysicsSpeedForDamage)
				{
					PerformWhipHitSweep(MinionActor, LinkIndexInArm, PrevMinionLocations[i], CurrentLoc);
				}
			}
		}

		if (bDebugDrawSlots)
		{
			const FColor ArmColor = bAnySpinActive
				? ((ArmIndex == 0) ? FColor::Magenta : FColor::Purple)
				: ((ArmIndex == 0) ? FColor::Orange : FColor::Cyan);

			DrawDebugLine(World, PrevDebugPos[ArmIndex], CurrentLoc, ArmColor, false, -1.0f, 0, 3.5f);
			DrawDebugSphere(World, CurrentLoc, WhipConfig.HitSphereRadius * 0.45f, 8, ArmColor, false, -1.0f);
		}

		PrevDebugPos[ArmIndex] = CurrentLoc;
		PrevMinionLocations[i] = CurrentLoc;
	}
}

void UMinionSquadComponent::PerformWhipHitSweep(AActor* MinionActor, int32 LinkIndexInArm, const FVector& StartLoc, const FVector& EndLoc)
{
	UWorld* World = GetWorld();
	AActor* PlayerActor = GetOwner();
	if (!World || !PlayerActor || !MinionActor)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MinionWhipSweep), false, PlayerActor);
	QueryParams.AddIgnoredActor(PlayerActor);
	for (AActor* SquadMinion : ActiveMinions)
	{
		if (IsValid(SquadMinion))
		{
			QueryParams.AddIgnoredActor(SquadMinion);
		}
	}

	TArray<FHitResult> HitResults;
	const bool bHit = World->SweepMultiByChannel(
		HitResults,
		StartLoc,
		EndLoc,
		FQuat::Identity,
		WhipConfig.HitTraceChannel,
		FCollisionShape::MakeSphere(WhipConfig.HitSphereRadius),
		QueryParams);

	if (!bHit)
	{
		return;
	}

	const double CurrentTime = World->GetTimeSeconds();

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor))
		{
			continue;
		}

		if (const double* LastHitTime = HitActorTimestamps.Find(HitActor))
		{
			if ((CurrentTime - *LastHitTime) < static_cast<double>(WhipConfig.HitCooldownPerTarget))
			{
				continue;
			}
		}

		HitActorTimestamps.Add(HitActor, CurrentTime);

		const float CalculatedDamage = WhipConfig.BaseDamage + (LinkIndexInArm * WhipConfig.BonusDamagePerLink);
		const FVector ShotDirection = (EndLoc - StartLoc).GetSafeNormal();

		if (WhipConfig.bApplyEnginePointDamage)
		{
			APawn* InstigatorPawn = Cast<APawn>(PlayerActor);
			AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
			TSubclassOf<UDamageType> DmgType = WhipConfig.DamageTypeClass
				? WhipConfig.DamageTypeClass
				: TSubclassOf<UDamageType>(UDamageType::StaticClass());

			UGameplayStatics::ApplyPointDamage(
				HitActor,
				CalculatedDamage,
				ShotDirection,
				Hit,
				InstigatorController,
				MinionActor,
				DmgType);
		}

		OnWhipDamageHit.Broadcast(HitActor, MinionActor, CalculatedDamage, LinkIndexInArm, Hit);

		if (bDebugDrawSlots)
		{
			DrawDebugSphere(World, Hit.ImpactPoint, 28.0f, 12, FColor::Red, false, 0.8f, 0, 2.5f);
		}
	}
}

void UMinionSquadComponent::AutoRegisterMinionsInWorld()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	TArray<AActor*> FoundMinions;
	UGameplayStatics::GetAllActorsWithInterface(World, UMinionAgentInterface::StaticClass(), FoundMinions);

	const FVector PlayerLoc = OwnerActor->GetActorLocation();
	FoundMinions.Sort([&PlayerLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), PlayerLoc) < FVector::DistSquared(B.GetActorLocation(), PlayerLoc);
	});

	for (AActor* Minion : FoundMinions)
	{
		if (ActiveMinions.Num() >= MaxMinions)
		{
			break;
		}
		RegisterMinion(Minion);
	}
}

bool UMinionSquadComponent::RegisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor) || !MinionActor->Implements<UMinionAgentInterface>())
	{
		return false;
	}

	if (ActiveMinions.Num() >= MaxMinions || ActiveMinions.Contains(MinionActor))
	{
		return false;
	}

	const int32 NewSlotIndex = ActiveMinions.Add(MinionActor);
	bForceNextUpdate = true;

	if (CurrentStance == EMinionSquadStance::AttackWhip)
	{
		SortMinionsForWhipArms();
		BuildChaosPhysicsChains();
	}
	else if (CurrentStance == EMinionSquadStance::CarryRush)
	{
		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetCarryRushModeEnabled(true);
		}
	}

	OnMinionRegistered.Broadcast(MinionActor, NewSlotIndex, ActiveMinions.Num());
	EvaluateSquadAndAvoidance();
	return true;
}

void UMinionSquadComponent::UnregisterMinion(AActor* MinionActor)
{
	if (!IsValid(MinionActor))
	{
		return;
	}

	if (ActiveMinions.RemoveSingleSwap(MinionActor) > 0)
	{
		if (IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor))
		{
			MinionInterface->SetWhipModeEnabled(false);
			MinionInterface->SetCarryRushModeEnabled(false);
		}

		if (CurrentStance == EMinionSquadStance::AttackWhip)
		{
			SortMinionsForWhipArms();
			BuildChaosPhysicsChains();
		}

		bForceNextUpdate = true;
		OnMinionUnregistered.Broadcast(MinionActor, ActiveMinions.Num());
	}
}

void UMinionSquadComponent::OnPlayerAvoidanceOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (CurrentStance != EMinionSquadStance::Follow)
	{
		return;
	}

	AActor* PlayerActor = GetOwner();
	if (!PlayerActor || !IsValid(OtherActor) || OtherActor == PlayerActor)
	{
		return;
	}

	if (IMinionAgentInterface* Minion = Cast<IMinionAgentInterface>(OtherActor))
	{
		if (Minion->GetMinionState() != EMinionState::TraversingNavLink)
		{
			Minion->EvadePlayer(
				PlayerActor->GetActorLocation(),
				PlayerActor->GetVelocity(),
				PlayerActor->GetActorRightVector());
		}
	}
}

void UMinionSquadComponent::EvaluateSquadAndAvoidance()
{
	if (CurrentStance != EMinionSquadStance::Follow)
	{
		return;
	}

	AActor* PlayerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!PlayerActor || !World || ActiveMinions.IsEmpty())
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance())
	{
		return;
	}

	for (int32 i = ActiveMinions.Num() - 1; i >= 0; --i)
	{
		if (!IsValid(ActiveMinions[i]))
		{
			ActiveMinions.RemoveAtSwap(i);
		}
	}

	const int32 MinionCount = ActiveMinions.Num();
	if (MinionCount == 0)
	{
		return;
	}

	const FVector PlayerLocation = PlayerActor->GetActorLocation();
	const FVector PlayerVelocity = PlayerActor->GetVelocity();
	const FVector PlayerRight = PlayerActor->GetActorRightVector();
	const float EvadeRadiusSq = FMath::Square(PlayerEvadeRadius);

	const FVector CurrentAnchorLoc = GetComponentLocation();
	const FRotator CurrentAnchorRot = GetComponentRotation();

	const bool bAnchorMoved = bForceNextUpdate ||
		FVector::DistSquared(CurrentAnchorLoc, LastAnchorLocation) >= FMath::Square(AnchorMoveThreshold) ||
		!CurrentAnchorRot.Equals(LastAnchorRotation, 8.0f);

	if (bAnchorMoved)
	{
		LastAnchorLocation = CurrentAnchorLoc;
		LastAnchorRotation = CurrentAnchorRot;
		bForceNextUpdate = false;
		OnSquadMoveUpdated.Broadcast(MinionCount, CurrentAnchorLoc);
	}

	if (bDebugDrawSlots)
	{
		DrawDebugSphere(World, CurrentAnchorLoc, 18.0f, 8, FColor::Yellow, false, UpdateInterval * 1.1f);
		DrawDebugCircle(World, PlayerLocation, PlayerEvadeRadius, 24, FColor::Red, false, UpdateInterval * 1.1f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}

	const FVector NavProjectionExtent(250.0f, 250.0f, 500.0f);

	TArray<FVector, TInlineAllocator<16>> ProjectedSlots;
	ProjectedSlots.SetNumUninitialized(MinionCount);

	for (int32 SlotIdx = 0; SlotIdx < MinionCount; ++SlotIdx)
	{
		const FVector IdealWorldSlot = CalculateSlotWorldLocation(SlotIdx);
		FNavLocation ProjectedLoc;

		if (NavSys->ProjectPointToNavigation(IdealWorldSlot, ProjectedLoc, NavProjectionExtent))
		{
			ProjectedSlots[SlotIdx] = ProjectedLoc.Location;
		}
		else if (NavSys->ProjectPointToNavigation(PlayerLocation, ProjectedLoc, NavProjectionExtent))
		{
			ProjectedSlots[SlotIdx] = ProjectedLoc.Location;
		}
		else
		{
			ProjectedSlots[SlotIdx] = IdealWorldSlot;
		}
	}

	TArray<bool, TInlineAllocator<16>> bMinionAssigned;
	bMinionAssigned.Init(false, MinionCount);

	for (int32 SlotIdx = 0; SlotIdx < MinionCount; ++SlotIdx)
	{
		const FVector& SlotLocation = ProjectedSlots[SlotIdx];
		int32 BestMinionIdx = INDEX_NONE;
		float BestDistSq = MAX_flt;

		for (int32 MinionIdx = 0; MinionIdx < MinionCount; ++MinionIdx)
		{
			if (bMinionAssigned[MinionIdx])
			{
				continue;
			}

			const float DistSq = FVector::DistSquared2D(ActiveMinions[MinionIdx]->GetActorLocation(), SlotLocation);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestMinionIdx = MinionIdx;
			}
		}

		if (BestMinionIdx == INDEX_NONE)
		{
			continue;
		}

		bMinionAssigned[BestMinionIdx] = true;
		AActor* MinionActor = ActiveMinions[BestMinionIdx];
		IMinionAgentInterface* MinionInterface = Cast<IMinionAgentInterface>(MinionActor);
		if (!MinionInterface)
		{
			continue;
		}

		const EMinionState CurrentState = MinionInterface->GetMinionState();
		if (CurrentState == EMinionState::TraversingNavLink)
		{
			continue;
		}

		if (FVector::DistSquared2D(MinionActor->GetActorLocation(), PlayerLocation) < EvadeRadiusSq)
		{
			MinionInterface->EvadePlayer(PlayerLocation, PlayerVelocity, PlayerRight);
			continue;
		}

		if (bDebugDrawSlots)
		{
			DrawDebugSphere(World, SlotLocation, 14.0f, 8, FColor::Green, false, UpdateInterval * 1.1f);
			DrawDebugLine(World, MinionActor->GetActorLocation(), SlotLocation, FColor::Cyan, false, UpdateInterval * 1.1f, 0, 1.5f);
		}

		const float DistToSlotSq = FVector::DistSquared2D(MinionActor->GetActorLocation(), SlotLocation);
		if (bAnchorMoved || CurrentState == EMinionState::Idle || DistToSlotSq > FMath::Square(35.0f))
		{
			MinionInterface->MoveToSquadSlot(SlotLocation);
		}
	}
}

FVector UMinionSquadComponent::CalculateSlotWorldLocation(int32 SlotIndex) const
{
	constexpr int32 ColumnsPerRow = 4;
	const int32 Row = SlotIndex / ColumnsPerRow;
	const int32 Col = SlotIndex % ColumnsPerRow;

	const float CenterOffset = (ColumnsPerRow - 1) * 0.5f;
	const float NormalizedCol = FMath::Abs(Col - CenterOffset);
	const float CrescentPullback = (NormalizedCol > 1.0f) ? 25.0f : 0.0f;

	const float LocalX = -(Row * RowSpacing) - CrescentPullback;
	const float RowStagger = (Row % 2 == 1) ? (ColumnSpacing * 0.35f) : 0.0f;
	const float LocalY = ((Col - CenterOffset) * ColumnSpacing) + RowStagger;

	return GetComponentTransform().TransformPosition(FVector(LocalX, LocalY, 0.0f));
}