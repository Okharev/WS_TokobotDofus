// MinionFormationSolver.cpp
#include "MinionFormationSolver.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"

void FMinionFormationSolver::Reset(const FVector& InitialNavLocation, const FVector& InitialForward)
{
	Breadcrumbs.Reset();
	Breadcrumbs.Add(InitialNavLocation);

	const FVector SafeFwd = InitialForward.GetSafeNormal2D();
	CachedTrailBackDir = SafeFwd.IsNearlyZero() ? -FVector::ForwardVector : -SafeFwd;
	CurrentMode = EMinionFormationMode::Crescent;
}

void FMinionFormationSolver::RecordBreadcrumb(
	const FVector& CurrentPlayerNavLoc,
	const FVector& FallbackForward,
	const FMinionFormationConfig& Config)
{
	if (Breadcrumbs.IsEmpty())
	{
		Reset(CurrentPlayerNavLoc, FallbackForward);
		return;
	}

	const FVector& Latest = Breadcrumbs.Last();
	const float DistSq2D = FVector::DistSquared2D(Latest, CurrentPlayerNavLoc);
	const float VerticalJump = FMath::Abs(CurrentPlayerNavLoc.Z - Latest.Z);

	// Wipe stale trail after teleports, large jumps, or level transitions
	if (DistSq2D > FMath::Square(600.0f) || VerticalJump > 220.0f)
	{
		Reset(CurrentPlayerNavLoc, FallbackForward);
		return;
	}

	const float SampleStep = FMath::Max(Config.BreadcrumbSampleStep, 20.0f);
	if (DistSq2D >= FMath::Square(SampleStep))
	{
		const FVector MoveDir = (CurrentPlayerNavLoc - Latest).GetSafeNormal2D();
		if (!MoveDir.IsNearlyZero())
		{
			// Smoothly blend trail direction so small player zigzags don't whip the rear formation
			CachedTrailBackDir = FMath::Lerp(CachedTrailBackDir, -MoveDir, 0.55f).GetSafeNormal2D();
		}

		Breadcrumbs.Add(CurrentPlayerNavLoc);
		const int32 MaxCount = FMath::Max(Config.MaxBreadcrumbs, 4);
		if (Breadcrumbs.Num() > MaxCount)
		{
			Breadcrumbs.RemoveAt(0, 1, EAllowShrinking::No);
		}
	}
}

FVector FMinionFormationSolver::GetTailBreadcrumbOrFallback(const FVector& FallbackLoc) const
{
	if (Breadcrumbs.Num() >= 2)
	{
		// Pick a breadcrumb ~3-4 steps behind the player rather than the absolute oldest far-away point
		const int32 DesiredIdx = FMath::Max(0, Breadcrumbs.Num() - 5);
		return Breadcrumbs[DesiredIdx];
	}
	return Breadcrumbs.IsEmpty() ? FallbackLoc : Breadcrumbs[0];
}

FVector FMinionFormationSolver::ClampPointOnNavMesh(
	const UNavigationSystemV1* NavSys,
	const ANavigationData* NavData,
	const FSharedConstNavQueryFilter& Filter,
	const UObject* Querier,
	const FVector& RayOrigin,
	const FVector& DesiredPoint,
	float EdgeMargin,
	bool& bOutHitBoundary,
	float& OutClearanceDist)
{
	const FVector Delta = DesiredPoint - RayOrigin;
	const float DesiredDist = Delta.Size2D();
	if (DesiredDist < KINDA_SMALL_NUMBER)
	{
		bOutHitBoundary = false;
		OutClearanceDist = 0.0f;
		return RayOrigin;
	}

	FVector HitLocation = DesiredPoint;
	bOutHitBoundary = NavData->Raycast(RayOrigin, DesiredPoint, HitLocation, Filter, Querier);

	if (bOutHitBoundary)
	{
		const float HitDist = FVector::Dist2D(RayOrigin, HitLocation);
		const float SafeDist = FMath::Max(0.0f, HitDist - EdgeMargin);
		OutClearanceDist = SafeDist;

		const FVector RayDir = (HitLocation - RayOrigin).GetSafeNormal2D();
		const FVector PulledBack = RayOrigin + (RayDir * SafeDist);

		// Tight vertical projection preserves exact ramp Z while refusing to snap down cliffs
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(PulledBack, Projected, FVector(20.0f, 20.0f, 110.0f), NavData, Filter))
		{
			return Projected.Location;
		}
		return HitLocation;
	}

	OutClearanceDist = DesiredDist;
	FNavLocation Projected;
	if (NavSys->ProjectPointToNavigation(DesiredPoint, Projected, FVector(20.0f, 20.0f, 120.0f), NavData, Filter))
	{
		return Projected.Location;
	}
	return DesiredPoint;
}

FVector FMinionFormationSolver::SampleBreadcrumbTrail(
	const FVector& AnchorNavLoc,
	float DistanceAlongTrail,
	const FVector& FallbackDir) const
{
	if (Breadcrumbs.IsEmpty())
	{
		return AnchorNavLoc + (FallbackDir * DistanceAlongTrail);
	}

	float Remaining = DistanceAlongTrail;

	// Segment 0: live AnchorNavLoc -> newest recorded breadcrumb (ensures zero pop between samples)
	{
		const FVector& Head = Breadcrumbs.Last();
		const float HeadSegLen = FVector::Dist2D(AnchorNavLoc, Head);
		if (HeadSegLen >= Remaining && HeadSegLen > KINDA_SMALL_NUMBER)
		{
			return FMath::Lerp(AnchorNavLoc, Head, Remaining / HeadSegLen);
		}
		Remaining -= HeadSegLen;
	}

	// Walk backward through recorded breadcrumb history
	for (int32 i = Breadcrumbs.Num() - 1; i > 0; --i)
	{
		const FVector& A = Breadcrumbs[i];
		const FVector& B = Breadcrumbs[i - 1];
		const float SegLen = FVector::Dist2D(A, B);

		if (SegLen >= Remaining && SegLen > KINDA_SMALL_NUMBER)
		{
			return FMath::Lerp(A, B, Remaining / SegLen);
		}
		Remaining -= SegLen;
	}

	// Extrapolate smoothly past the oldest breadcrumb along tail tangent
	FVector TailDir = FallbackDir;
	if (Breadcrumbs.Num() >= 2)
	{
		const FVector ComputedTail = (Breadcrumbs[0] - Breadcrumbs[1]).GetSafeNormal2D();
		if (!ComputedTail.IsNearlyZero())
		{
			TailDir = ComputedTail;
		}
	}
	return Breadcrumbs[0] + (TailDir * Remaining);
}

bool FMinionFormationSolver::IsSlotWellSeparated(
	const FVector& Candidate,
	const FVector& AnchorNavLoc,
	const TArray<FVector, TInlineAllocator<16>>& AcceptedSlots,
	float MinSlotSepSq,
	float MinPlayerDistSq) const
{
	if (MinPlayerDistSq > 0.0f && FVector::DistSquared2D(Candidate, AnchorNavLoc) < MinPlayerDistSq)
	{
		return false;
	}

	for (const FVector& Existing : AcceptedSlots)
	{
		if (FVector::DistSquared2D(Candidate, Existing) < MinSlotSepSq)
		{
			return false;
		}
	}
	return true;
}

FMinionFormationSolveResult FMinionFormationSolver::SolveFormation(
	const UNavigationSystemV1* NavSys,
	const ANavigationData* NavData,
	const UObject* Querier,
	const FVector& AnchorNavLoc,
	const FVector& AnchorForward,
	int32 MinionCount,
	const FMinionFormationConfig& Config)
{
	FMinionFormationSolveResult Result;
	if (!NavSys || !NavData || MinionCount <= 0)
	{
		return Result;
	}

	const FSharedConstNavQueryFilter Filter = UNavigationQueryFilter::GetQueryFilter(*NavData, Querier, nullptr);

	const FVector BackDir = CachedTrailBackDir.IsNearlyZero()
		? -AnchorForward.GetSafeNormal2D()
		: CachedTrailBackDir;
	const FVector RightDir = FVector::CrossProduct(FVector::UpVector, -BackDir).GetSafeNormal2D();
	Result.TrailBackDirection = BackDir;
	Result.DebugProbeOrigin = AnchorNavLoc;

	// 1. Topology Probes on the NavMesh (detects walls AND cliff edges simultaneously)
	constexpr float ProbeDist = 260.0f;
	bool bHitLeft = false, bHitRight = false, bHitBack = false;
	float LeftClear = 0.0f, RightClear = 0.0f, BackClear = 0.0f;

	Result.DebugLeftHit = ClampPointOnNavMesh(
		NavSys, NavData, Filter, Querier, AnchorNavLoc,
		AnchorNavLoc - RightDir * ProbeDist, Config.WallAndLedgeMargin, bHitLeft, LeftClear);

	Result.DebugRightHit = ClampPointOnNavMesh(
		NavSys, NavData, Filter, Querier, AnchorNavLoc,
		AnchorNavLoc + RightDir * ProbeDist, Config.WallAndLedgeMargin, bHitRight, RightClear);

	Result.DebugBackHit = ClampPointOnNavMesh(
		NavSys, NavData, Filter, Querier, AnchorNavLoc,
		AnchorNavLoc + BackDir * ProbeDist, Config.WallAndLedgeMargin, bHitBack, BackClear);

	Result.BackClearance = BackClear;

	// Secondary width probe 1 row behind player (catches entering/exiting a bridge cleanly)
	const FVector RearProbeOrigin = SampleBreadcrumbTrail(
		AnchorNavLoc,
		FMath::Min(Config.PlayerBackOffset, BackClear),
		BackDir);

	float RearLeftClear = LeftClear;
	float RearRightClear = RightClear;
	if (FVector::DistSquared2D(AnchorNavLoc, RearProbeOrigin) > FMath::Square(35.0f))
	{
		bool bRearHitL = false, bRearHitR = false;
		ClampPointOnNavMesh(
			NavSys, NavData, Filter, Querier, RearProbeOrigin,
			RearProbeOrigin - RightDir * ProbeDist, Config.WallAndLedgeMargin, bRearHitL, RearLeftClear);
		ClampPointOnNavMesh(
			NavSys, NavData, Filter, Querier, RearProbeOrigin,
			RearProbeOrigin + RightDir * ProbeDist, Config.WallAndLedgeMargin, bRearHitR, RearRightClear);
	}

	const float PlayerCorridorWidth = LeftClear + RightClear;
	const float RearCorridorWidth = RearLeftClear + RearRightClear;
	const float EffectiveWidth = FMath::Min(PlayerCorridorWidth, RearCorridorWidth);
	Result.EffectiveCorridorWidth = EffectiveWidth;

	// 2. Mode selection with Hysteresis to stop doorway/bridge flickering
	const float SingleThresh = (CurrentMode == EMinionFormationMode::SingleFileTrail)
		? (Config.SingleFileWidthThreshold + Config.WidthHysteresis)
		: Config.SingleFileWidthThreshold;

	const float DoubleThresh = (CurrentMode == EMinionFormationMode::DoubleFile)
		? (Config.DoubleFileWidthThreshold + Config.WidthHysteresis)
		: Config.DoubleFileWidthThreshold;

	const float BackThresh = (CurrentMode == EMinionFormationMode::AdaptiveFan)
		? (Config.MinBackClearance + 25.0f)
		: Config.MinBackClearance;

	if (BackClear < BackThresh)
	{
		CurrentMode = EMinionFormationMode::AdaptiveFan;
	}
	else if (EffectiveWidth < SingleThresh)
	{
		CurrentMode = EMinionFormationMode::SingleFileTrail;
	}
	else if (EffectiveWidth < DoubleThresh)
	{
		CurrentMode = EMinionFormationMode::DoubleFile;
	}
	else
	{
		CurrentMode = EMinionFormationMode::Crescent;
	}

	Result.ActiveMode = CurrentMode;
	Result.bSuppressLateralEvade =
		(CurrentMode == EMinionFormationMode::SingleFileTrail) ||
		(PlayerCorridorWidth < SingleThresh);

	const float MinSlotSepSq = FMath::Square(Config.MinSlotSeparation);
	const float MinPlayerDistSq = FMath::Square(Config.PlayerSafeRadius);
	// Shifts corridor formations toward the centerline if the player hugs one wall/rail
	const float CorridorCenterShift = (RightClear - LeftClear) * 0.5f;

	auto TryAddValidatedSlot = [&](const FVector& RayOrigin, const FVector& IdealPoint, bool bEnforcePlayerDist) -> bool
	{
		bool bHit = false;
		float ClearDist = 0.0f;
		const FVector Validated = ClampPointOnNavMesh(
			NavSys, NavData, Filter, Querier, RayOrigin, IdealPoint, Config.WallAndLedgeMargin, bHit, ClearDist);

		if (IsSlotWellSeparated(Validated, AnchorNavLoc, Result.Slots, MinSlotSepSq, bEnforcePlayerDist ? MinPlayerDistSq : 0.0f))
		{
			Result.Slots.Add(Validated);
			return true;
		}
		return false;
	};

	// 3. Mode-specific primary slot generation
	if (CurrentMode == EMinionFormationMode::SingleFileTrail)
	{
		FVector PrevValidPoint = AnchorNavLoc;
		for (int32 i = 0; i < MinionCount; ++i)
		{
			const float TrailDist = Config.PlayerBackOffset + (i * Config.SingleFileSpacing);
			const FVector TrailSample = SampleBreadcrumbTrail(AnchorNavLoc, TrailDist, BackDir);

			// Compute local tangent at this point in the trail so centering works around bends
			const FVector LocalBack = (TrailSample - PrevValidPoint).GetSafeNormal2D();
			const FVector LocalRight = LocalBack.IsNearlyZero()
				? RightDir
				: FVector::CrossProduct(FVector::UpVector, -LocalBack).GetSafeNormal2D();

			bool bHL = false, bHR = false;
			float cL = 0.0f, cR = 0.0f;
			ClampPointOnNavMesh(NavSys, NavData, Filter, Querier, TrailSample, TrailSample - LocalRight * 120.0f, Config.WallAndLedgeMargin, bHL, cL);
			ClampPointOnNavMesh(NavSys, NavData, Filter, Querier, TrailSample, TrailSample + LocalRight * 120.0f, Config.WallAndLedgeMargin, bHR, cR);
			const FVector CenteredSample = TrailSample + LocalRight * ((cR - cL) * 0.5f);

			bool bHit = false;
			float Dist = 0.0f;
			// Raycast sequentially from PrevValidPoint so the line bends cleanly around corners
			const FVector Validated = ClampPointOnNavMesh(
				NavSys, NavData, Filter, Querier, PrevValidPoint, CenteredSample, Config.WallAndLedgeMargin, bHit, Dist);

			if (IsSlotWellSeparated(Validated, AnchorNavLoc, Result.Slots, FMath::Square(Config.MinSlotSeparation * 0.75f), MinPlayerDistSq))
			{
				Result.Slots.Add(Validated);
				PrevValidPoint = Validated;
			}
		}
	}
	else if (CurrentMode == EMinionFormationMode::DoubleFile || CurrentMode == EMinionFormationMode::Crescent)
	{
		const int32 Cols = (CurrentMode == EMinionFormationMode::Crescent) ? 4 : 2;
		const float CenterCol = (Cols - 1) * 0.5f;
		const float LatShift = (CurrentMode == EMinionFormationMode::DoubleFile) ? CorridorCenterShift : 0.0f;

		for (int32 SlotIdx = 0; SlotIdx < MinionCount; ++SlotIdx)
		{
			const int32 Row = SlotIdx / Cols;
			const int32 Col = SlotIdx % Cols;
			const float NormCol = FMath::Abs(Col - CenterCol);
			const float CrescentPull = (Cols == 4 && NormCol > 1.0f) ? 25.0f : 0.0f;
			const float Stagger = (Row % 2 == 1) ? (Config.ColumnSpacing * 0.3f) : 0.0f;

			const float BackDist = Config.PlayerBackOffset + (Row * Config.RowSpacing) + CrescentPull;
			const float LateralOffset = ((Col - CenterCol) * Config.ColumnSpacing) + Stagger + LatShift;

			const FVector RowSpine = SampleBreadcrumbTrail(AnchorNavLoc, BackDist, BackDir);
			const FVector IdealPos = RowSpine + (RightDir * LateralOffset);

			TryAddValidatedSlot(AnchorNavLoc, IdealPos, true);
		}
	}

	// 4. AdaptiveFan: primary solver when back is to wall/ledge, AND automatic overflow for any rejected grid/trail slots
	if (Result.Slots.Num() < MinionCount)
	{
		// Preference order: rear -> rear-diagonals -> sides -> forward-diagonals -> front (dead-end corner)
		static constexpr float FanAnglesDeg[] = {
			0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 105.0f, -105.0f, 140.0f, -140.0f, 175.0f
		};

		for (int32 Ring = 0; Ring < 3 && Result.Slots.Num() < MinionCount; ++Ring)
		{
			const float RingRadius = Config.PlayerBackOffset + (Ring * Config.RowSpacing);
			for (float AngleDeg : FanAnglesDeg)
			{
				if (Result.Slots.Num() >= MinionCount)
				{
					break;
				}

				const FVector RayDir = BackDir.RotateAngleAxis(AngleDeg, FVector::UpVector);
				const FVector IdealPos = AnchorNavLoc + (RayDir * RingRadius);
				TryAddValidatedSlot(AnchorNavLoc, IdealPos, true);
			}
		}
	}

	// 5. Extreme tight-island fallback (e.g., 16 minions on a tiny pillar): relax separation so everyone gets valid ground
	while (Result.Slots.Num() < MinionCount)
	{
		const float Angle = (2.0f * PI * static_cast<float>(Result.Slots.Num())) / static_cast<float>(FMath::Max(MinionCount, 1));
		const FVector MicroOffset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 55.0f;
		bool bHit = false;
		float Clear = 0.0f;
		const FVector Fallback = ClampPointOnNavMesh(
			NavSys, NavData, Filter, Querier, AnchorNavLoc,
			AnchorNavLoc + MicroOffset, Config.WallAndLedgeMargin * 0.5f, bHit, Clear);
		Result.Slots.Add(Fallback);
	}

	return Result;
}