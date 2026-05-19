#include "UEnodeTweaksModule.h"
#include "MultiConnectPreprocessor.h"
#include "MultiPinDragPreprocessor.h"
#include "SmartArrangePreprocessor.h"
#include "OrthogonalConnectionDrawingPolicy.h"
#include "NodeTweaksSettings.h"
#include "MathExpressionNode.h"
#include "SMathExpressionNode.h"

#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"
#include "EdGraphUtilities.h"
#include "ConnectionDrawingPolicy.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"

// ---------------------------------------------------------------------------
// Node factory – creates the custom widget for UK2Node_MathExpr
// ---------------------------------------------------------------------------

struct FMathExprNodeFactory : public FGraphPanelNodeFactory
{
    virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
    {
        if (UK2Node_MathExpr* MathNode = Cast<UK2Node_MathExpr>(Node))
        {
            return SNew(SMathExpressionNode, MathNode);
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Connection factory – activated when orthogonal wires or wire bridges are on
// ---------------------------------------------------------------------------

struct FOrthogonalConnectionFactory : public FGraphPanelPinConnectionFactory
{
    virtual FConnectionDrawingPolicy* CreateConnectionPolicy(
        const UEdGraphSchema*     Schema,
        int32                     InBackLayerID,
        int32                     InFrontLayerID,
        float                     ZoomFactor,
        const FSlateRect&         InClippingRect,
        FSlateWindowElementList&  InDrawElements,
        UEdGraph*                 InGraphObj) const override
    {
        const UNodeTweaksSettings* S = GetDefault<UNodeTweaksSettings>();
        if (!S->bOrthogonalWires && !S->bWireBridges)
            return nullptr;

        if (Schema->IsA<UEdGraphSchema_K2>())
        {
            return new FOrthogonalKismetConnectionDrawingPolicy(
                InBackLayerID, InFrontLayerID,
                ZoomFactor, InClippingRect,
                InDrawElements, InGraphObj);
        }

        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

void FUEnodeTweaksModule::StartupModule()
{
    if (FSlateApplication::IsInitialized())
    {
        MultiConnectProcessor = MakeShared<FMultiConnectPreprocessor>();
        MultiPinDragProcessor = MakeShared<FMultiPinDragPreprocessor>();
        // MultiPinDrag runs first (priority 0) so it can block MultiConnect
        // (priority 1) when a multi-pin selection is active, preventing
        // duplicate reconnect handling on the same drop event.
        FSlateApplication::Get().RegisterInputPreProcessor(MultiPinDragProcessor, 0);
        FSlateApplication::Get().RegisterInputPreProcessor(MultiConnectProcessor, 1);
    }

    SmartArrangeProcessor = MakeShared<FSmartArrangePreprocessor>();
    FSlateApplication::Get().RegisterInputPreProcessor(SmartArrangeProcessor, 0);

    ConnectionFactory = MakeShared<FOrthogonalConnectionFactory>();
    FEdGraphUtilities::RegisterVisualPinConnectionFactory(ConnectionFactory);

    NodeFactory = MakeShared<FMathExprNodeFactory>();
    FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
}

void FUEnodeTweaksModule::ShutdownModule()
{
    if (NodeFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
        NodeFactory.Reset();
    }

    if (ConnectionFactory.IsValid())
    {
        FEdGraphUtilities::UnregisterVisualPinConnectionFactory(ConnectionFactory);
        ConnectionFactory.Reset();
    }

    if (FSlateApplication::IsInitialized())
    {
        if (SmartArrangeProcessor.IsValid())
            FSlateApplication::Get().UnregisterInputPreProcessor(SmartArrangeProcessor);
        if (MultiConnectProcessor.IsValid())
            FSlateApplication::Get().UnregisterInputPreProcessor(MultiConnectProcessor);
        if (MultiPinDragProcessor.IsValid())
            FSlateApplication::Get().UnregisterInputPreProcessor(MultiPinDragProcessor);
    }
    SmartArrangeProcessor.Reset();
    MultiConnectProcessor.Reset();
    MultiPinDragProcessor.Reset();
}

IMPLEMENT_MODULE(FUEnodeTweaksModule, UEnodeTweaks)
