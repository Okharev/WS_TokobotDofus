// MinionFormationSolver.h
#pragma once

#include "CoreMinimal.h"
#include "NavigationData.h"
#include "MinionFormationSolver.generated.h"

class UNavigationSystemV1;

UENUM(BlueprintType)
enum class EMinionFormationMode : uint8
{
	Crescent        UMETA(DisplayName = "Open Crescent (4-Col)"),
	DoubleFile      UMETA(DisplayName = "Medium Corridor (2-Col)"),
	SingleFileTrail UMETA(DisplayName = "Thin Bridge / Single-File Trail"),
	AdaptiveFan     UMETA(DisplayName = "Wall / Ledge Adaptive Fan")
};

/**
 * Designer-tunable configuration for topology-aware squad formation solving.
 */
USTRUCT(BlueprintType)
struct WS_TOKOBOTDOFUS_API FMinionFormationConfig
{
	GENERATED_BODY()

	/** Distance between formation rows in open/corridor modes (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float RowSpacing = 85.0f;

	/** Lateral distance between columns in open/corridor modes (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float ColumnSpacing = 75.0f;

	/** Spacing between consecutive minions in SingleFileTrail mode (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float SingleFileSpacing = 72.0f;

	/** Distance behind the player where the first row/minion begins (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float PlayerBackOffset = 135.0f;

	/** Minimum clearance from player center so slots never trigger PlayerEvadeRadius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float PlayerSafeRadius = 100.0f;

	/** Safety pullback from NavMesh walls and cliff edges (CapsuleRadius + buffer, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float WallAndLedgeMargin = 42.0f;

	/** Minimum distance between any two accepted slots to prevent corner stacking (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float MinSlotSeparation = 58.0f;

	/** Distance the player must travel before recording a new breadcrumb (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Breadcrumbs")
	float BreadcrumbSampleStep = 55.0f;

	/** Maximum stored breadcrumbs in the rolling trail history. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Breadcrumbs")
	int32 MaxBreadcrumbs = 32;

	/** Corridor width below which the squad collapses into single-file (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Topology")
	float SingleFileWidthThreshold = 155.0f;

	/** Corridor width below which the squad switches to 2-column double-file (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Topology")
	float DoubleFileWidthThreshold = 280.0f;

	/** Rear clearance below which the squad switches to AdaptiveFan (back to wall/ledge, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Topology")
	float MinBackClearance = 110.0f;

	/** Hysteresis band preventing rapid flickering at doorways/bridge entrances (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation|Topology")
	float WidthHysteresis = 25.0f;
};

struct FMinionFormationSolveResult
{
	EMinionFormationMode ActiveMode = EMinionFormationMode::Crescent;
	TArray<FVector, TInlineAllocator<16>> Slots;
	FVector TrailBackDirection = -FVector::ForwardVector;
	float EffectiveCorridorWidth = 500.0f;
	float BackClearance = 260.0f;
	bool bSuppressLateralEvade = false;

	// Debug probe endpoints for PIE visualization
	FVector DebugProbeOrigin = FVector::ZeroVector;
	FVector DebugLeftHit = FVector::ZeroVector;
	FVector DebugRightHit = FVector::ZeroVector;
	FVector DebugBackHit = FVector::ZeroVector;
};

/**
 * Timer-driven NavMesh topology & breadcrumb formation solver.
 * Guarantees every output slot lies continuously on the player's NavMesh island
 * without leaking across walls or down cliff faces.
 */
class WS_TOKOBOTDOFUS_API FMinionFormationSolver
{
public:
	void Reset(const FVector& InitialNavLocation, const FVector& InitialForward);

	/** Records a grounded player breadcrumb when travel >= BreadcrumbSampleStep. */
	void RecordBreadcrumb(
		const FVector& CurrentPlayerNavLoc,
		const FVector& FallbackForward,
		const FMinionFormationConfig& Config);

	/** Probes surrounding NavMesh geometry and outputs validated, de-clumped slots. */
	FMinionFormationSolveResult SolveFormation(
		const UNavigationSystemV1* NavSys,
		const ANavigationData* NavData,
		const UObject* Querier,
		const FVector& AnchorNavLoc,
		const FVector& AnchorForward,
		int32 MinionCount,
		const FMinionFormationConfig& Config);

	EMinionFormationMode GetCurrentMode() const { return CurrentMode; }
	FVector GetLastValidTrailBackDir() const { return CachedTrailBackDir; }
	const TArray<FVector>& GetBreadcrumbs() const { return Breadcrumbs; }

	/** Returns the oldest safe point along the breadcrumb trail (ideal for off-screen catch-up warps). */
	FVector GetTailBreadcrumbOrFallback(const FVector& FallbackLoc) const;

	/**
	 * Casts a 2D NavMesh ray from RayOrigin to DesiredPoint, pulling back by EdgeMargin on hit
	 * and tightly snapping Z for slopes/ramps without jumping down ledges.
	 */
	static FVector ClampPointOnNavMesh(
		const UNavigationSystemV1* NavSys,
		const ANavigationData* NavData,
		const FSharedConstNavQueryFilter& Filter,
		const UObject* Querier,
		const FVector& RayOrigin,
		const FVector& DesiredPoint,
		float EdgeMargin,
		bool& bOutHitBoundary,
		float& OutClearanceDist);

private:
	/** Samples a continuous point at DistanceAlongTrail behind AnchorNavLoc along the breadcrumb spine. */
	FVector SampleBreadcrumbTrail(
		const FVector& AnchorNavLoc,
		float DistanceAlongTrail,
		const FVector& FallbackDir) const;

	bool IsSlotWellSeparated(
		const FVector& Candidate,
		const FVector& AnchorNavLoc,
		const TArray<FVector, TInlineAllocator<16>>& AcceptedSlots,
		float MinSlotSepSq,
		float MinPlayerDistSq) const;

	TArray<FVector> Breadcrumbs; // Index 0 = oldest, Last() = newest
	EMinionFormationMode CurrentMode = EMinionFormationMode::Crescent;
	FVector CachedTrailBackDir = -FVector::ForwardVector;
};