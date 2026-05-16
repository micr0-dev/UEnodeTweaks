#include "MultiPinDragPreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Layout/WidgetPath.h"
#include "ScopedTransaction.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "Widgets/Text/STextBlock.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"

// ─── Widget path helpers ──────────────────────────────────────────────────────

static SGraphPin* MPD_FindGraphPinInPath(const FWidgetPath& Path)
{
    for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
    {
        const TSharedRef<SWidget>& W = Path.Widgets[i].Widget;
        if (W->GetType().ToString().Contains(TEXT("GraphPin")))
            return static_cast<SGraphPin*>(&W.Get());
    }
    return nullptr;
}

static SGraphPanel* MPD_FindGraphPanelInPath(const FWidgetPath& Path)
{
    static const FName GraphPanelType(TEXT("SGraphPanel"));
    for (int32 i = 0; i < Path.Widgets.Num(); ++i)
    {
        const TSharedRef<SWidget>& W = Path.Widgets[i].Widget;
        if (W->GetType() == GraphPanelType)
            return static_cast<SGraphPanel*>(&W.Get());
    }
    return nullptr;
}

static UEdGraphNode* MPD_FindGraphNodeInPath(const FWidgetPath& Path)
{
    for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
    {
        const TSharedRef<SWidget>& W = Path.Widgets[i].Widget;
        const FString TypeStr = W->GetType().ToString();
        // "SGraphNode" and all its subclasses contain "GraphNode",
        // but SGraphPin ("GraphPin") and SGraphPanel ("GraphPanel") do not.
        if (TypeStr.Contains(TEXT("GraphNode"))
            && !TypeStr.Contains(TEXT("GraphPin"))
            && !TypeStr.Contains(TEXT("GraphPanel")))
        {
            return static_cast<SGraphNode*>(&W.Get())->GetNodeObj();
        }
    }
    return nullptr;
}

// ─── Protected-method accessor ────────────────────────────────────────────────
// SpawnPinDragEvent is protected on SGraphPin. A minimal subclass exposes it
// so we can create an FDragConnection with multiple source pins at once.
class SGraphPinDragAccessor : public SGraphPin
{
public:
    using SGraphPin::SpawnPinDragEvent;
};

static TSharedRef<FDragDropOperation> SpawnMultiPinDrag(
    SGraphPin*                              AnchorPin,
    const TSharedRef<SGraphPanel>&          Panel,
    const TArray<TSharedRef<SGraphPin>>&    Pins)
{
    return static_cast<SGraphPinDragAccessor*>(AnchorPin)->SpawnPinDragEvent(Panel, Pins);
}

// ─── Pin matching / scoring ───────────────────────────────────────────────────

// Score a candidate target pin against a source pin.
// Higher is better. Priority tiers (never overlap):
//   Tier 3 (best):  same type  + visible
//   Tier 2:         same type  + hidden
//   Tier 1:         compat type + visible
//   Tier 0 (worst): compat type + hidden
// Within a tier, position proximity breaks ties (closer = higher score).
static float ScoreTargetPin(
    const UEdGraphPin*              SourcePin,
    const UEdGraphPin*              TargetPin,
    float                           SourcePinY,
    const TMap<UEdGraphPin*, float>& PinYPositions)
{
    const bool bSameType = (TargetPin->PinType == SourcePin->PinType);
    const bool bVisible  = !TargetPin->bHidden;

    // Each tier is worth 10 000 points so position (< 1 000) never crosses tiers
    const float TierScore = (bSameType ? 20000.f : 0.f) + (bVisible ? 10000.f : 0.f);

    // Position proximity: sigmoid-style so the score stays well within one tier
    float PosScore = 0.f;
    if (const float* TargetY = PinYPositions.Find(TargetPin))
        PosScore = 999.f / (1.f + FMath::Abs(*TargetY - SourcePinY) / 50.f);

    return TierScore + PosScore;
}

// Build a screen-space Y-position map for all pins currently in the panel
static TMap<UEdGraphPin*, float> BuildPinYMap(SGraphPanel* Panel)
{
    TMap<UEdGraphPin*, float> Map;
    if (!Panel) return Map;

    TSet<TSharedRef<SWidget>> AllPins;
    Panel->GetAllPins(AllPins);
    for (const TSharedRef<SWidget>& W : AllPins)
    {
        SGraphPin& GPW = static_cast<SGraphPin&>(W.Get());
        if (UEdGraphPin* PinObj = GPW.GetPinObj())
            Map.Add(PinObj, GPW.GetTickSpaceGeometry().GetAbsolutePosition().Y);
    }
    return Map;
}

// ─── Text-color helpers ───────────────────────────────────────────────────────

static void SetWidgetTextColor(SWidget& Widget, const FSlateColor& Color)
{
    static const FName TextBlockType(TEXT("STextBlock"));
    if (Widget.GetType() == TextBlockType)
        static_cast<STextBlock&>(Widget).SetColorAndOpacity(Color);

    FChildren* Children = Widget.GetChildren();
    if (Children)
        for (int32 i = 0; i < Children->Num(); ++i)
            SetWidgetTextColor(Children->GetChildAt(i).Get(), Color);
}

// ─── Constants ────────────────────────────────────────────────────────────────

static const FLinearColor GSelectedExecColor (1.0f, 0.75f, 0.0f, 1.0f);  // gold arrow tint
static const FLinearColor GSelectedLabelColor(1.0f, 0.85f, 0.1f, 1.0f);  // yellow text
static const FLinearColor GDefaultColor      (1.0f, 1.0f,  1.0f, 1.0f);  // white = no modifier

// ─── Helpers ──────────────────────────────────────────────────────────────────

void FMultiPinDragPreprocessor::ApplySelectionColor(SGraphPin* Pin, bool bSelected)
{
    if (!Pin) return;
    const UEdGraphPin* PinObj = Pin->GetPinObj();
    const bool bIsExec = PinObj && PinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;

    if (bIsExec)
    {
        // Exec pins: tint the arrow icon gold
        Pin->SetPinColorModifier(bSelected ? GSelectedExecColor : GDefaultColor);
    }
    else
    {
        // Data pins: tint the label text yellow, leave the type-colored dot alone
        Pin->SetPinColorModifier(GDefaultColor);
        SetWidgetTextColor(*Pin, bSelected ? FSlateColor(GSelectedLabelColor) : FSlateColor::UseForeground());
    }
}

bool FMultiPinDragPreprocessor::IsPinSelected(const UEdGraphPin* Pin) const
{
    for (const FEdGraphPinReference& Ref : SelectedPins)
        if (Ref.Get() == Pin) return true;
    return false;
}

void FMultiPinDragPreprocessor::ClearHoverPreview()
{
    if (TSharedPtr<SGraphPanel> Panel = PanelWeak.Pin())
        for (UEdGraphPin* Pin : HoverPreviewPins)
            Panel->RemovePinFromHoverSet(Pin);

    HoverPreviewPins.Reset();
    HoverPreviewNode.Reset();
}

void FMultiPinDragPreprocessor::ClearSelection()
{
    ClearHoverPreview();

    if (TSharedPtr<SGraphPanel> Panel = PanelWeak.Pin())
    {
        TSet<TSharedRef<SWidget>> AllPins;
        Panel->GetAllPins(AllPins);
        for (const TSharedRef<SWidget>& W : AllPins)
        {
            SGraphPin& GPW = static_cast<SGraphPin&>(W.Get());
            if (IsPinSelected(GPW.GetPinObj()))
                ApplySelectionColor(&GPW, false);
        }
    }
    SelectedPins.Reset();
    PanelWeak.Reset();
    PendingDropTarget.Reset();
}

void FMultiPinDragPreprocessor::UpdateHoverPreview(FSlateApplication& SlateApp)
{
    if (!bIsMultiDragging || SelectedPins.Num() == 0) return;

    TSharedPtr<SGraphPanel> Panel = PanelWeak.Pin();
    if (!Panel.IsValid()) return;

    FWidgetPath HoverPath = SlateApp.LocateWindowUnderMouse(
        SlateApp.GetCursorPos(), SlateApp.GetTopLevelWindows(), false);

    UEdGraphNode* HoveredNode = MPD_FindGraphNodeInPath(HoverPath);

    // Only update when the hovered node changes
    if (HoveredNode == HoverPreviewNode.Get()) return;

    ClearHoverPreview();

    if (!HoveredNode) return;

    // Don't preview connections to nodes that own our selected pins
    for (const FEdGraphPinReference& SelRef : SelectedPins)
    {
        if (UEdGraphPin* P = SelRef.Get())
            if (P->GetOwningNode() == HoveredNode) return;
    }

    UEdGraph*             Graph  = HoveredNode->GetGraph();
    const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
    if (!Schema) return;

    // For each selected pin, highlight the best compatible pin on the hovered node
    for (const FEdGraphPinReference& SelRef : SelectedPins)
    {
        UEdGraphPin* SelPin = SelRef.Get();
        if (!SelPin) continue;

        for (UEdGraphPin* TPin : HoveredNode->Pins)
        {
            if (TPin->Direction == SelPin->Direction) continue;
            if (Schema->CanCreateConnection(SelPin, TPin).Response != CONNECT_RESPONSE_DISALLOW)
            {
                Panel->AddPinToHoverSet(TPin);
                HoverPreviewPins.Add(TPin);
                break;
            }
        }
    }

    HoverPreviewNode = HoveredNode;
}

// ─── IInputProcessor ──────────────────────────────────────────────────────────

void FMultiPinDragPreprocessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    // Re-apply selection visuals every tick — pin widgets can be recreated at
    // any time (node refresh, hover) resetting color modifiers to defaults.
    if (SelectedPins.Num() > 0)
    {
        if (TSharedPtr<SGraphPanel> Panel = PanelWeak.Pin())
        {
            TSet<TSharedRef<SWidget>> AllPins;
            Panel->GetAllPins(AllPins);
            for (const TSharedRef<SWidget>& W : AllPins)
            {
                SGraphPin& GPW = static_cast<SGraphPin&>(W.Get());
                if (IsPinSelected(GPW.GetPinObj()))
                    ApplySelectionColor(&GPW, true);
            }
        }
    }

    // Update connection preview while dragging over nodes
    UpdateHoverPreview(SlateApp);

    if (!bPendingMultiConnect) return;
    bPendingMultiConnect = false;

    UEdGraphNode* TargetNode = PendingDropTarget.Get();
    PendingDropTarget.Reset();

    if (!TargetNode || SelectedPins.Num() == 0) { ClearSelection(); return; }

    // Don't connect back to a node that owns one of our selected pins
    for (const FEdGraphPinReference& SelRef : SelectedPins)
    {
        if (UEdGraphPin* P = SelRef.Get())
            if (P->GetOwningNode() == TargetNode) { ClearSelection(); return; }
    }

    UEdGraph*             Graph  = TargetNode->GetGraph();
    const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
    if (!Schema) { ClearSelection(); return; }

    // Build pin Y positions once for all matching decisions
    const TMap<UEdGraphPin*, float> PinYMap = BuildPinYMap(PanelWeak.Pin().Get());

    FScopedTransaction Transaction(
        NSLOCTEXT("UEnodeTweaks", "MultiPinDragConnect", "Multi-Pin Connect"));

    bool bMadeAnyConnection = false;

    // Track which target pins are already spoken for so that two selected pins
    // of the same type don't both claim the same input (e.g. 4 ints → MakeArray).
    TSet<UEdGraphPin*> ClaimedTargetPins;

    for (const FEdGraphPinReference& SelRef : SelectedPins)
    {
        UEdGraphPin* SelPin = SelRef.Get();
        if (!SelPin) continue;

        // Score all compatible candidate pins, pick the best unclaimed one.
        // Priority: same type > visible > position proximity.
        const float  SourceY  = PinYMap.FindRef(SelPin);
        UEdGraphPin* BestPin  = nullptr;
        float        BestScore = -FLT_MAX;

        for (UEdGraphPin* TPin : TargetNode->Pins)
        {
            if (TPin->Direction == SelPin->Direction) continue;
            if (ClaimedTargetPins.Contains(TPin)) continue;
            if (Schema->CanCreateConnection(SelPin, TPin).Response == CONNECT_RESPONSE_DISALLOW)
                continue;

            const float Score = ScoreTargetPin(SelPin, TPin, SourceY, PinYMap);
            if (Score > BestScore) { BestScore = Score; BestPin = TPin; }
        }

        if (BestPin)
        {
            ClaimedTargetPins.Add(BestPin);
            TargetNode->Modify();
            SelPin->GetOwningNode()->Modify();
            Schema->TryCreateConnection(SelPin, BestPin);
            bMadeAnyConnection = true;
        }
    }

    if (!bMadeAnyConnection)
        Transaction.Cancel();

    if (bKeepSelectionForCtrl)
    {
        // Ctrl is held — keep selection alive so the user can immediately drag
        // to the next target node. Only reset the hover preview.
        bKeepSelectionForCtrl = false;
        ClearHoverPreview();
    }
    else
    {
        ClearSelection();
    }
}

bool FMultiPinDragPreprocessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return false;
    if (SlateApp.IsDragDropping()) return false;

    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        MouseEvent.GetScreenSpacePosition(), SlateApp.GetTopLevelWindows(), false);
    SGraphPin* HitPin = MPD_FindGraphPinInPath(Path);

    // ── Shift+click: toggle pin in/out of selection ───────────────────────────
    if (MouseEvent.IsShiftDown())
    {
        if (!HitPin) return false;

        UEdGraphPin* PinObj = HitPin->GetPinObj();

        for (int32 i = 0; i < SelectedPins.Num(); ++i)
        {
            if (SelectedPins[i].Get() == PinObj)
            {
                ApplySelectionColor(HitPin, false);
                SelectedPins.RemoveAt(i);
                return true;
            }
        }

        FEdGraphPinReference Ref;
        Ref.SetPin(PinObj);
        SelectedPins.Add(Ref);
        ApplySelectionColor(HitPin, true);

        if (!PanelWeak.IsValid())
            if (SGraphPanel* Panel = MPD_FindGraphPanelInPath(Path))
                PanelWeak = StaticCastSharedRef<SGraphPanel>(Panel->AsShared());

        return true;
    }

    // ── No shift: clear selection or start multi-drag ─────────────────────────
    if (SelectedPins.Num() == 0) return false;

    if (!HitPin || !IsPinSelected(HitPin->GetPinObj()))
    {
        ClearSelection();
        return false;
    }

    // Clicked a selected pin — start a multi-wire drag
    TSharedPtr<SGraphPanel> Panel = PanelWeak.Pin();
    if (!Panel.IsValid()) { ClearSelection(); return false; }

    TArray<TSharedRef<SGraphPin>> SelectedWidgets;
    SGraphPin* AnchorWidget = nullptr;

    TSet<TSharedRef<SWidget>> AllPinWidgets;
    Panel->GetAllPins(AllPinWidgets);

    for (const TSharedRef<SWidget>& W : AllPinWidgets)
    {
        SGraphPin& GPW = static_cast<SGraphPin&>(W.Get());
        UEdGraphPin* PinObj = GPW.GetPinObj();
        if (!IsPinSelected(PinObj)) continue;

        SelectedWidgets.Add(StaticCastSharedRef<SGraphPin>(W));

        if (PinObj == HitPin->GetPinObj())
            AnchorWidget = &GPW;
    }

    bIsMultiDragging = (SelectedWidgets.Num() > 1);

    if (AnchorWidget && SelectedWidgets.Num() > 1)
    {
        // Build FDragConnection with all selected pins — engine renders a live
        // wire preview for every pin, not just the anchor.
        TSharedRef<FDragDropOperation> DragOp =
            SpawnMultiPinDrag(AnchorWidget, Panel.ToSharedRef(), SelectedWidgets);
        SlateApp.ProcessReply(Path, FReply::Handled().BeginDragDrop(DragOp), &Path, &MouseEvent, 0);
        return true;
    }

    return false;
}

bool FMultiPinDragPreprocessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return false;
    if (!bIsMultiDragging) return false;
    bIsMultiDragging = false;

    if (!SlateApp.GetDragDroppingContent().IsValid()) return false;

    // Detect drop target BEFORE cancelling the drag
    FWidgetPath DropPath = SlateApp.LocateWindowUnderMouse(
        MouseEvent.GetScreenSpacePosition(), SlateApp.GetTopLevelWindows(), false);

    SGraphPin*    DropPinWidget = MPD_FindGraphPinInPath(DropPath);
    UEdGraphNode* DropNode = DropPinWidget
        ? DropPinWidget->GetPinObj()->GetOwningNode()
        : MPD_FindGraphNodeInPath(DropPath);

    // Cancel FDragConnection's drop so it doesn't connect all selected pins to
    // the same single target pin (which would break existing single-input connections).
    SlateApp.ProcessReply(DropPath, FReply::Handled().EndDragDrop(), &DropPath, &MouseEvent, 0);

    if (DropNode)
    {
        PendingDropTarget    = DropNode;
        bPendingMultiConnect = true;
        bKeepSelectionForCtrl = MouseEvent.IsControlDown();
    }

    // Always consume so MultiConnectPreprocessor doesn't also fire on this drop.
    return true;
}

bool FMultiPinDragPreprocessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
        ClearSelection();
    return false;
}

bool FMultiPinDragPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    // Releasing Ctrl ends the multi-drop chain and clears the selection
    if (InKeyEvent.GetKey() == EKeys::LeftControl || InKeyEvent.GetKey() == EKeys::RightControl)
        ClearSelection();
    return false;
}
