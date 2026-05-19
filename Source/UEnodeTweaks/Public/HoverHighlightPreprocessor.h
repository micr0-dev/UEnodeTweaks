#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Containers/Set.h"

class SGraphPanel;
class UEdGraphNode;

/**
 * When the cursor rests over a Blueprint node, dims every unrelated node and
 * hides wires that connect only unrelated nodes.
 *
 * "Related" = the hovered node + every node reachable from it by following any
 * pin connection in either direction (data AND execution pins).
 *
 * Node dimming: SWidget::SetRenderOpacity().
 * Wire dimming: FOrthogonalKismetConnectionDrawingPolicy reads IsHighlightActive()
 *               and skips wires whose both endpoints are not highlighted.
 */
class FHoverHighlightPreprocessor : public IInputProcessor
{
public:
    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp,
                                      const FPointerEvent& InMouseEvent) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp,
                                    const FKeyEvent& InKeyEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp,
                                  const FKeyEvent& InKeyEvent) override;
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp,
                      TSharedRef<ICursor> Cursor) override;

    /** Returns the currently highlighted node set (empty when no highlight is active). */
    static const TSet<UEdGraphNode*>& GetHighlightedNodes()  { return HighlightedNodes; }
    static bool  IsHighlightActive()                          { return HighlightedNodes.Num() > 0; }
    /** Current dim opacity (animates 1→0.15 on hover, 0.15→1 on leave). Used by drawing policy. */
    static float GetCurrentDimOpacity()                       { return CurrentDimOpacity; }

private:
    SGraphPanel*   LastPanel       = nullptr;
    UEdGraphNode*  LastHoveredNode = nullptr;
    float          TargetDimOpacity  = 1.0f;

    static constexpr float kDimTarget = 0.15f;
    static constexpr float kDimSpeed  = 8.0f; // FInterpTo speed (higher = faster fade)

    static TSet<UEdGraphNode*> HighlightedNodes;
    static float               CurrentDimOpacity;

    static SGraphPanel*  FindPanelInPath(const FWidgetPath& Path);
    static UEdGraphNode* FindNodeInPath(const FWidgetPath& Path);

    /** BFS from Root through all pin connections in both directions. */
    static TSet<UEdGraphNode*> ComputeConnectedSet(UEdGraphNode* Root);

    void ApplyHighlight(SGraphPanel* Panel, UEdGraphNode* HoveredNode);
    void ClearHighlight();
};
