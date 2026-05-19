#include "OrthogonalConnectionDrawingPolicy.h"
#include "NodeTweaksSettings.h"
#include "HoverHighlightPreprocessor.h"

#include "Rendering/DrawElements.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/Reverse.h"

// ---------------------------------------------------------------------------
// Routing constants
// ---------------------------------------------------------------------------

// Grid cell size (screen pixels) is computed per-frame as GridSize * ZoomFactor
// so paths are zoom-independent (same graph-unit grid regardless of zoom level).
static constexpr float kTurnPenalty    = 999999.0f; // per-turn cost — high enough to always prefer fewest-turn (L/Z) paths
static constexpr float kParallelPad    = 10.0f;   // cost for running adjacent to a parallel wire
static constexpr float kObstacleCost   = 1000.0f;
static constexpr float kOverlapCost    = 700.0f;  // cross-node overlap only; same-node wires are exempt
static constexpr int32 kMaxIter        = 4000;

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
    OccupiedNodes.Reset();
    AllPaths.Reset();

    const float Padding = 8.0f;
    for (int32 i = 0; i < ArrangedNodes.Num(); ++i)
    {
        const FArrangedWidget& AW = ArrangedNodes[i];
        const FVector2D Pos  = FVector2D(AW.Geometry.AbsolutePosition);
        const FVector2D Size = AW.Geometry.GetDrawSize();
        NodeObstacles.Add(FBox2D(Pos - FVector2D(Padding), Pos + Size + FVector2D(Padding)));
    }

    const UNodeTweaksSettings* S = GetDefault<UNodeTweaksSettings>();
    if (S->bWireBridges)
    {
        // Phase 1: collect all paths (no drawing) so AllPaths is fully known before any arc is drawn.
        // This makes bridges deterministic: every H/V crossing gets exactly one bridge regardless of draw order.
        CollectedPaths.Reset();
        bCollectMode = true;
        FKismetConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
        bCollectMode = false;

        AllPaths = CollectedPaths;
        PhaseDrawIndex = 0;

        // Phase 2: draw everything — AllPaths is complete so every horizontal segment sees all crossings.
        FKismetConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
    }
    else
    {
        FKismetConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
    }
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

    // Phase 2 (bridge draw pass): use the pre-collected path — no re-routing needed.
    // Only consume a collected path if one exists; otherwise fall through (handles drag preview wires
    // which call DrawConnection directly after the two-pass cycle has finished).
    if (!bCollectMode && S->bWireBridges && PhaseDrawIndex < CollectedPaths.Num())
    {
        DrawPathWithBridges(LayerId, CollectedPaths[PhaseDrawIndex++], LocalParams);
        return;
    }

    // Compute path (used in collection phase and single-pass non-bridge mode).
    TArray<FVector2D> Path;

    if (S->bOrthogonalWires)
    {
        const float Grid    = S->GridSize * ZoomFactor;
        const float StubLen = Grid * 2.0f;
        const FVector2D StubOut = Start + FVector2D(StubLen, 0.0f);
        const FVector2D StubIn  = End   - FVector2D(StubLen, 0.0f);

        // Extract endpoint nodes for shared-node overlap exemption
        UEdGraphNode* NodeA = Params.AssociatedPin1 ? Params.AssociatedPin1->GetOwningNodeUnchecked() : nullptr;
        UEdGraphNode* NodeB = Params.AssociatedPin2 ? Params.AssociatedPin2->GetOwningNodeUnchecked() : nullptr;

        TArray<FVector2D> W = FindOrthogonalPath(StubOut, StubIn, Grid, NodeA, NodeB);

        if (W.Num() >= 1) W[0]     = StubOut;
        if (W.Num() >= 2) W.Last() = StubIn;

        if (W.Num() >= 3)
        {
            W[1].Y = StubOut.Y;
            W[W.Num() - 2].Y = StubIn.Y;
            if (W.Num() == 3)
                W[1].X = StubIn.X;
        }

        Path.Add(Start);
        for (const FVector2D& P : W)
            Path.Add(P);
        Path.Add(End);

        MarkPathOccupied(Path, Grid, NodeA, NodeB);
    }
    else
    {
        Path = SampleBezierAsPolyline(Start, End, LocalParams);
    }

    if (bCollectMode)
    {
        // Phase 1: store path for bridge pass, don't draw.
        CollectedPaths.Add(Path);
        return;
    }

    // Single-pass (bridges off): draw immediately.
    DrawPolyline(LayerId, Path, LocalParams);
}

// ---------------------------------------------------------------------------
// DrawPolyline – plain segments with hover detection
// ---------------------------------------------------------------------------

void FOrthogonalKismetConnectionDrawingPolicy::DrawPolyline(
    int32 LayerId, const TArray<FVector2D>& Path, const FConnectionParams& Params)
{
    const float CR = GetDefault<UNodeTweaksSettings>()->CornerRadius * ZoomFactor;
    if (Path.Num() < 2) return;

    FVector2D Cursor = Path[0];

    for (int32 i = 1; i < Path.Num(); ++i)
    {
        const FVector2D  B     = Path[i];
        const bool       bLast = (i == Path.Num() - 1);
        const FVector2D  DirIn = (B - Cursor).GetSafeNormal();

        if (!bLast && CR > 0.5f)
        {
            const FVector2D DirOut = (Path[i + 1] - B).GetSafeNormal();
            if (!DirIn.Equals(DirOut, 0.01f))
            {
                const float MaxR = FMath::Min((B - Cursor).Size() * 0.5f,
                                              (Path[i + 1] - B).Size() * 0.5f);
                const float R = FMath::Min(CR, MaxR);
                if (R > 0.5f)
                {
                    const FVector2D ArcStart = B - DirIn  * R;
                    const FVector2D ArcEnd   = B + DirOut * R;
                    DrawSegment(LayerId, Cursor, ArcStart, Params);
                    constexpr float k = 0.5523f; // optimal quarter-circle Bezier tangent ratio
                    FSlateDrawElement::MakeDrawSpaceSpline(
                        DrawElementsList, LayerId,
                        ArcStart, DirIn  * (k * R),
                        ArcEnd,   DirOut * (k * R),
                        Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);
                    Cursor = ArcEnd;
                    continue;
                }
            }
        }

        DrawSegment(LayerId, Cursor, B, Params);
        Cursor = B;
    }
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
    const float R_bridge = S->BridgeRadius * ZoomFactor;
    const float R_corner = S->CornerRadius * ZoomFactor;
    auto* MutableThis = const_cast<FOrthogonalKismetConnectionDrawingPolicy*>(this);

    if (Path.Num() < 2) return;

    // SegStart tracks the actual draw-start of each segment (post-corner-arc).
    FVector2D SegStart = Path[0];

    for (int32 si = 0; si < Path.Num() - 1; ++si)
    {
        const FVector2D& B     = Path[si + 1];
        const bool       bLast = (si == Path.Num() - 2);
        const FVector2D  Dir   = (B - SegStart).GetSafeNormal();

        // Compute corner rounding at junction B (skip for last segment).
        float     CR      = 0.f;
        FVector2D DirNext;
        if (!bLast && R_corner > 0.5f)
        {
            DirNext = (Path[si + 2] - B).GetSafeNormal();
            if (!Dir.Equals(DirNext, 0.01f))
            {
                const float MaxR = FMath::Min((B - SegStart).Size() * 0.5f,
                                              (Path[si + 2] - B).Size() * 0.5f);
                CR = FMath::Min(R_corner, MaxR);
            }
        }

        // Segment endpoint pulled back to leave room for the corner arc.
        const FVector2D SegEnd = (CR > 0.5f) ? B - Dir * CR : B;

        // ---- Draw [SegStart, SegEnd]: vertical passes through, horizontal gets bridges ----
        if (FMath::Abs(Dir.X) <= FMath::Abs(Dir.Y))
        {
            // Vertical: no bridge arc
            MutableThis->DrawSegment(LayerId, SegStart, SegEnd, Params);
        }
        else
        {
            // Horizontal: collect crossings and draw bridge arcs
            const FVector2D Normal = FVector2D(Dir.Y, -Dir.X); // up in screen space

            TArray<TPair<float, FVector2D>> Crossings;
            for (const TArray<FVector2D>& PrevPath : AllPaths)
                for (int32 pi = 0; pi < PrevPath.Num() - 1; ++pi)
                {
                    float T; FVector2D Pt;
                    if (SegmentsIntersect(SegStart, SegEnd, PrevPath[pi], PrevPath[pi + 1], T, Pt))
                        Crossings.Add({ FVector2D::DotProduct(Pt - SegStart, Dir), Pt });
                }

            Crossings.Sort([](const TPair<float,FVector2D>& X, const TPair<float,FVector2D>& Y)
                { return X.Key < Y.Key; });

            FVector2D DrawCursor = SegStart;
            for (const auto& Cross : Crossings)
            {
                const FVector2D& Pt      = Cross.Value;
                const FVector2D ArcEntry = Pt - Dir * R_bridge;
                const FVector2D ArcExit  = Pt + Dir * R_bridge;

                if (FVector2D::DotProduct(ArcExit - DrawCursor, Dir) <= 0.f) continue;
                if (FVector2D::DotProduct(Pt - SegEnd, Dir)          >= 0.f) continue;

                const FVector2D DrawTo = FVector2D::DotProduct(ArcEntry - DrawCursor, Dir) > 0.f
                    ? ArcEntry : DrawCursor;
                MutableThis->DrawSegment(LayerId, DrawCursor, DrawTo, Params);

                const FVector2D Top = Pt + Normal * R_bridge;
                const float     TM  = (4.f / 3.f) * R_bridge;
                FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, LayerId,
                    ArcEntry, Normal * TM, Top,     Dir     * TM,
                    Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);
                FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, LayerId,
                    Top,      Dir    * TM, ArcExit, -Normal * TM,
                    Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);

                DrawCursor = ArcExit;
            }
            MutableThis->DrawSegment(LayerId, DrawCursor, SegEnd, Params);
        }

        // ---- Corner arc at junction B ----
        if (CR > 0.5f)
        {
            const FVector2D ArcEnd = B + DirNext * CR;
            constexpr float k = 0.5523f;
            FSlateDrawElement::MakeDrawSpaceSpline(
                DrawElementsList, LayerId,
                SegEnd, Dir    * (k * CR),
                ArcEnd, DirNext * (k * CR),
                Params.WireThickness, ESlateDrawEffect::None, Params.WireColor);
            SegStart = ArcEnd;
        }
        else
        {
            SegStart = B;
        }
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
    const FVector2D& Start, const FVector2D& End, float Grid,
    UEdGraphNode* NodeA, UEdGraphNode* NodeB) const
{
    const FIntPoint StartCell = SnapToGrid(Start, Grid);
    const FIntPoint EndCell   = SnapToGrid(End,   Grid);

    if (StartCell == EndCell)
        return { GridToWorld(StartCell, Grid) };

    TMap<FIntPoint, FAStarCell> CellMap;
    CellMap.Reserve(1024);
    TArray<FAStarHeapItem> OpenHeap;
    OpenHeap.Reserve(512);

    {
        FAStarCell& S0 = CellMap.FindOrAdd(StartCell);
        S0.G   = 0.0f;
        S0.F   = ManhattanDist(StartCell, EndCell);
        // Seed direction toward the target so the path always exits in the dominant axis first.
        // Combined with the huge kTurnPenalty this guarantees L/Z shapes (never V-H-V etc.).
        {
            const float dx = End.X - Start.X;
            const float dy = End.Y - Start.Y;
            if (FMath::Abs(dx) >= FMath::Abs(dy))
                S0.Dir = (dx >= 0.f) ? 0 : 1; // right or left
            else
                S0.Dir = (dy >= 0.f) ? 2 : 3; // down or up
        }
        OpenHeap.HeapPush(FAStarHeapItem{ S0.F, StartCell });
    }

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
            if (CurDir != -1 && d != CurDir) Cost += kTurnPenalty;

            // Node obstacle avoidance
            const FVector2D WP = GridToWorld(Neighbor, Grid);
            for (const FBox2D& Box : NodeObstacles)
                if (Box.IsInside(WP)) { Cost += kObstacleCost; break; }

            // Same-axis overlap: penalize only if the occupying wire shares no node with this wire.
            const uint8 SameAxisMask = (1 << d) | (1 << (d ^ 1));
            if (const uint8* CellDirs = OccupiedDirs.Find(Neighbor))
            {
                if (*CellDirs & SameAxisMask)
                {
                    bool bSharedNode = false;
                    if (NodeA || NodeB)
                    {
                        if (const TPair<UEdGraphNode*, UEdGraphNode*>* Occ = OccupiedNodes.Find(Neighbor))
                            bSharedNode = (Occ->Key   == NodeA || Occ->Key   == NodeB ||
                                           Occ->Value == NodeA || Occ->Value == NodeB);
                    }
                    if (!bSharedNode)
                        Cost += kOverlapCost;
                }
            }

            // Parallel padding: discourage cells immediately adjacent to a parallel wire
            const bool bHoriz = (d < 2);
            const FIntPoint PerpA = Neighbor + (bHoriz ? FIntPoint(0,  1) : FIntPoint( 1, 0));
            const FIntPoint PerpB = Neighbor + (bHoriz ? FIntPoint(0, -1) : FIntPoint(-1, 0));
            if (const uint8* DA = OccupiedDirs.Find(PerpA)) if (*DA & SameAxisMask) Cost += kParallelPad;
            if (const uint8* DB = OccupiedDirs.Find(PerpB)) if (*DB & SameAxisMask) Cost += kParallelPad;

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
    const TArray<FVector2D>& Path, float Grid, UEdGraphNode* NodeA, UEdGraphNode* NodeB)
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

        const TPair<UEdGraphNode*, UEdGraphNode*> NodePair(NodeA, NodeB);
        FIntPoint Cur = A;
        while (Cur != B)
        {
            OccupiedDirs.FindOrAdd(Cur) |= AxisMask;
            OccupiedNodes.FindOrAdd(Cur) = NodePair;
            Cur.X += Delta.X;
            Cur.Y += Delta.Y;
        }
        OccupiedDirs.FindOrAdd(B) |= AxisMask;
        OccupiedNodes.FindOrAdd(B) = NodePair;
    }
}
