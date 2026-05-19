#include "SMathExpressionNode.h"
#include "MathExpressionNode.h"
#include "MathExprParser.h"

#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"

void SMathExpressionNode::Construct(const FArguments& InArgs, UK2Node_MathExpr* InNode)
{
    MathNode = InNode;
    GraphNode = InNode;
    SetCursor(EMouseCursor::CardinalCross);
    UpdateGraphNode();
}

void SMathExpressionNode::UpdateGraphNode()
{
    // Use the default layout, then CreateBelowPinControls adds our text field
    SGraphNode::UpdateGraphNode();
}

void SMathExpressionNode::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
    if (!MathNode) return;

    MainBox->AddSlot()
    .AutoHeight()
    .Padding(4.0f, 2.0f)
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .BorderBackgroundColor(this, &SMathExpressionNode::GetBorderColor)
        [
            SAssignNew(ExpressionText, SEditableText)
            .Text(FText::FromString(MathNode->Expression))
            .HintText(NSLOCTEXT("MathExpr", "Hint", "e.g. x * 2 + sin(y)"))
            .OnTextChanged(this, &SMathExpressionNode::OnExpressionTextChanged)
            .OnTextCommitted(this, &SMathExpressionNode::OnExpressionTextCommitted)
            .SelectAllTextWhenFocused(true)
            .RevertTextOnEscape(true)
            .MinDesiredWidth(120.0f)
            .ToolTipText_Lambda([this]() -> FText
            {
                if (MathNode && !MathNode->ParseError.IsEmpty())
                    return FText::FromString(FString::Printf(TEXT("Error: %s"), *MathNode->ParseError));
                return NSLOCTEXT("MathExpr", "InputHint", "Math expression — variables become input pins");
            })
        ]
    ];

    // Auto-focus the text field when the node is freshly placed (empty expression)
    if (MathNode->Expression.IsEmpty() && ExpressionText.IsValid())
    {
        FSlateApplication::Get().SetKeyboardFocus(ExpressionText, EFocusCause::SetDirectly);
    }
}

FSlateColor SMathExpressionNode::GetBorderColor() const
{
    if (MathNode && !MathNode->ParseError.IsEmpty())
        return FLinearColor(1.0f, 0.2f, 0.2f, 1.0f); // red on error
    return FLinearColor(0.2f, 0.8f, 0.2f, 0.5f);     // subtle green when valid
}

void SMathExpressionNode::OnExpressionTextChanged(const FText& NewText)
{
    if (!MathNode) return;

    // Live parse for error highlighting – don't commit to the graph yet
    FString Str = NewText.ToString();
    FMathParseResult Result = FMathExprParser::Parse(Str);
    MathNode->ParseError = Result.Error;

    // Force a visual refresh
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SMathExpressionNode::OnExpressionTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
    if (!MathNode) return;

    FString NewExpr = NewText.ToString().TrimStartAndEnd();
    if (NewExpr == MathNode->Expression) return;

    // Commit with transaction so it can be undone
    const FScopedTransaction Transaction(NSLOCTEXT("MathExpr", "EditExpr", "Edit Math Expression"));
    MathNode->Modify();
    MathNode->Expression = NewExpr;
    MathNode->RebuildPins();

    // Notify the graph that pins changed, and mark the Blueprint dirty for recompilation
    MathNode->GetGraph()->NotifyGraphChanged();
    if (UBlueprint* BP = MathNode->GetBlueprint())
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
}
