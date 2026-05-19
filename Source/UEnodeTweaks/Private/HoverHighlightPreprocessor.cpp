#include "HoverHighlightPreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "SNodePanel.h"
#include "SGraphPanel.h"
#include "SGraphNode.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

TSet<UEdGraphNode*> FHoverHighlightPreprocessor::HighlightedNodes;
float               FHoverHighlightPreprocessor::CurrentDimOpacity = 1.0f;

// ---------------------------------------------------------------------------
// Protected-member accessor — exposes NodeToWidgetLookup
// ---------------------------------------------------------------------------

class SGraphPanelHighlightAccessor : public SGraphPanel
{
public:
    using SGraphPanel::NodeToWidgetLookup;
};

static void SetAllNodeOpacity(SGraphPanel* Panel, float Opacity)
{
    auto& Lookup = static_cast<SGraphPanelHighlightAccessor*>(Panel)->NodeToWidgetLookup;
    for (auto& KV : Lookup)
        KV.Value->SetRenderOpacity(Opacity);
}

static void SetNodeOpacity(SGraphPanel* Panel, UEdGraphNode* Node, float Opacity)
{
    auto& Lookup = static_cast<SGraphPanelHighlightAccessor*>(Panel)->NodeToWidgetLookup;
    if (TSharedRef<SNodePanel::SNode>* Widget = Lookup.Find(Node))
        (*Widget)->SetRenderOpacity(Opacity);
}

// ---------------------------------------------------------------------------
// Widget path helpers
// ---------------------------------------------------------------------------

SGraphPanel* FHoverHighlightPreprocessor::FindPanelInPath(const FWidgetPath& Path)
{
    static const FName PanelType(TEXT("SGraphPanel"));
    for (int32 i = 0; i < Path.Widgets.Num(); ++i)
    {
        if (Path.Widgets[i].Widget->GetType() == PanelType)
            return static_cast<SGraphPanel*>(&Path.Widgets[i].Widget.Get());
    }
    return nullptr;
}

UEdGraphNode* FHoverHighlightPreprocessor::FindNodeInPath(const FWidgetPath& Path)
{
    for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
    {
        const FString TypeStr = Path.Widgets[i].Widget->GetType().ToString();
        if (TypeStr.Contains(TEXT("GraphNode"))
            && !TypeStr.Contains(TEXT("GraphPin"))
            && !TypeStr.Contains(TEXT("GraphPanel")))
        {
            return static_cast<SGraphNode*>(&Path.Widgets[i].Widget.Get())->GetNodeObj();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Direct neighbors only — Root + every node sharing a pin connection
// ---------------------------------------------------------------------------

TSet<UEdGraphNode*> FHoverHighlightPreprocessor::ComputeConnectedSet(UEdGraphNode* Root)
{
    TSet<UEdGraphNode*> Result;
    Result.Add(Root);
    for (UEdGraphPin* Pin : Root->Pins)
    {
        for (UEdGraphPin* Linked : Pin->LinkedTo)
        {
            if (UEdGraphNode* Other = Linked ? Linked->GetOwningNodeUnchecked() : nullptr)
                Result.Add(Other);
        }
    }
    return Result;
}

// ---------------------------------------------------------------------------
// Highlight helpers
// ---------------------------------------------------------------------------

void FHoverHighlightPreprocessor::ApplyHighlight(SGraphPanel* Panel, UEdGraphNode* HoveredNode)
{
    HighlightedNodes = ComputeConnectedSet(HoveredNode);
    TargetDimOpacity = kDimTarget;

    // Apply current animated opacity immediately so the bright/dim split is visible
    SetAllNodeOpacity(Panel, CurrentDimOpacity);
    for (UEdGraphNode* N : HighlightedNodes)
        SetNodeOpacity(Panel, N, 1.0f);
}

void FHoverHighlightPreprocessor::ClearHighlight()
{
    if (HighlightedNodes.Num() == 0) return;
    // Don't restore immediately — let Tick animate back to 1.0 then clean up
    TargetDimOpacity = 1.0f;
}

// ---------------------------------------------------------------------------
// Tick — drives opacity animation
// ---------------------------------------------------------------------------

void FHoverHighlightPreprocessor::Tick(const float DeltaTime, FSlateApplication& /*SlateApp*/,
                                        TSharedRef<ICursor> /*Cursor*/)
{
    if (!LastPanel || HighlightedNodes.Num() == 0) return;

    const float NewOpacity = FMath::FInterpTo(CurrentDimOpacity, TargetDimOpacity, DeltaTime, kDimSpeed);
    if (FMath::IsNearlyEqual(NewOpacity, CurrentDimOpacity, 0.001f)) return;

    CurrentDimOpacity = NewOpacity;
    SetAllNodeOpacity(LastPanel, CurrentDimOpacity);
    for (UEdGraphNode* N : HighlightedNodes)
        SetNodeOpacity(LastPanel, N, 1.0f);

    // Animation finished fading back — clean up
    if (TargetDimOpacity >= 1.0f && FMath::IsNearlyEqual(CurrentDimOpacity, 1.0f, 0.005f))
    {
        SetAllNodeOpacity(LastPanel, 1.0f);
        HighlightedNodes.Empty();
        CurrentDimOpacity = 1.0f;
        LastPanel = nullptr;
    }
}

// ---------------------------------------------------------------------------
// IInputProcessor
// ---------------------------------------------------------------------------

bool FHoverHighlightPreprocessor::HandleKeyDownEvent(FSlateApplication& SlateApp,
                                                     const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.IsRepeat()) return false;
    if (InKeyEvent.GetKey() != EKeys::LeftControl && InKeyEvent.GetKey() != EKeys::RightControl)
        return false;

    // Ctrl just pressed — apply highlight to whatever is currently under the cursor
    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        SlateApp.GetCursorPos(),
        SlateApp.GetInteractiveTopLevelWindows());

    SGraphPanel*  Panel       = FindPanelInPath(Path);
    UEdGraphNode* HoveredNode = Panel ? FindNodeInPath(Path) : nullptr;

    if (Panel && HoveredNode)
    {
        if (Panel != LastPanel && HighlightedNodes.Num() > 0)
        {
            if (LastPanel) SetAllNodeOpacity(LastPanel, 1.0f);
            HighlightedNodes.Empty();
            CurrentDimOpacity = 1.0f;
        }
        LastPanel       = Panel;
        LastHoveredNode = HoveredNode;
        ApplyHighlight(Panel, HoveredNode);
    }

    return false;
}

bool FHoverHighlightPreprocessor::HandleKeyUpEvent(FSlateApplication& /*SlateApp*/,
                                                    const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::LeftControl || InKeyEvent.GetKey() == EKeys::RightControl)
    {
        ClearHighlight();
        LastHoveredNode = nullptr;
    }
    return false;
}

bool FHoverHighlightPreprocessor::HandleMouseMoveEvent(FSlateApplication& SlateApp,
                                                        const FPointerEvent& InMouseEvent)
{
    // Only active while Ctrl is held
    if (!InMouseEvent.IsControlDown())
    {
        ClearHighlight();
        LastHoveredNode = nullptr;
        return false;
    }

    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        InMouseEvent.GetScreenSpacePosition(),
        SlateApp.GetInteractiveTopLevelWindows());

    SGraphPanel*  Panel        = FindPanelInPath(Path);
    UEdGraphNode* HoveredNode  = Panel ? FindNodeInPath(Path) : nullptr;

    // Mouse left the graph panel — trigger fade-out (Tick will clean up LastPanel)
    if (!Panel || !HoveredNode)
    {
        ClearHighlight();
        LastHoveredNode = nullptr;
        return false;
    }

    // Moved to a different panel — hard-clear old panel, start fresh
    if (Panel != LastPanel && HighlightedNodes.Num() > 0)
    {
        if (LastPanel) SetAllNodeOpacity(LastPanel, 1.0f);
        HighlightedNodes.Empty();
        CurrentDimOpacity = 1.0f;
    }

    LastPanel = Panel;

    // Same node as before — nothing to do
    if (HoveredNode == LastHoveredNode)
        return false;

    LastHoveredNode = HoveredNode;
    ApplyHighlight(Panel, HoveredNode);

    return false; // never consume mouse-move
}
