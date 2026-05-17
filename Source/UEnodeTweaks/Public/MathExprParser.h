#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Set.h"

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------

enum class EMathTokenType : uint8
{
    Number, Ident, Plus, Minus, Star, Slash, Percent,
    Caret, StarStar, LParen, RParen, Comma, End, Bad
};

struct FMathToken
{
    EMathTokenType Type;
    FString        Text;
    double         Number = 0.0;
};

// ---------------------------------------------------------------------------
// AST nodes
// ---------------------------------------------------------------------------

enum class EMathNodeKind : uint8
{
    Number, Variable, Constant,
    BinOp, UnaryMinus, FuncCall
};

struct FMathNode
{
    EMathNodeKind          Kind;
    double                 NumValue  = 0.0;
    FString                Name;       // variable / constant / function name
    EMathTokenType         Op = EMathTokenType::Bad;
    TArray<TSharedPtr<FMathNode>> Children;
};

using FMathNodePtr = TSharedPtr<FMathNode>;

// ---------------------------------------------------------------------------
// Parse result
// ---------------------------------------------------------------------------

struct FMathParseResult
{
    FMathNodePtr Root;      // null on failure
    FString      Error;
    TSet<FString> Variables; // sorted unique variable names (excluding constants)
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

class UENODETWEAKS_API FMathExprParser
{
public:
    static FMathParseResult Parse(const FString& Expression);

    /** Known constants – not treated as variables */
    static bool IsConstant(const FString& Name);
    /** Known functions */
    static bool IsFunction(const FString& Name);
};
