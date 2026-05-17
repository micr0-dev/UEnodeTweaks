#pragma once

#include "Engine/DeveloperSettings.h"
#include "NodeTweaksSettings.generated.h"

UCLASS(config=EditorPerProjectUserSettings, defaultconfig, meta=(DisplayName="UE Node Tweaks"))
class UNodeTweaksSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UNodeTweaksSettings();

    /** Replace Bezier curves with 90-degree orthogonal wire routing in Blueprint graphs. */
    UPROPERTY(EditAnywhere, config, Category="Wire Routing")
    bool bOrthogonalWires;

    /** Grid cell size in screen pixels used by the A* router. Smaller = more precise paths, higher CPU cost. */
    UPROPERTY(EditAnywhere, config, Category="Wire Routing", meta=(ClampMin="8.0", ClampMax="64.0"))
    float GridSize;

    /** Extra cost added each time the route changes direction. Higher values prefer fewer bends. */
    UPROPERTY(EditAnywhere, config, Category="Wire Routing", meta=(ClampMin="0.0"))
    float TurnPenalty;

    /** Extra cost when a wire segment overlaps an already-routed wire. Higher values prefer non-overlapping routes. */
    UPROPERTY(EditAnywhere, config, Category="Wire Routing", meta=(ClampMin="0.0"))
    float CrossingPenalty;

    /** Draw a small arc where wires cross, like a bridge in a circuit diagram. Works in both Bezier and orthogonal mode. */
    UPROPERTY(EditAnywhere, config, Category="Wire Bridges")
    bool bWireBridges;

    /** Radius of the bridge arc in screen pixels. */
    UPROPERTY(EditAnywhere, config, Category="Wire Bridges", meta=(ClampMin="2.0", ClampMax="24.0", EditCondition="bWireBridges"))
    float BridgeRadius;

    // UDeveloperSettings interface
    virtual FName GetCategoryName() const override { return FName("Plugins"); }
};
