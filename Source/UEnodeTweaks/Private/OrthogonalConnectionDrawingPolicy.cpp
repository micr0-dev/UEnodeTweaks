#include "OrthogonalConnectionDrawingPolicy.h"
#include "NodeTweaksSettings.h"
#include "HoverHighlightPreprocessor.h"

#include "Rendering/DrawElements.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/Reverse.h"

// ---------------------------------------------------------------------------
// Grid helpers
// ---------------------------------------------------------------------------

static FORCEINLINE FIntPoint SnapToGrid(const FVector2D& Pos, float Grid)
{
    return FIntPoint(
        FMath::RoundToInt(Pos.X / Grid),
        FMath::RoundToInt(Pos.Y / Grid));
}

static FORCEINLINE FVector2D GridToWorld(const FIntPoint& Cell, float Grid)
{
    return FVector2D(Cell.X * Grid, Cell.Y * Grid);
}

static FORCEINLINE float ManhattanDist(const FIntPoint& A, const FIntPoint& B)
{
    return (float)(FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y));
}

// 4-directional movement: Right, Left, Down, Up
static const FIntPoint kDirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

// ---------------------------------------------------------------------------
// A* node state
// ---------------------------------------------------------------------------

struct FAStarCell
{
    float     G       = FLT_MAX;
    float     F       = FLT_MAX;
    FIntPoint Parent  = FIntPoint(MIN_int32, MIN_int32);
    int8      Dir     = -1;
    bool      bClosed = false;
};

struct FAStarHeapItem
{
    float     F;
    FIntPoint Cell;
    // TArray heap is a max-heap; negate comparison to get min-heap behaviour
    bool operator<(const FAStarHeapItem& O) const { return F > O.F; }
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

FOrthogonalKismetConnectionDrawingPolicy::FOrthogonalKismetConnectionDrawingPolicy(
    int32 InBackLayerID, int32 InFrontLayerID,
    float ZoomFactor, const FSlateRect& InClippingRect,
    FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
    : FKismetConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj)
{
}

// ---------------------------------------------------------------------------
// Draw – build obstacle map then let the base drive DrawConnection calls
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::Draw(
    TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries,
    FArrangedChildren& ArrangedNodes)
{
    NodeObstacles.Reset();
    OccupiedDirs.Reset();
    AllPaths.Reset();

    const float Padding = 8.0f;
    for (int32 i = 0; i < ArrangedNodes.Num(); ++i)
    {
        const FArrangedWidget& AW = ArrangedNodes[i];
        const FVector2D Pos  = FVector2D(AW.Geometry.AbsolutePosition);
        const FVector2D Size = AW.Geometry.GetDrawSize();
        NodeObstacles.Add(FBox2D(Pos - FVector2D(Padding), Pos + Size + FVector2D(Padding)));
    }

    FKismetConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

// ---------------------------------------------------------------------------
// DrawConnection – unified entry point for both modes
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::DrawConnection(
    int32 LayerId, const FVector2D& Start, const FVector2D& End, const FConnectionParams& Params)
{
    // During hover highlight: dim wires that don't connect two highlighted nodes
    FConnectionParams LocalParams = Params;
    if (FHoverHighlightPreprocessor::IsHighlightActive() && Params.AssociatedPin1 && Params.AssociatedPin2)
    {
        UEdGraphNode* N1 = Params.AssociatedPin1->GetOwningNodeUnchecked();
        UEdGraphNode* N2 = Params.AssociatedPin2->GetOwningNodeUnchecked();
        const TSet<UEdGraphNode*>& Highlighted = FHoverHighlightPreprocessor::GetHighlightedNodes();
        if (!Highlighted.Contains(N1) || !Highlighted.Contains(N2))
        {
            const float Dim = FHoverHighlightPreprocessor::GetCurrentDimOpacity();
            LocalParams.WireColor = FLinearColor(Params.WireColor.R * Dim,
                                                  Params.WireColor.G * Dim,
                                                  Params.WireColor.B * Dim,
                                                  Params.WireColor.A * Dim);
        }
    }

    const UNodeTweaksSettings* S = GetDefault<UNodeTweaksSettings>();

    TArray<FVector2D> Path;

    if (S->bOrthogonalWires)
    {
        const float StubLen = S->GridSize * 2.0f;
        const FVector2D StubOut = Start + FVector2D(StubLen, 0.0f);
        const FVector2D StubIn  = End   - FVector2D(StubLen, 0.0f);

        Path.Add(Start);
        Path.Add(StubOut);
        for (const FVector2D& P : FindOrthogonalPath(StubOut, StubIn))
            Path.Add(P);
        Path.Add(StubIn);
        Path.Add(End);

        MarkPathOccupied(Path, S->GridSize);
    }
    else
    {
        // Bezier mode: sample spline into a polyline for bridge detection / drawing
        Path = SampleBezierAsPolyline(Start, End, LocalParams);
    }

    if (S->bWireBridges)
        DrawPathWithBridges(LayerId, Path, LocalParams);
    else
        DrawPolyline(LayerId, Path, LocalParams);

    AllPaths.Add(Path);
}

// ---------------------------------------------------------------------------
// DrawPolyline – plain segments with hover detection
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::DrawPolyline(
    int32 LayerId, const TArray<FVector2D>& Path, const FConnectionParams& Params)
{
    for (int32 i = 0; i < Path.Num() - 1; ++i)
        DrawSegment(LayerId, Path[i], Path[i + 1], Params);
}

// ---------------------------------------------------------------------------
// DrawSegment – one straight wire segment + hover update
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::DrawSegment(
    int32 LayerId, const FVector2D& A, const FVector2D& B, const FConnectionParams& Params)
{
    if ((B - A).IsNearlyZero())
        return;

    const FVector2D Tangent = B - A; // straight cubic Bezier when both tangents = direction

    FSlateDrawElement::MakeDrawSpaceSpline(
        DrawElementsList, LayerId,
        A, Tangent, B, Tangent,
        Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);

    if (Settings->bTreatSplinesLikePins)
    {
        const float HoverRadiusSq = FMath::Square(Params.WireThickness * 0.5f + Settings->SplineHoverTolerance);
        const FVector2D Closest   = FMath::ClosestPointOnSegment2D(LocalMousePosition, A, B);
        const float DistSq        = (LocalMousePosition - Closest).SizeSquared();

        if (DistSq < HoverRadiusSq && DistSq < SplineOverlapResult.GetDistanceSquared())
        {
            SplineOverlapResult = FGraphSplineOverlapResult(
                Params.AssociatedPin1, Params.AssociatedPin2,
                DistSq,
                (A - Closest).SizeSquared(),
                (B - Closest).SizeSquared(),
                true);
        }
    }
}

// ---------------------------------------------------------------------------
// DrawPathWithBridges – detect crossings with AllPaths, draw arcs over them
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::DrawPathWithBridges(
    int32 LayerId, const TArray<FVector2D>& Path, const FConnectionParams& Params) const
{
    const UNodeTweaksSettings* S = GetDefault<UNodeTweaksSettings>();
    const float R = S->BridgeRadius * ZoomFactor;

    auto* MutableThis = const_cast<FOrthogonalKismetConnectionDrawingPolicy*>(this);

    for (int32 si = 0; si < Path.Num() - 1; ++si)
    {
        const FVector2D& A = Path[si];
        const FVector2D& B = Path[si + 1];

        const FVector2D Dir    = (B - A).GetSafeNormal();
        const FVector2D Normal = FVector2D(-Dir.Y, Dir.X); // 90° CCW = "up" for rightward wires

        // Collect crossings: stored as (distance_from_A_along_Dir, crossing_point)
        // Using distance instead of T avoids divide-by-SegLen and segment-boundary issues.
        TArray<TPair<float, FVector2D>> Crossings;

        for (const TArray<FVector2D>& PrevPath : AllPaths)
        {
            for (int32 pi = 0; pi < PrevPath.Num() - 1; ++pi)
            {
                float T; FVector2D Pt;
                if (SegmentsIntersect(A, B, PrevPath[pi], PrevPath[pi + 1], T, Pt))
                {
                    const float Dist = FVector2D::DotProduct(Pt - A, Dir);
                    Crossings.Add({ Dist, Pt });
                }
            }
        }

        Crossings.Sort([](const TPair<float,FVector2D>& X, const TPair<float,FVector2D>& Y)
        {
            return X.Key < Y.Key;
        });

        FVector2D Cursor = A;

        for (const TPair<float, FVector2D>& Cross : Crossings)
        {
            const FVector2D& CrossPt = Cross.Value;

            // Arc footprint along the segment
            const FVector2D ArcEntry = CrossPt - Dir * R;
            const FVector2D ArcExit  = CrossPt + Dir * R;

            // Skip if this crossing's exit is already behind the draw cursor
            if (FVector2D::DotProduct(ArcExit - Cursor, Dir) <= 0.0f)
                continue;
            // Skip if crossing center is at or past the segment end
            if (FVector2D::DotProduct(CrossPt - B, Dir) >= 0.0f)
                continue;

            // Straight line up to arc entry, but not backwards
            const FVector2D DrawTo = FVector2D::DotProduct(ArcEntry - Cursor, Dir) > 0.0f
                ? ArcEntry : Cursor;
            MutableThis->DrawSegment(LayerId, Cursor, DrawTo, Params);

            // True semicircle via two quarter-circle Beziers joined at the apex.
            //
            //   ArcEntry ──Q1──► Top ──Q2──► ArcExit
            //
            //   Q1: T0 = Normal*(4R/3)   T1 = Dir*(4R/3)
            //   Q2: T0 = Dir*(4R/3)      T1 = -Normal*(4R/3)
            //
            // Tangent magnitude (4/3)*R is the optimal quarter-circle Bezier approximation
            // (<0.1% radial error). Together the two segments form a full semicircle.
            const FVector2D Top = CrossPt + Normal * R;
            const float     TM  = (4.0f / 3.0f) * R;

            FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, LayerId,
                ArcEntry, Normal * TM, Top,     Dir    * TM,
                Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);

            FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, LayerId,
                Top,      Dir    * TM, ArcExit, -Normal * TM,
                Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);

            Cursor = ArcExit;
        }

        // Remaining wire after last bridge (or entire segment if no crossings)
        MutableThis->DrawSegment(LayerId, Cursor, B, Params);
    }
}

// ---------------------------------------------------------------------------
// SegmentsIntersect
// ---------------------------------------------------------------------------

bool FOrthogonalKismetConnectionDrawingPolicy::SegmentsIntersect(
    const FVector2D& A, const FVector2D& B,
    const FVector2D& C, const FVector2D& D,
    float& OutT, FVector2D& OutPt)
{
    const FVector2D R = B - A;
    const FVector2D S = D - C;
    const float     RxS = R.X * S.Y - R.Y * S.X;

    if (FMath::Abs(RxS) < 1.0f) // parallel / collinear – ignore
        return false;

    const FVector2D AC = C - A;
    const float T = (AC.X * S.Y - AC.Y * S.X) / RxS;
    const float U = (AC.X * R.Y - AC.Y * R.X) / RxS;

    // Tiny inset to ignore degenerate endpoint-touches between adjacent sampled segments
    constexpr float kMargin = 0.005f;
    if (T > kMargin && T < 1.0f - kMargin && U > kMargin && U < 1.0f - kMargin)
    {
        OutT  = T;
        OutPt = A + T * R;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SampleBezierAsPolyline
// ---------------------------------------------------------------------------

TArray<FVector2D> FOrthogonalKismetConnectionDrawingPolicy::SampleBezierAsPolyline(
    const FVector2D& Start, const FVector2D& End, const FConnectionParams& Params) const
{
    const FVector2D SplineTangent = ComputeSplineTangent(Start, End);
    const FVector2D T0 = Params.StartTangent.IsNearlyZero()
        ? ((Params.StartDirection == EGPD_Output) ? SplineTangent : -SplineTangent)
        : Params.StartTangent;
    const FVector2D T1 = Params.EndTangent.IsNearlyZero()
        ? ((Params.EndDirection == EGPD_Input) ? SplineTangent : -SplineTangent)
        : Params.EndTangent;

    constexpr int32 kSamples = 32;
    TArray<FVector2D> Points;
    Points.Reserve(kSamples + 1);
    for (int32 i = 0; i <= kSamples; ++i)
    {
        const float t = (float)i / (float)kSamples;
        Points.Add(FMath::CubicInterp(Start, T0, End, T1, t));
    }
    return Points;
}

// ---------------------------------------------------------------------------
// FindOrthogonalPath – A* on a hash-map grid
// ---------------------------------------------------------------------------

TArray<FVector2D> FOrthogonalKismetConnectionDrawingPolicy::FindOrthogonalPath(
    const FVector2D& Start, const FVector2D& End) const
{
    const UNodeTweaksSettings* S = GetDefault<UNodeTweaksSettings>();
    const float Grid = S->GridSize;

    const FIntPoint StartCell = SnapToGrid(Start, Grid);
    const FIntPoint EndCell   = SnapToGrid(End,   Grid);

    if (StartCell == EndCell)
        return { GridToWorld(StartCell, Grid) };

    TMap<FIntPoint, FAStarCell> CellMap;
    CellMap.Reserve(512);
    TArray<FAStarHeapItem> OpenHeap;
    OpenHeap.Reserve(256);

    {
        FAStarCell& S0 = CellMap.FindOrAdd(StartCell);
        S0.G = 0.0f;
        S0.F = ManhattanDist(StartCell, EndCell);
        S0.Dir = -1;
        OpenHeap.HeapPush(FAStarHeapItem{ S0.F, StartCell });
    }

    constexpr int32 kMaxIter    = 500;
    constexpr float kObstacleCost = 1000.0f;

    int32 Iter   = 0;
    bool  bFound = false;

    while (OpenHeap.Num() > 0 && Iter < kMaxIter)
    {
        ++Iter;
        FAStarHeapItem Top;
        OpenHeap.HeapPop(Top);

        FAStarCell* CurData = CellMap.Find(Top.Cell);
        if (!CurData || CurData->bClosed) continue;
        CurData->bClosed = true;

        if (Top.Cell == EndCell) { bFound = true; break; }

        const float CurG   = CurData->G;
        const int8  CurDir = CurData->Dir;

        for (int8 d = 0; d < 4; ++d)
        {
            const FIntPoint Neighbor = Top.Cell + kDirs[d];
            FAStarCell& NData = CellMap.FindOrAdd(Neighbor);
            if (NData.bClosed) continue;

            float Cost = 1.0f;
            if (CurDir != -1 && d != CurDir) Cost += S->TurnPenalty;

            const FVector2D WP = GridToWorld(Neighbor, Grid);
            for (const FBox2D& Box : NodeObstacles)
                if (Box.IsInside(WP)) { Cost += kObstacleCost; break; }

            if (const uint8* CellDirs = OccupiedDirs.Find(Neighbor))
            {
                // Bits for the same axis (e.g. right & left share axis 0; down & up share axis 1)
                const uint8 SameAxisMask = (1 << d) | (1 << (d ^ 1));
                if (*CellDirs & SameAxisMask)
                    Cost += 10000.0f; // same-direction overlap: effectively impassable
                else
                    Cost += S->CrossingPenalty; // perpendicular crossing: soft preference
            }

            const float NewG = CurG + Cost;
            if (NewG < NData.G)
            {
                NData.G      = NewG;
                NData.F      = NewG + ManhattanDist(Neighbor, EndCell);
                NData.Parent = Top.Cell;
                NData.Dir    = d;
                OpenHeap.HeapPush(FAStarHeapItem{ NData.F, Neighbor });
            }
        }
    }

    if (!bFound) return ComputeFallbackPath(GridToWorld(StartCell, Grid), GridToWorld(EndCell, Grid));

    // Reconstruct
    TArray<FIntPoint> CellPath;
    {
        FIntPoint Cur = EndCell;
        while (Cur != StartCell)
        {
            CellPath.Add(Cur);
            const FAStarCell* D = CellMap.Find(Cur);
            if (!D || D->Parent == FIntPoint(MIN_int32, MIN_int32)) break;
            Cur = D->Parent;
        }
        CellPath.Add(StartCell);
        Algo::Reverse(CellPath);
    }

    // Smooth: only emit waypoints at direction changes
    TArray<FVector2D> Waypoints;
    Waypoints.Add(GridToWorld(CellPath[0], Grid));
    for (int32 i = 1; i < CellPath.Num() - 1; ++i)
    {
        if ((CellPath[i] - CellPath[i-1]) != (CellPath[i+1] - CellPath[i]))
            Waypoints.Add(GridToWorld(CellPath[i], Grid));
    }
    Waypoints.Add(GridToWorld(CellPath.Last(), Grid));
    return Waypoints;
}

// ---------------------------------------------------------------------------
// ComputeFallbackPath – simple midpoint S-shape
// ---------------------------------------------------------------------------

TArray<FVector2D> FOrthogonalKismetConnectionDrawingPolicy::ComputeFallbackPath(
    const FVector2D& Start, const FVector2D& End) const
{
    const float MidX = (Start.X + End.X) * 0.5f;
    return { Start, FVector2D(MidX, Start.Y), FVector2D(MidX, End.Y), End };
}

// ---------------------------------------------------------------------------
// MarkPathOccupied
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::MarkPathOccupied(
    const TArray<FVector2D>& Path, float Grid)
{
    for (int32 i = 0; i < Path.Num() - 1; ++i)
    {
        const FIntPoint A = SnapToGrid(Path[i],     Grid);
        const FIntPoint B = SnapToGrid(Path[i + 1], Grid);

        const FIntPoint Delta(FMath::Sign(B.X - A.X), FMath::Sign(B.Y - A.Y));

        // Identify which kDirs[] index this segment corresponds to
        int8 DirIdx = -1;
        for (int8 d = 0; d < 4; ++d)
            if (kDirs[d] == Delta) { DirIdx = d; break; }

        if (DirIdx < 0) continue; // zero-length or diagonal (shouldn't happen for orthogonal paths)

        // Mark both directions on this axis (right+left share axis, down+up share axis)
        const uint8 AxisMask = static_cast<uint8>((1 << DirIdx) | (1 << (DirIdx ^ 1)));

        FIntPoint Cur = A;
        while (Cur != B)
        {
            OccupiedDirs.FindOrAdd(Cur) |= AxisMask;
            Cur.X += Delta.X;
            Cur.Y += Delta.Y;
        }
        OccupiedDirs.FindOrAdd(B) |= AxisMask;
    }
}
