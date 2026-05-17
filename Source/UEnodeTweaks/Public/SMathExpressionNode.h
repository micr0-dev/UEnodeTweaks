#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UK2Node_MathExpr;

/**
 * Custom SGraphNode for UK2Node_MathExpr.
 * Adds an editable single-line text field to the node body that
 * live-validates the expression and shows a red border on error.
 */
class SMathExpressionNode : public SGraphNode
{
public:
    SLATE_BEGIN_ARGS(SMathExpressionNode) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, UK2Node_MathExpr* InNode);

    // SGraphNode interface
    virtual void UpdateGraphNode() override;
    virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
    FSlateColor GetBorderColor() const;

private:
    UK2Node_MathExpr*         MathNode = nullptr;
    TSharedPtr<SEditableText> ExpressionText;

    void OnExpressionTextChanged(const FText& NewText);
    void OnExpressionTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
};
