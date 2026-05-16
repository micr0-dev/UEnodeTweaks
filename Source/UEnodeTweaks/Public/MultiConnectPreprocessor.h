#pragma once

#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"
#include "Math/Vector2D.h"
#include "Widgets/SWindow.h"

/**
 * Slate input preprocessor that implements CTRL-held multi-connect for Blueprint pin wires.
 *
 * When the user releases a pin-connection drag while holding CTRL, the drag is
 * re-initiated from the same source pin on the next tick so they can connect one
 * output pin to many inputs in succession. Releasing CTRL ends the mode.
 */
class FMultiConnectPreprocessor : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

private:
    // True when a re-initiation of the source-pin drag should fire on the next tick
    bool bPendingReconnect = false;

    // True when the last left-mouse-down landed on a graph pin widget
    bool bDownOnPin = false;

    // Screen-space position of the source pin (recorded at mouse-down)
    FVector2D SourcePinScreenPos = FVector2D::ZeroVector;

    // The Slate window that hosted the drag (for routing the synthetic event)
    TWeakPtr<SWindow> SourceWindowWeak;
};
