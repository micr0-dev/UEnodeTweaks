#include "MultiConnectPreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "DragConnection.h"          // GraphEditor/Private — gives us FDragConnection
#include "Input/Events.h"
#include "Widgets/SWindow.h"

void FMultiConnectPreprocessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    if (!bPendingReconnect)
    {
        return;
    }

    bPendingReconnect = false;

    // If the user released CTRL between the drop and this tick, do nothing.
    if (!SlateApp.GetModifierKeys().IsControlDown())
    {
        return;
    }

    TSharedPtr<SWindow> Window = SourceWindowWeak.Pin();
    if (!Window.IsValid())
    {
        return;
    }

    // Replay a left-mouse-down at the exact screen position of the source pin.
    // Slate will route it through the widget tree to the SGraphPin sitting there,
    // which responds with FReply::BeginDragDrop(FDragConnection::New(...)) —
    // starting a fresh wire drag from the same output pin.
    FModifierKeysState CtrlHeld(
        /*bInIsLeftShiftDown*/   false,
        /*bInIsRightShiftDown*/  false,
        /*bInIsLeftControlDown*/ true,
        /*bInIsRightControlDown*/false,
        /*bInIsLeftAltDown*/     false,
        /*bInIsRightAltDown*/    false,
        /*bInIsLeftCommandDown*/ false,
        /*bInIsRightCommandDown*/false,
        /*bInAreCapsLocked*/     false
    );

    FPointerEvent SyntheticDown(
        0, // CursorPointerIndex
        SourcePinScreenPos,
        SourcePinScreenPos,
        TSet<FKey>(),
        EKeys::LeftMouseButton,
        /*WheelDelta*/ 0.0f,
        CtrlHeld
    );

    SlateApp.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), SyntheticDown);
}

bool FMultiConnectPreprocessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        // Always record where each drag might start; we only act on this if
        // the drop later turns out to be a CTRL-held pin connection drop.
        SourcePinScreenPos = FVector2f(MouseEvent.GetScreenSpacePosition());

        if (const FWidgetPath* Path = MouseEvent.GetEventPath())
        {
            if (Path->IsValid())
            {
                SourceWindowWeak = Path->GetWindow();
            }
        }
    }

    return false; // Never consume mouse-down
}

bool FMultiConnectPreprocessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return false;
    }

    if (!MouseEvent.IsControlDown())
    {
        return false;
    }

    // Only re-initiate if the active drag op is a pin-wire connection drag.
    // FDragNode (node body movement) is excluded because it does not match FDragConnection.
    TSharedPtr<FDragDropOperation> DragOp = SlateApp.GetDragDropContent();
    if (DragOp.IsValid() && DragOp->IsOfType<FDragConnection>())
    {
        bPendingReconnect = true;
    }

    return false; // Never consume mouse-up — let the normal drop/connection logic run first
}

bool FMultiConnectPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    // Abort any pending reconnect when CTRL is released.
    if (InKeyEvent.GetKey() == EKeys::LeftControl || InKeyEvent.GetKey() == EKeys::RightControl)
    {
        bPendingReconnect = false;
    }

    return false; // Never consume key events
}
