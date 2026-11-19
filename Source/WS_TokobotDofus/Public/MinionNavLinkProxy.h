// MinionNavLinkProxy.h
#pragma once

#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "NavLinkCustomInterface.h"
#include "MinionNavLinkProxy.generated.h"

/**
 * Smart NavLink Proxy with 3D draggable viewport widgets that launches minions across gaps or ledges.
 */
UCLASS()
class WS_TOKOBOTDOFUS_API AMinionNavLinkProxy : public ANavLinkProxy
{
	GENERATED_BODY()

public:
	AMinionNavLinkProxy();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	/** Draggable 3D viewport gizmo for the Smart Link start point (local space). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink", meta = (MakeEditWidget = "true"))
	FVector LinkStartPoint = FVector(-120.0f, 0.0f, 0.0f);

	/** Draggable 3D viewport gizmo for the Smart Link end point (local space). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink", meta = (MakeEditWidget = "true"))
	FVector LinkEndPoint = FVector(120.0f, 0.0f, 0.0f);

	/** Whether minions can jump both ways or only from Start to End (LeftToRight). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink")
	TEnumAsByte<ENavLinkDirection::Type> LinkDirection = ENavLinkDirection::BothWays;

	/** Arc height parameter (0.1 = flat leap, 0.5 = standard 45-degree arc, 0.85 = high lob). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float JumpArcHeight = 0.5f;

	/** Upward Z offset added to the destination point		so minions clear ledge lips cleanly. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink")
	float LandingHeightPadding = 35.0f;

	/** Radius around the link endpoints where approaching minions trigger the jump. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink")
	float TriggerRadius = 55.0f;

	/** Draws the launch trajectory in Play mode when a minion jumps. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "MinionNavLink|Debug")
	bool bDrawDebugJumpArc = true;

private:
	UFUNCTION()
	void HandleSmartLinkReached(AActor* MovingActor, const FVector& DestinationPoint);
};