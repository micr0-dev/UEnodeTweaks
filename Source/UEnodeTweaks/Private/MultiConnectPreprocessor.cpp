#include "MultiConnectPreprocessor.h"

#include "Application/SlateApplicationBase.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorDragDropAction.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "ScopedTransaction.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "Widgets/SWindow.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_ExecutionSequence.h"

// ─── Widget path helpers ──────────────────────────────────────────────────────

static bool PathContainsGraphPin(const FWidgetPath& Path)
{
    for (int32 i = 0; i < Path.Widgets.Num(); ++i)
    {
        if (Path.Widgets[i].Widget->GetType().ToString().Contains(TEXT("GraphPin")))
            return true;
    }
    return false;
}

static SGraphPin* FindGraphPinInPath(const FWidgetPath& Path)
{
    for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
    {
        const TSharedRef<SWidget>& W = Path.Widgets[i].Widget;
        if (W->GetType().ToString().Contains(TEXT("GraphPin")))
            return static_cast<SGraphPin*>(&W.Get());
    }
    return nullptr;
}

static SGraphPanel* FindGraphPanelInPath(const FWidgetPath& Path)
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

// ─── Pin type helpers ─────────────────────────────────────────────────────────

static bool IsExecOutputPin(const UEdGraphPin* Pin)
{
    return Pin
        && Pin->Direction == EGPD_Output
        && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
}

// ─── IInputProcessor ──────────────────────────────────────────────────────────

void FMultiConnectPreprocessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    // Phase 2 of exec multi-connect: the Sequence node was created last frame;
    // now find its new pin widget and synthesize the click.
    if (bWaitingForSequencePinWidget)
    {
        if (!SlateApp.GetModifierKeys().IsControlDown())
        {
            bWaitingForSequencePinWidget = false;
            return;
        }
        TryFindAndClickSequencePin(SlateApp);
        return;
    }

    if (!bPendingReconnect) return;
    bPendingReconnect = false;

    if (!SlateApp.GetModifierKeys().IsControlDown()) return;

    // ── Exec multi-connect ────────────────────────────────────────────────────
    // Exec outputs allow only one downstream connection, so multi-connect is
    // routed through a UK2Node_ExecutionSequence instead of a plain re-drag.
    UEdGraphPin* SourcePin = SourcePinRef.Get();
    if (SourcePin && IsExecOutputPin(SourcePin) && SourcePin->LinkedTo.Num() > 0)
    {
        UEdGraph* Graph = SourcePin->GetOwningNode()->GetGraph();
        if (!Graph) return;
        const UEdGraphSchema_K2* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
        if (!Schema) return;

        UK2Node_ExecutionSequence* SeqNode = nullptr;

        // Case A: SourcePinRef is now pointing at a Sequence Then-pin.
        // After the first exec cycle HandleMouseButtonDownEvent updates SourcePinRef
        // to the Then pin that was just synthetic-clicked.
        SeqNode = Cast<UK2Node_ExecutionSequence>(SourcePin->GetOwningNode());

        // Case B: source exec pin is wired directly into a Sequence's In-pin.
        if (!SeqNode && SourcePin->LinkedTo.Num() > 0)
        {
            UEdGraphPin* Downstream = SourcePin->LinkedTo[0];
            if (Downstream)
                SeqNode = Cast<UK2Node_ExecutionSequence>(Downstream->GetOwningNode());
        }

        if (!SeqNode)
        {
            // ── First exec cycle: create Sequence and rewire ─────────────────
            UEdGraphNode* SourceNode = SourcePin->GetOwningNode();
            UEdGraphPin*  OldTarget  = SourcePin->LinkedTo.Num() > 0 ? SourcePin->LinkedTo[0] : nullptr;

            const FScopedTransaction Transaction(
                NSLOCTEXT("UEnodeTweaks", "MultiConnectSequenceCreate", "Multi-Connect: Insert Sequence"));
            Graph->Modify();
            SourceNode->Modify();
            if (OldTarget) OldTarget->GetOwningNode()->Modify();

            // Use the same creation path as the context menu (SpawnNodeFromTemplate →
            // CreateNode → MarkBlueprintAsModified) so the node is fully initialized
            // and its exec pins are valid for new connections.
            SeqNode = FEdGraphSchemaAction_K2NewNode::SpawnNodeFromTemplate<UK2Node_ExecutionSequence>(
                Graph,
                NewObject<UK2Node_ExecutionSequence>(GetTransientPackage()),
                FVector2D(SourceNode->NodePosX + 350.0f, SourceNode->NodePosY),
                /*bSelectNewNode=*/false);

            if (!SeqNode) return;

            // source → Sequence.In
            UEdGraphPin* SeqIn = SeqNode->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
            Schema->BreakPinLinks(*SourcePin, true);
            Schema->TryCreateConnection(SourcePin, SeqIn);

            // Sequence.Then 0 → old target (preserves the first connection)
            if (OldTarget)
                Schema->TryCreateConnection(SeqNode->GetThenPinGivenIndex(0), OldTarget);

            SequenceNodeWeak = SeqNode;

            UE_LOG(LogTemp, Log, TEXT("[UEnodeTweaks] Sequence created. Then-pin count: %d, Then_0 linked: %d"),
                SeqNode->Pins.Num(),
                SeqNode->GetThenPinGivenIndex(0) ? SeqNode->GetThenPinGivenIndex(0)->LinkedTo.Num() : -1);
        }
        else
        {
            // ── Subsequent exec cycles: extend the existing Sequence ──────────
            // Check whether an unconnected Then pin already exists (Then 1 is
            // created by AllocateDefaultPins and is free until cycle 2 uses it).
            bool bHasAvailablePin = false;
            for (UEdGraphPin* Pin : SeqNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() == 0)
                {
                    bHasAvailablePin = true;
                    break;
                }
            }
            if (!bHasAvailablePin)
            {
                const FScopedTransaction Transaction(
                    NSLOCTEXT("UEnodeTweaks", "MultiConnectSequenceAddPin", "Multi-Connect: Add Sequence Output"));
                SeqNode->Modify();
                SeqNode->AddInputPin();
                // AddInputPin fires a general NotifyGraphChanged (full deferred rebuild).
                // Send a targeted EditNode action so the pin widget is created immediately
                // this frame, before our TryFindAndClickSequencePin retry runs next Tick.
                Graph->NotifyNodeChanged(SeqNode);
            }
        }

        // Use the first unconnected Then output as the drag source.
        UEdGraphPin* NextThenPin = nullptr;
        for (UEdGraphPin* Pin : SeqNode->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() == 0)
            {
                NextThenPin = Pin;
                break;
            }
        }
        if (!NextThenPin) return;

        PendingThenPinRef.SetPin(NextThenPin);
        bWaitingForSequencePinWidget = true;  // widget appears next frame
        return;
    }

    // ── Data-pin multi-connect ────────────────────────────────────────────────
    TSharedPtr<SWindow> Window = SourceWindowWeak.Pin();
    if (!Window.IsValid()) return;

    SynthesizeClickAt(SlateApp, SourcePinScreenPos);
}

bool FMultiConnectPreprocessor::TryFindAndClickSequencePin(FSlateApplication& SlateApp)
{
    UEdGraphPin*          TargetPin = PendingThenPinRef.Get();
    TSharedPtr<SGraphPanel> Panel   = SourceGraphPanelWeak.Pin();
    TSharedPtr<SWindow>   Window    = SourceWindowWeak.Pin();

    if (!TargetPin || !Panel.IsValid() || !Window.IsValid())
    {
        bWaitingForSequencePinWidget = false;
        return false;
    }

    TSet<TSharedRef<SWidget>> AllPins;
    Panel->GetAllPins(AllPins);

    for (const TSharedRef<SWidget>& PinWidget : AllPins)
    {
        SGraphPin& GPW = static_cast<SGraphPin&>(PinWidget.Get());
        if (GPW.GetPinObj() == TargetPin)
        {
            const FGeometry& Geom = GPW.GetTickSpaceGeometry();
            if (Geom.GetAbsoluteSize().IsNearlyZero())
            {
                UE_LOG(LogTemp, Log, TEXT("[UEnodeTweaks] Pin widget found but geometry is zero — retrying next tick"));
                return false;  // widget exists but layout hasn't run yet — retry next tick
            }

            FVector2D PinCenter = Geom.GetAbsolutePosition() + Geom.GetAbsoluteSize() * 0.5f;

            // Verify the computed screen position actually hits THIS pin widget.
            // GetTickSpaceGeometry positions can be stale or in wrong coordinate
            // space before the first paint — skip if the hit-test disagrees.
            FWidgetPath VerifyPath = SlateApp.LocateWindowUnderMouse(
                PinCenter, SlateApp.GetTopLevelWindows(), false);
            SGraphPin* HitPin = FindGraphPinInPath(VerifyPath);
            if (HitPin != &GPW)
            {
                UE_LOG(LogTemp, Warning, TEXT("[UEnodeTweaks] Hit-test mismatch: computed center (%.1f,%.1f) hits %s, not target pin — retrying"),
                    PinCenter.X, PinCenter.Y,
                    HitPin ? *HitPin->GetPinObj()->PinName.ToString() : TEXT("nothing"));
                return false;  // position doesn't hit right widget — retry next tick
            }

            UE_LOG(LogTemp, Log, TEXT("[UEnodeTweaks] Synthesizing click at (%.1f, %.1f) for pin %s"),
                PinCenter.X, PinCenter.Y, *TargetPin->PinName.ToString());

            SourcePinScreenPos           = PinCenter;
            bWaitingForSequencePinWidget = false;
            SynthesizeClickAt(SlateApp, PinCenter);
            return true;
        }
    }

    // Widget not ready — Tick will retry next frame.
    return false;
}

void FMultiConnectPreprocessor::SynthesizeClickAt(FSlateApplication& SlateApp, const FVector2D& ScreenPos)
{
    TSharedPtr<SWindow> Window = SourceWindowWeak.Pin();
    if (!Window.IsValid()) return;

    // No CTRL on the synthetic event: SGraphPin::OnMouseButtonDown has a
    // "move links" path gated on (IsControlDown && LinkedTo.Num() > 0) that
    // breaks existing connections and grabs the other wire end. Omitting CTRL
    // bypasses it so we get a fresh FDragConnection from the source pin.
    // The user's physical CTRL is picked up by subsequent Slate events.
    FModifierKeysState NoModifiers;
    TSet<FKey>         NoPressedButtons;
    FPointerEvent SyntheticDown(
        FSlateApplicationBase::CursorPointerIndex,
        ScreenPos, ScreenPos,
        NoPressedButtons,
        EKeys::LeftMouseButton,
        0.0f,
        NoModifiers
    );
    SlateApp.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), SyntheticDown);
}

bool FMultiConnectPreprocessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return false;

    // During an active drag the user physically clicks to "confirm" a drop on
    // a target. Updating SourcePinScreenPos here would corrupt it to the
    // target's position, breaking 3rd+ reconnect cycles.
    if (SlateApp.IsDragDropping()) return false;

    SourcePinScreenPos = MouseEvent.GetScreenSpacePosition();
    bDownOnPin         = false;
    SourceWindowWeak.Reset();
    SourcePinRef       = FEdGraphPinReference();
    SourceGraphPanelWeak.Reset();

    // Preprocessors run before Slate computes the widget path, so
    // MouseEvent.GetEventPath() is always null. Perform our own hit-test.
    FWidgetPath WidgetPath = SlateApp.LocateWindowUnderMouse(
        SourcePinScreenPos, SlateApp.GetTopLevelWindows(), false);

    if (WidgetPath.IsValid())
    {
        SourceWindowWeak = WidgetPath.GetWindow();
        bDownOnPin       = PathContainsGraphPin(WidgetPath);

        if (bDownOnPin)
        {
            if (SGraphPin* PinWidget = FindGraphPinInPath(WidgetPath))
                SourcePinRef.SetPin(PinWidget->GetPinObj());

            if (SGraphPanel* Panel = FindGraphPanelInPath(WidgetPath))
                SourceGraphPanelWeak = StaticCastSharedRef<SGraphPanel>(Panel->AsShared());
        }
    }

    return false;
}

bool FMultiConnectPreprocessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return false;
    if (!MouseEvent.IsControlDown() || !bDownOnPin)               return false;

    TSharedPtr<FDragDropOperation> DragOp = SlateApp.GetDragDroppingContent();
    if (DragOp.IsValid() && DragOp->IsOfType<FGraphEditorDragDropAction>())
        bPendingReconnect = true;

    return false;
}

bool FMultiConnectPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::LeftControl || InKeyEvent.GetKey() == EKeys::RightControl)
    {
        bPendingReconnect            = false;
        bWaitingForSequencePinWidget = false;
    }
    return false;
}
