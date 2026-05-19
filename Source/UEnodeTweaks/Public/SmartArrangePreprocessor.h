#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class SGraphPanel;

/**
 * Intercepts the Q key in Blueprint graph editors.
 *
 * When 2+ nodes are selected with connections between them, replaces UE's
 * built-in "StraightenConnections" with a full column-based layout that:
 *   - Assigns nodes to left→right columns based on data-flow dependency
 *   - Centers each node on the mean Y of its connected upstream nodes
 *   - Eliminates overlaps with vertical padding
 *   - Preserves the original selection's center of mass
 *
 * Falls back to UE's default behaviour (pin-alignment) when fewer than 2
 * selected nodes have inter-connections.
 */
class FSmartArrangePreprocessor : public IInputProcessor
{
public:
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& InMouseEvent) override;
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

private:
    /** Last SGraphPanel the mouse moved over — used to find the target on key press. */
    SGraphPanel* LastSeenPanel = nullptr;

    static SGraphPanel* FindPanelInPath(const FWidgetPath& Path);
    bool RunSmartArrange(SGraphPanel* Panel) const;
};
