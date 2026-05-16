#pragma once

#include "Framework/Application/IInputProcessor.h"
#include "EdGraph/EdGraphPin.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"

class SGraphPin;
class SGraphPanel;
class UEdGraphNode;

class FMultiPinDragPreprocessor : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;

private:
    void ApplySelectionColor(SGraphPin* Pin, bool bSelected);
    void ClearSelection();
    void UpdateHoverPreview(FSlateApplication& SlateApp);
    void ClearHoverPreview();
    bool IsPinSelected(const UEdGraphPin* Pin) const;

    // Shift+clicked pin selection
    TArray<FEdGraphPinReference> SelectedPins;
    TWeakPtr<SGraphPanel>        PanelWeak;

    // Set in HandleMouseButtonUpEvent; consumed in Tick
    TWeakObjectPtr<UEdGraphNode> PendingDropTarget;

    // Hover preview: compatible target pins highlighted while dragging over a node
    TArray<UEdGraphPin*>         HoverPreviewPins;
    TWeakObjectPtr<UEdGraphNode> HoverPreviewNode;

    bool bIsMultiDragging      = false;
    bool bPendingMultiConnect  = false;
    bool bKeepSelectionForCtrl = false;
};
