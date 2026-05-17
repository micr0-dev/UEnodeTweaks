#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "MathExpressionNode.generated.h"

/**
 * A pure Blueprint node containing an inline math expression.
 * Variable names in the expression become float input pins.
 * One float output pin ("Result") is produced.
 */
UCLASS(MinimalAPI)
class UK2Node_MathExpr : public UK2Node
{
    GENERATED_BODY()

public:
    UK2Node_MathExpr();

    /** The expression string edited directly on the node body. */
    UPROPERTY()
    FString Expression;

    /** Last parse error; empty if expression is valid. */
    UPROPERTY(Transient)
    FString ParseError;

    // ---- UK2Node interface -------------------------------------------------
    virtual void AllocateDefaultPins() override;
    virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
    virtual bool IsNodePure() const override { return true; }
    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FText GetMenuCategory() const override;
    virtual FText GetKeywords() const override;
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    // ---- Helpers -----------------------------------------------------------

    /** Re-parse the expression, refresh pins, and update ParseError. */
    void RebuildPins();

    UEdGraphPin* GetResultPin() const;
};
