#pragma once

#include "Framework/Application/IInputProcessor.h"
#include "EdGraph/EdGraphPin.h"         // FEdGraphPinReference (safe weak-ref to UEdGraphPin)
#include "Math/Vector2D.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

class SWindow;
class SGraphPanel;
class UK2Node_ExecutionSequence;

class FMultiConnectPreprocessor : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

private:
    void SynthesizeClickAt(FSlateApplication& SlateApp, const FVector2D& ScreenPos);
    bool TryFindAndClickSequencePin(FSlateApplication& SlateApp);

    // ── Core multi-connect state ─────────────────────────────────────────────
    bool bPendingReconnect = false;
    bool bDownOnPin        = false;

    FVector2D SourcePinScreenPos = FVector2D::ZeroVector;
    TWeakPtr<SWindow>     SourceWindowWeak;
    TWeakPtr<SGraphPanel> SourceGraphPanelWeak;

    // FEdGraphPinReference safely stores a UEdGraphPin* via its owning node +
    // pin GUID (UEdGraphPin is not a UObject, so TWeakObjectPtr can't be used).
    FEdGraphPinReference SourcePinRef;

    // ── Exec / Sequence state ────────────────────────────────────────────────
    bool bWaitingForSequencePinWidget = false;
    FEdGraphPinReference                      PendingThenPinRef;
    TWeakObjectPtr<UK2Node_ExecutionSequence> SequenceNodeWeak;
};
