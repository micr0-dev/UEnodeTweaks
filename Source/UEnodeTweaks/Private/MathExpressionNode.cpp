#include "MathExpressionNode.h"
#include "MathExprParser.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetCompiler.h"
#include "SGraphActionMenu.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace MathExprHelpers
{
    static const FName ResultPinName(TEXT("Result"));

    static double ConstantValue(const FString& Name)
    {
        if (Name == TEXT("pi"))  return UE_PI;
        if (Name == TEXT("tau")) return UE_PI * 2.0;
        if (Name == TEXT("e"))   return 2.718281828459045;
        if (Name == TEXT("deg")) return 180.0 / UE_PI;
        if (Name == TEXT("rad")) return UE_PI / 180.0;
        return 0.0;
    }

    static UK2Node_CallFunction* SpawnCall(FKismetCompilerContext& Ctx, UEdGraph* Graph,
                                           UFunction* Func, UK2Node* SourceNode)
    {
        UK2Node_CallFunction* Node = Ctx.SpawnIntermediateNode<UK2Node_CallFunction>(SourceNode, Graph);
        Node->SetFromFunction(Func);
        Node->AllocateDefaultPins();
        return Node;
    }

    /** Spawn MakeLiteralFloat and return its return pin. Sets default value via the Value input pin. */
    static UEdGraphPin* SpawnLiteralFloat(FKismetCompilerContext& Ctx, UEdGraph* Graph,
                                          double Value, UK2Node* SourceNode)
    {
        UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeLiteralFloat"));
        if (!Func) return nullptr;

        UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);
        UEdGraphPin* ValPin = Node->FindPin(TEXT("Value"));
        if (ValPin)
            ValPin->DefaultValue = FString::SanitizeFloat((float)Value);
        return Node->GetReturnValuePin();
    }

    // Forward declaration
    static UEdGraphPin* EmitNode(const FMathNodePtr& Ast, FKismetCompilerContext& Ctx,
                                 UEdGraph* Graph, UK2Node* SourceNode,
                                 const TMap<FString, UEdGraphPin*>& VarPins);

    static UEdGraphPin* EmitBinOp(const FMathNodePtr& Ast, FKismetCompilerContext& Ctx,
                                  UEdGraph* Graph, UK2Node* SourceNode,
                                  const TMap<FString, UEdGraphPin*>& VarPins)
    {
        UEdGraphPin* L = EmitNode(Ast->Children[0], Ctx, Graph, SourceNode, VarPins);
        UEdGraphPin* R = EmitNode(Ast->Children[1], Ctx, Graph, SourceNode, VarPins);
        if (!L || !R) return nullptr;

        // Percent: FMod
        if (Ast->Op == EMathTokenType::Percent)
        {
            UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("FMod"));
            if (!Func) return nullptr;
            UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);
            UEdGraphPin* DivPin    = Node->FindPin(TEXT("Dividend"));
            UEdGraphPin* DivisorPin = Node->FindPin(TEXT("Divisor"));
            if (DivPin)    L->MakeLinkTo(DivPin);
            if (DivisorPin) R->MakeLinkTo(DivisorPin);
            return Node->GetReturnValuePin();
        }

        // Power: ^  or  **
        if (Ast->Op == EMathTokenType::Caret || Ast->Op == EMathTokenType::StarStar)
        {
            UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MultiplyMultiply_FloatFloat"));
            if (!Func) return nullptr;
            UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);
            UEdGraphPin* Base = Node->FindPin(TEXT("Base"));
            UEdGraphPin* Exp  = Node->FindPin(TEXT("Exp"));
            if (Base) L->MakeLinkTo(Base);
            if (Exp)  R->MakeLinkTo(Exp);
            return Node->GetReturnValuePin();
        }

        FName FuncName;
        switch (Ast->Op)
        {
        case EMathTokenType::Plus:  FuncName = TEXT("Add_FloatFloat");      break;
        case EMathTokenType::Minus: FuncName = TEXT("Subtract_FloatFloat"); break;
        case EMathTokenType::Star:  FuncName = TEXT("Multiply_FloatFloat"); break;
        case EMathTokenType::Slash: FuncName = TEXT("Divide_FloatFloat");   break;
        default: return nullptr;
        }

        UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(FuncName);
        if (!Func) return nullptr;
        UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);

        UEdGraphPin* PA = Node->FindPin(TEXT("A"));
        UEdGraphPin* PB = Node->FindPin(TEXT("B"));
        if (PA) L->MakeLinkTo(PA);
        if (PB) R->MakeLinkTo(PB);
        return Node->GetReturnValuePin();
    }

    static UEdGraphPin* EmitFuncCall(const FMathNodePtr& Ast, FKismetCompilerContext& Ctx,
                                     UEdGraph* Graph, UK2Node* SourceNode,
                                     const TMap<FString, UEdGraphPin*>& VarPins)
    {
        static const TMap<FString, FString> kFuncMap = {
            { TEXT("sin"),   TEXT("Sin")                     },
            { TEXT("cos"),   TEXT("Cos")                     },
            { TEXT("tan"),   TEXT("Tan")                     },
            { TEXT("asin"),  TEXT("Asin")                    },
            { TEXT("acos"),  TEXT("Acos")                    },
            { TEXT("atan"),  TEXT("Atan")                    },
            { TEXT("atan2"), TEXT("Atan2_FloatFloat")        },
            { TEXT("sqrt"),  TEXT("Sqrt")                    },
            { TEXT("pow"),   TEXT("MultiplyMultiply_FloatFloat") },
            { TEXT("abs"),   TEXT("Abs_Float")               },
            { TEXT("min"),   TEXT("FMin")                    },
            { TEXT("max"),   TEXT("FMax")                    },
            { TEXT("clamp"), TEXT("FClamp")                  },
            { TEXT("lerp"),  TEXT("Lerp_FloatFloat")         },
            { TEXT("floor"), TEXT("FFloor")                  },
            { TEXT("ceil"),  TEXT("FCeil")                   },
            { TEXT("round"), TEXT("FRoundToFloat")           },
            { TEXT("sign"),  TEXT("SignOfFloat")             },
            { TEXT("exp"),   TEXT("Exp")                     },
            { TEXT("log"),   TEXT("Log_Float")               },
            { TEXT("ln"),    TEXT("Loge")                    },
            { TEXT("mod"),   TEXT("FMod")                    },
        };

        const FString& Name = Ast->Name;
        const FString* MappedName = kFuncMap.Find(Name);
        if (!MappedName) return nullptr;

        UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(**MappedName);
        if (!Func) return nullptr;

        UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);

        // Collect non-exec, non-self input pins positionally
        TArray<UEdGraphPin*> InputPins;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Input &&
                Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
                Pin->PinName != TEXT("self"))
            {
                InputPins.Add(Pin);
            }
        }

        for (int32 i = 0; i < Ast->Children.Num() && i < InputPins.Num(); ++i)
        {
            UEdGraphPin* ArgPin = EmitNode(Ast->Children[i], Ctx, Graph, SourceNode, VarPins);
            if (ArgPin) ArgPin->MakeLinkTo(InputPins[i]);
        }

        UEdGraphPin* RetPin = Node->GetReturnValuePin();

        // floor / ceil return int32 → convert to float
        if (Name == TEXT("floor") || Name == TEXT("ceil"))
        {
            UFunction* Conv = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToFloat"));
            if (Conv && RetPin)
            {
                UK2Node_CallFunction* ConvNode = SpawnCall(Ctx, Graph, Conv, SourceNode);
                UEdGraphPin* IntIn = ConvNode->FindPin(TEXT("InInt"));
                if (!IntIn) IntIn = ConvNode->FindPin(TEXT("Int")); // alternate name
                if (IntIn) RetPin->MakeLinkTo(IntIn);
                return ConvNode->GetReturnValuePin();
            }
        }

        return RetPin;
    }

    static UEdGraphPin* EmitNode(const FMathNodePtr& Ast, FKismetCompilerContext& Ctx,
                                 UEdGraph* Graph, UK2Node* SourceNode,
                                 const TMap<FString, UEdGraphPin*>& VarPins)
    {
        if (!Ast.IsValid()) return nullptr;

        switch (Ast->Kind)
        {
        case EMathNodeKind::Number:
            return SpawnLiteralFloat(Ctx, Graph, Ast->NumValue, SourceNode);

        case EMathNodeKind::Constant:
            return SpawnLiteralFloat(Ctx, Graph, ConstantValue(Ast->Name), SourceNode);

        case EMathNodeKind::Variable:
        {
            const UEdGraphPin* const* P = VarPins.Find(Ast->Name);
            return P ? const_cast<UEdGraphPin*>(*P) : nullptr;
        }

        case EMathNodeKind::BinOp:
            return EmitBinOp(Ast, Ctx, Graph, SourceNode, VarPins);

        case EMathNodeKind::FuncCall:
            return EmitFuncCall(Ast, Ctx, Graph, SourceNode, VarPins);

        case EMathNodeKind::UnaryMinus:
        {
            UEdGraphPin* Operand = EmitNode(Ast->Children[0], Ctx, Graph, SourceNode, VarPins);
            if (!Operand) return nullptr;
            UFunction* Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("NegateFloat"));
            if (!Func) Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Negate_Float"));
            if (!Func)
            {
                // Fallback: multiply by -1
                Func = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Multiply_FloatFloat"));
                if (!Func) return nullptr;
                UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);
                UEdGraphPin* PA = Node->FindPin(TEXT("A"));
                UEdGraphPin* PB = Node->FindPin(TEXT("B"));
                if (PA) Operand->MakeLinkTo(PA);
                if (PB) PB->DefaultValue = TEXT("-1.0");
                return Node->GetReturnValuePin();
            }
            UK2Node_CallFunction* Node = SpawnCall(Ctx, Graph, Func, SourceNode);
            UEdGraphPin* In = Node->Pins.Num() > 1 ? Node->Pins[1] : nullptr;
            if (In && In->Direction == EGPD_Input) Operand->MakeLinkTo(In);
            return Node->GetReturnValuePin();
        }

        default: return nullptr;
        }
    }
} // namespace MathExprHelpers

// ---------------------------------------------------------------------------
// UK2Node_MathExpr
// ---------------------------------------------------------------------------

UK2Node_MathExpr::UK2Node_MathExpr()
{
    Expression = TEXT("x");
}

void UK2Node_MathExpr::AllocateDefaultPins()
{
    FMathParseResult Result = FMathExprParser::Parse(Expression);
    ParseError = Result.Error;

    if (Result.Root.IsValid())
    {
        TArray<FString> SortedVars = Result.Variables.Array();
        SortedVars.Sort();

        for (const FString& VarName : SortedVars)
        {
            UEdGraphPin* Pin = CreatePin(EGPD_Input,
                UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, *VarName);
            Pin->PinFriendlyName = FText::FromString(VarName);
        }
    }

    UEdGraphPin* Out = CreatePin(EGPD_Output,
        UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double,
        MathExprHelpers::ResultPinName);
    Out->PinFriendlyName = NSLOCTEXT("MathExpr", "ResultPin", "Result");
}

void UK2Node_MathExpr::RebuildPins()
{
    // Re-parse live for error state (no graph ops – just update ParseError)
    FMathParseResult Result = FMathExprParser::Parse(Expression);
    ParseError = Result.Error;

    // Trigger full UE pin reconstruction (calls AllocateDefaultPins, restores links)
    ReconstructNode();
}

UEdGraphPin* UK2Node_MathExpr::GetResultPin() const
{
    return FindPin(MathExprHelpers::ResultPinName, EGPD_Output);
}

void UK2Node_MathExpr::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
    using namespace MathExprHelpers;

    FMathParseResult Result = FMathExprParser::Parse(Expression);
    if (!Result.Root.IsValid())
    {
        CompilerContext.MessageLog.Error(
            *FString::Printf(TEXT("@@: Math Expression parse error: %s"), *Result.Error), this);
        BreakAllNodeLinks();
        return;
    }

    UEdGraphPin* MyResult = GetResultPin();
    if (!MyResult)
    {
        BreakAllNodeLinks();
        return;
    }

    // --- 1. Spawn a knot (reroute) node per variable so that:
    //         external_source → knot_in, knot_out → wherever var is used in AST
    //         This correctly handles the same variable used multiple times.
    TMap<FString, UK2Node_Knot*> KnotNodes;
    TMap<FString, UEdGraphPin*>  VarPins;   // var name → knot output pin

    TArray<FString> SortedVars = Result.Variables.Array();
    SortedVars.Sort();

    for (const FString& VarName : SortedVars)
    {
        UK2Node_Knot* Knot = CompilerContext.SpawnIntermediateNode<UK2Node_Knot>(this, SourceGraph);
        Knot->AllocateDefaultPins();
        KnotNodes.Add(VarName, Knot);
        VarPins.Add(VarName, Knot->GetOutputPin());
    }

    // --- 2. Emit the AST graph
    UEdGraphPin* EmittedOut = EmitNode(Result.Root, CompilerContext, SourceGraph, this, VarPins);
    if (!EmittedOut)
    {
        CompilerContext.MessageLog.Error(TEXT("@@: Math Expression failed to emit graph nodes"), this);
        BreakAllNodeLinks();
        return;
    }

    // --- 3. Redirect external variable connections → knot inputs
    for (UEdGraphPin* Pin : Pins)
    {
        if (Pin->Direction != EGPD_Input) continue;
        FString VarName = Pin->PinName.ToString().ToLower();
        UK2Node_Knot** Knot = KnotNodes.Find(VarName);
        if (Knot && (*Knot)->GetInputPin())
            CompilerContext.MovePinLinksToIntermediate(*Pin, *(*Knot)->GetInputPin());
    }

    // --- 4. Redirect result connections → emitted output
    CompilerContext.MovePinLinksToIntermediate(*MyResult, *EmittedOut);

    BreakAllNodeLinks();
}

FText UK2Node_MathExpr::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    if (TitleType == ENodeTitleType::MenuTitle)
        return NSLOCTEXT("MathExpr", "MenuTitle", "f(x)");
    if (Expression.IsEmpty())
        return NSLOCTEXT("MathExpr", "EmptyTitle", "f(x)");
    return FText::FromString(Expression);
}

FText UK2Node_MathExpr::GetTooltipText() const
{
    if (!ParseError.IsEmpty())
        return FText::FromString(FString::Printf(TEXT("Error: %s"), *ParseError));
    return NSLOCTEXT("MathExpr", "Tooltip",
        "Evaluates an inline math expression.\nVariable names become input pins.");
}

FText UK2Node_MathExpr::GetMenuCategory() const
{
    return NSLOCTEXT("MathExpr", "Category", "Math");
}

FText UK2Node_MathExpr::GetKeywords() const
{
    return NSLOCTEXT("MathExpr", "Keywords",
        "f(x) math formula expression calculate "
        "sin cos tan asin acos atan sqrt pow abs min max clamp lerp "
        "floor ceil round sign exp log ln mod + - * / ^ %");
}

void UK2Node_MathExpr::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UClass* ActionKey = GetClass();
    if (ActionRegistrar.IsOpenForRegistration(ActionKey))
    {
        UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

        // Pre-fill the expression from whatever was typed in the search box.
        // SGraphActionMenu::LastUsedFilterText is set during scoring (i.e. as
        // the user types), so it holds the current search string at spawn time.
        Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
            [](UEdGraphNode* NewNode, bool bIsTemplateNode)
            {
                if (bIsTemplateNode) return;
                UK2Node_MathExpr* MathNode = CastChecked<UK2Node_MathExpr>(NewNode);

                FString SearchText = SGraphActionMenu::LastUsedFilterText.TrimStartAndEnd();
                FMathParseResult Result = FMathExprParser::Parse(SearchText);
                if (Result.Root.IsValid())
                {
                    MathNode->Expression = SearchText;
                    MathNode->ParseError.Empty();
                    MathNode->ReconstructNode(); // rebuild pins for the pre-filled expression
                }
            });

        ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
    }
}

void UK2Node_MathExpr::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (PropertyChangedEvent.Property &&
        PropertyChangedEvent.Property->GetFName() ==
            GET_MEMBER_NAME_CHECKED(UK2Node_MathExpr, Expression))
    {
        RebuildPins();
    }
}
