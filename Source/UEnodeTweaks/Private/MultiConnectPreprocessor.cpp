#include "MultiConnectPreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "GraphEditorDragDropAction.h"   // Public GraphEditor header; covers all graph drag ops
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Returns true if any widget in the path has a type name containing "GraphPin".
 *  All SGraphPin subclasses (SBlueprintGraphPin_Bool, SGraphPinVector, etc.)
 *  match this pattern, letting us distinguish pin drags from node-body drags
 *  without needing private engine headers. */
static bool PathContainsGraphPin(const FWidgetPath& Path)
{
    for (int32 i = 0; i < Path.Widgets.Num(); ++i)
    {
        if (Path.Widgets[i].Widget->GetType().ToString().Contains(TEXT("GraphPin")))
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// IInputProcessor interface
// ---------------------------------------------------------------------------

void FMultiConnectPreprocessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    if (!bPendingReconnect)
    {
        return;
    }

    bPendingReconnect = false;

    // If the user released CTRL between the drop and this tick, bail out.
    if (!SlateApp.GetModifierKeys().IsControlDown())
    {
        return;
    }

    TSharedPtr<SWindow> Window = SourceWindowWeak.Pin();
    if (!Window.IsValid())
    {
        return;
    }

    // Re-initiate the drag by posting a left-mouse-down at the source pin's
    // screen position. FSlateApplication::OnMouseDown routes the event through
    // the widget tree; the SGraphPin sitting at that position responds with
    // FReply::BeginDragDrop(FDragConnection::New(...)), starting a fresh wire.
    //
    // CTRL is physically held by the user so the modifier state is read
    // correctly from the platform — no need to construct FPointerEvent manually.
    SlateApp.OnMouseDown(Window->GetNativeWindow(), EMouseButtons::Left, SourcePinScreenPos);
}

bool FMultiConnectPreprocessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return false;
    }

    SourcePinScreenPos = MouseEvent.GetScreenSpacePosition();
    bDownOnPin = false;

    if (const FWidgetPath* Path = MouseEvent.GetEventPath())
    {
        if (Path->IsValid())
        {
            SourceWindowWeak = Path->GetWindow();
            bDownOnPin = PathContainsGraphPin(*Path);
        }
    }

    return false; // Never consume
}

bool FMultiConnectPreprocessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return false;
    }

    if (!MouseEvent.IsControlDown() || !bDownOnPin)
    {
        return false;
    }

    // Only re-initiate when the completed drag was a graph-editor drag op
    // (FDragConnection for pin wires; FDragNode for node bodies is excluded
    //  because bDownOnPin will be false for node-header clicks).
    TSharedPtr<FDragDropOperation> DragOp = SlateApp.GetDragDropContent();
    if (DragOp.IsValid() && DragOp->IsOfType<FGraphEditorDragDropAction>())
    {
        bPendingReconnect = true;
    }

    return false; // Never consume — let the normal drop/connection logic run first
}

bool FMultiConnectPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    // Abort any pending reconnect when CTRL is released.
    if (InKeyEvent.GetKey() == EKeys::LeftControl || InKeyEvent.GetKey() == EKeys::RightControl)
    {
        bPendingReconnect = false;
    }

    return false; // Never consume
}
