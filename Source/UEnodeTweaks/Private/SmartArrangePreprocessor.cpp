#include "SmartArrangePreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "ScopedTransaction.h"
#include "SGraphPanel.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"

// ---------------------------------------------------------------------------
// Protected-member accessor (same pattern as SGraphPinDragAccessor)
// ---------------------------------------------------------------------------

class SGraphPanelArrangeAccessor : public SGraphPanel
{
public:
    using SGraphPanel::GetBoundsForNode;
};

static FVector2D GetNodeSize(SGraphPanel* Panel, UEdGraphNode* Node)
{
    FVector2D Min, Max;
    if (static_cast<SGraphPanelArrangeAccessor*>(Panel)->GetBoundsForNode(Node, Min, Max))
        return Max - Min;
    // Fallback heuristic: base height + 24px per pin
    const float W = 200.f;
    const float H = 48.f + Node->Pins.Num() * 24.f;
    return FVector2D(W, H);
}

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static constexpr float kHGap = 80.f;   // horizontal gap between columns
static constexpr float kVGap = 20.f;   // vertical gap between nodes in a column

// ---------------------------------------------------------------------------
// Widget path helper
// ---------------------------------------------------------------------------

SGraphPanel* FSmartArrangePreprocessor::FindPanelInPath(const FWidgetPath& Path)
{
    static const FName PanelType(TEXT("SGraphPanel"));
    for (int32 i = 0; i < Path.Widgets.Num(); ++i)
    {
        if (Path.Widgets[i].Widget->GetType() == PanelType)
            return static_cast<SGraphPanel*>(&Path.Widgets[i].Widget.Get());
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Input processor
// ---------------------------------------------------------------------------

bool FSmartArrangePreprocessor::HandleMouseMoveEvent(FSlateApplication& SlateApp,
                                                      const FPointerEvent& InMouseEvent)
{
    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        InMouseEvent.GetScreenSpacePosition(),
        SlateApp.GetInteractiveTopLevelWindows());
    LastSeenPanel = FindPanelInPath(Path);
    return false; // never consume mouse-move
}

bool FSmartArrangePreprocessor::HandleKeyDownEvent(FSlateApplication& SlateApp,
                                                    const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() != EKeys::Q || InKeyEvent.IsRepeat())
        return false;
    if (InKeyEvent.IsControlDown() || InKeyEvent.IsAltDown() || InKeyEvent.IsShiftDown())
        return false;

    // Re-validate LastSeenPanel via cursor position in case focus moved
    {
        FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
            SlateApp.GetCursorPos(),
            SlateApp.GetInteractiveTopLevelWindows());
        SGraphPanel* Candidate = FindPanelInPath(Path);
        if (Candidate) LastSeenPanel = Candidate;
    }

    if (!LastSeenPanel) return false;

    // Only take over for K2 (Blueprint) graphs
    UEdGraph* Graph = LastSeenPanel->GetGraphObj();
    if (!Graph || !Graph->GetSchema()->IsA<UEdGraphSchema_K2>()) return false;

    return RunSmartArrange(LastSeenPanel);
}

// ---------------------------------------------------------------------------
// Core layout
// ---------------------------------------------------------------------------

bool FSmartArrangePreprocessor::RunSmartArrange(SGraphPanel* Panel) const
{
    // ---- 1. Gather selected nodes ----------------------------------------
    const FGraphPanelSelectionSet& SelectedSet = Panel->SelectionManager.GetSelectedNodes();
    TArray<UEdGraphNode*> Nodes;
    for (UObject* Obj : SelectedSet)
    {
        if (UEdGraphNode* N = Cast<UEdGraphNode>(Obj))
            Nodes.Add(N);
    }
    if (Nodes.Num() < 2) return false;

    // ---- 2. Build edge set (only between selected nodes) -----------------
    TSet<UEdGraphNode*> NodeSet(Nodes);

    // For each node: which selected nodes feed INTO it (predecessors)
    TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Predecessors;
    TMap<UEdGraphNode*, TArray<UEdGraphNode*>> Successors;
    for (UEdGraphNode* N : Nodes)
    {
        Predecessors.Add(N, {});
        Successors.Add(N, {});
    }

    bool bAnyEdge = false;
    for (UEdGraphNode* Src : Nodes)
    {
        for (UEdGraphPin* Pin : Src->Pins)
        {
            if (Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Dst = Linked ? Linked->GetOwningNode() : nullptr;
                if (Dst && NodeSet.Contains(Dst) && Dst != Src)
                {
                    Successors[Src].AddUnique(Dst);
                    Predecessors[Dst].AddUnique(Src);
                    bAnyEdge = true;
                }
            }
        }
    }

    if (!bAnyEdge) return false; // no internal connections — let default handle it

    // ---- 3. Column assignment (longest-path layering) --------------------
    // Process nodes in topological order using Kahn's algorithm.
    // Back-edges (cycles) are ignored to avoid infinite loops.

    TMap<UEdGraphNode*, int32> Column;
    for (UEdGraphNode* N : Nodes) Column.Add(N, 0);

    // Kahn-style: repeatedly relax until stable
    bool bChanged = true;
    int32 MaxIter = Nodes.Num() * Nodes.Num(); // safety bound
    while (bChanged && MaxIter-- > 0)
    {
        bChanged = false;
        for (UEdGraphNode* N : Nodes)
        {
            for (UEdGraphNode* Pred : Predecessors[N])
            {
                int32 NewCol = Column[Pred] + 1;
                if (NewCol > Column[N])
                {
                    Column[N] = NewCol;
                    bChanged = true;
                }
            }
        }
    }

    // ---- 4. Group nodes by column ----------------------------------------
    int32 MaxCol = 0;
    for (UEdGraphNode* N : Nodes) MaxCol = FMath::Max(MaxCol, Column[N]);

    TArray<TArray<UEdGraphNode*>> Columns;
    Columns.SetNum(MaxCol + 1);
    for (UEdGraphNode* N : Nodes) Columns[Column[N]].Add(N);

    // ---- 5. Get node sizes -----------------------------------------------
    TMap<UEdGraphNode*, FVector2D> NodeSize;
    for (UEdGraphNode* N : Nodes)
        NodeSize.Add(N, GetNodeSize(Panel, N));

    // ---- 6. Record original center of selection -------------------------
    FVector2D OrigMin(BIG_NUMBER, BIG_NUMBER), OrigMax(-BIG_NUMBER, -BIG_NUMBER);
    for (UEdGraphNode* N : Nodes)
    {
        OrigMin.X = FMath::Min(OrigMin.X, (float)N->NodePosX);
        OrigMin.Y = FMath::Min(OrigMin.Y, (float)N->NodePosY);
        OrigMax.X = FMath::Max(OrigMax.X, (float)N->NodePosX + NodeSize[N].X);
        OrigMax.Y = FMath::Max(OrigMax.Y, (float)N->NodePosY + NodeSize[N].Y);
    }
    const FVector2D OrigCenter = (OrigMin + OrigMax) * 0.5f;

    // ---- 7. Calculate new positions ----------------------------------------
    TMap<UEdGraphNode*, FVector2D> NewPos;

    float ColX = 0.f; // will be offset to original center later

    for (int32 Col = 0; Col <= MaxCol; ++Col)
    {
        TArray<UEdGraphNode*>& ColNodes = Columns[Col];
        if (ColNodes.IsEmpty()) continue;

        // Max column width
        float MaxW = 0.f;
        for (UEdGraphNode* N : ColNodes) MaxW = FMath::Max(MaxW, NodeSize[N].X);

        // For each node, compute ideal Y = mean Y-center of its predecessors in new positions
        TArray<TPair<float, UEdGraphNode*>> IdealYAndNode;
        for (UEdGraphNode* N : ColNodes)
        {
            float IdealY = 0.f;
            int32 Count = 0;
            for (UEdGraphNode* Pred : Predecessors[N])
            {
                if (NewPos.Contains(Pred))
                {
                    IdealY += NewPos[Pred].Y + NodeSize[Pred].Y * 0.5f;
                    ++Count;
                }
            }
            if (Count > 0)
                IdealY /= Count;
            else
                IdealY = N->NodePosY; // preserve current Y if no placed predecessors

            IdealYAndNode.Add({ IdealY, N });
        }

        // Sort by ideal Y
        IdealYAndNode.Sort([](const TPair<float, UEdGraphNode*>& A,
                              const TPair<float, UEdGraphNode*>& B)
        {
            return A.Key < B.Key;
        });

        // Forward overlap-resolution pass (top → bottom)
        TArray<float> ActualTopY;
        ActualTopY.SetNum(IdealYAndNode.Num());
        float PrevBottom = -BIG_NUMBER;
        for (int32 i = 0; i < IdealYAndNode.Num(); ++i)
        {
            UEdGraphNode* N = IdealYAndNode[i].Value;
            float H = NodeSize[N].Y;
            float IdealTop = IdealYAndNode[i].Key - H * 0.5f;
            float Top = FMath::Max(IdealTop, PrevBottom + kVGap);
            ActualTopY[i] = Top;
            PrevBottom = Top + H;
        }

        // Re-center the column around the mean ideal Y
        float MeanIdeal = 0.f;
        for (auto& Pair : IdealYAndNode) MeanIdeal += Pair.Key;
        MeanIdeal /= IdealYAndNode.Num();

        float GroupTop = ActualTopY[0];
        float GroupBottom = ActualTopY.Last() + NodeSize[IdealYAndNode.Last().Value].Y;
        float GroupCenter = (GroupTop + GroupBottom) * 0.5f;
        float Shift = MeanIdeal - GroupCenter;
        for (float& T : ActualTopY) T += Shift;

        // Store new positions
        for (int32 i = 0; i < IdealYAndNode.Num(); ++i)
        {
            NewPos.Add(IdealYAndNode[i].Value, FVector2D(ColX, ActualTopY[i]));
        }

        ColX += MaxW + kHGap;
    }

    // ---- 8. Center the arrangement on the original center ----------------
    FVector2D NewMin(BIG_NUMBER, BIG_NUMBER), NewMax(-BIG_NUMBER, -BIG_NUMBER);
    for (auto& KV : NewPos)
    {
        NewMin.X = FMath::Min(NewMin.X, KV.Value.X);
        NewMin.Y = FMath::Min(NewMin.Y, KV.Value.Y);
        NewMax.X = FMath::Max(NewMax.X, KV.Value.X + NodeSize[KV.Key].X);
        NewMax.Y = FMath::Max(NewMax.Y, KV.Value.Y + NodeSize[KV.Key].Y);
    }
    const FVector2D NewCenter = (NewMin + NewMax) * 0.5f;
    const FVector2D Offset = OrigCenter - NewCenter;
    for (auto& KV : NewPos) KV.Value += Offset;

    // ---- 9. Apply with undo transaction ----------------------------------
    UEdGraph* Graph = Panel->GetGraphObj();
    const UEdGraphSchema* Schema = Graph->GetSchema();

    const FScopedTransaction Transaction(NSLOCTEXT("SmartArrange", "Arrange", "Smart Arrange Nodes"));
    for (auto& KV : NewPos)
        Schema->SetNodePosition(KV.Key, KV.Value);

    return true;
}
