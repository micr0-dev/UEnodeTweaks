#pragma once

#include "BlueprintConnectionDrawingPolicy.h"
#include "Math/Vector2D.h"
#include "Math/IntPoint.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Layout/Geometry.h"

class FSlateRect;
class FSlateWindowElementList;
class UEdGraph;

/**
 * Custom Blueprint wire drawing policy supporting orthogonal (A*) routing and/or wire bridges.
 * Activated by FOrthogonalConnectionFactory when either feature is enabled in settings.
 */
class FOrthogonalKismetConnectionDrawingPolicy : public FKismetConnectionDrawingPolicy
{
public:
    FOrthogonalKismetConnectionDrawingPolicy(
        int32 InBackLayerID, int32 InFrontLayerID,
        float ZoomFactor, const FSlateRect& InClippingRect,
        FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

    // FConnectionDrawingPolicy interface
    virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override;
    virtual void DrawConnection(int32 LayerId, const FVector2D& Start, const FVector2D& End, const FConnectionParams& Params) override;

private:
    // ---- Per-frame state -------------------------------------------------------

    /** Node bounding boxes in Slate draw-space, built once per frame in Draw(). */
    TArray<FBox2D> NodeObstacles;

    /**
     * Directional occupancy map for A* routing.
     * Key = grid cell. Value = bitmask of kDirs[] indices that already have a wire
     * passing through this cell in that axis (bits 0–3 = right/left/down/up).
     * Same-axis overlap is blocked; perpendicular crossings are allowed.
     */
    TMap<FIntPoint, uint8> OccupiedDirs;

    /** Polyline representations of all wires drawn so far this frame (bridge crossing detection). */
    TArray<TArray<FVector2D>> AllPaths;

    // ---- Orthogonal routing ----------------------------------------------------

    /** Run A* in draw-space and return smoothed waypoints. */
    TArray<FVector2D> FindOrthogonalPath(const FVector2D& Start, const FVector2D& End) const;

    /** L/Z-shape fallback used when A* hits its iteration cap. */
    TArray<FVector2D> ComputeFallbackPath(const FVector2D& Start, const FVector2D& End) const;

    /** Mark grid cells covered by the given polyline as occupied. */
    void MarkPathOccupied(const TArray<FVector2D>& Path, float Grid);

    // ---- Drawing helpers -------------------------------------------------------

    /**
     * Draw a polyline, inserting bridge arcs at every crossing with AllPaths.
     * Used when bWireBridges is on (orthogonal or Bezier mode).
     */
    void DrawPathWithBridges(int32 LayerId, const TArray<FVector2D>& Path, const FConnectionParams& Params) const;

    /**
     * Draw a plain polyline with segment-based hover detection.
     * Used when bWireBridges is off and bOrthogonalWires is on.
     */
    void DrawPolyline(int32 LayerId, const TArray<FVector2D>& Path, const FConnectionParams& Params);

    /** Draw one straight wire segment and update hover detection. */
    void DrawSegment(int32 LayerId, const FVector2D& A, const FVector2D& B, const FConnectionParams& Params);

    // ---- Bezier sampling -------------------------------------------------------

    /** Sample the Bezier spline implied by Start/End/Params into a polyline. */
    TArray<FVector2D> SampleBezierAsPolyline(const FVector2D& Start, const FVector2D& End,
                                              const FConnectionParams& Params) const;

    // ---- Crossing detection ----------------------------------------------------

    /** Returns true if segments [A,B] and [C,D] cross; sets OutT (param on AB) and OutPt. */
    static bool SegmentsIntersect(const FVector2D& A, const FVector2D& B,
                                   const FVector2D& C, const FVector2D& D,
                                   float& OutT, FVector2D& OutPt);
};
