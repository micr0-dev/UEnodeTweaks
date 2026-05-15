#pragma once

#include "IInputProcessor.h"
#include "InputCoreTypes.h"
#include "Math/Vector2D.h"

/**
 * Slate input preprocessor that implements CTRL-held multi-connect for Blueprint pin wires.
 *
 * When the user releases a pin connection drag while holding CTRL, a synthetic
 * mouse-down is replayed at the source pin's screen position on the next tick,
 * effectively re-initiating the drag from the same output pin so they can
 * connect it to multiple inputs in succession. Releasing CTRL ends the mode.
 */
class FMultiConnectPreprocessor : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

private:
    // Set to true when we want to re-initiate a pin drag on the next tick
    bool bPendingReconnect = false;

    // Screen-space position of the source pin (recorded at mouse-down)
    FVector2D SourcePinScreenPos = FVector2D::ZeroVector;

    // The Slate window that hosted the drag (for routing the synthetic event)
    TWeakPtr<SWindow> SourceWindowWeak;
};
