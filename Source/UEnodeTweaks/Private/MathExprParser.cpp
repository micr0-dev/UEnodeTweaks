#include "MathExprParser.h"

// ---------------------------------------------------------------------------
// Constants and functions sets
// ---------------------------------------------------------------------------

static const TSet<FString> kConstants = {
    TEXT("pi"), TEXT("tau"), TEXT("e"), TEXT("deg"), TEXT("rad")
};

static const TSet<FString> kFunctions = {
    TEXT("sin"), TEXT("cos"), TEXT("tan"),
    TEXT("asin"), TEXT("acos"), TEXT("atan"), TEXT("atan2"),
    TEXT("sqrt"), TEXT("pow"), TEXT("abs"),
    TEXT("min"), TEXT("max"), TEXT("clamp"), TEXT("lerp"),
    TEXT("floor"), TEXT("ceil"), TEXT("round"), TEXT("sign"),
    TEXT("exp"), TEXT("log"), TEXT("ln"), TEXT("mod")
};

bool FMathExprParser::IsConstant(const FString& Name) { return kConstants.Contains(Name); }
bool FMathExprParser::IsFunction(const FString& Name) { return kFunctions.Contains(Name); }

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

struct FLexer
{
    const TCHAR* Src;
    int32        Pos = 0;
    int32        Len = 0;

    explicit FLexer(const FString& S) : Src(*S), Len(S.Len()) {}

    TCHAR Peek() const { return Pos < Len ? Src[Pos] : 0; }
    TCHAR Advance() { return Src[Pos++]; }

    void SkipWhitespace()
    {
        while (Pos < Len && FChar::IsWhitespace(Src[Pos])) ++Pos;
    }

    FMathToken Next()
    {
        SkipWhitespace();
        if (Pos >= Len) return { EMathTokenType::End };

        TCHAR c = Peek();

        // Number
        if (FChar::IsDigit(c) || (c == '.' && Pos + 1 < Len && FChar::IsDigit(Src[Pos + 1])))
        {
            FString s;
            while (Pos < Len && (FChar::IsDigit(Peek()) || Peek() == '.')) s += Advance();
            if (Pos < Len && (Peek() == 'e' || Peek() == 'E'))
            {
                s += Advance();
                if (Pos < Len && (Peek() == '+' || Peek() == '-')) s += Advance();
                while (Pos < Len && FChar::IsDigit(Peek())) s += Advance();
            }
            FMathToken t; t.Type = EMathTokenType::Number; t.Text = s;
            t.Number = FCString::Atod(*s);
            return t;
        }

        // Identifier / keyword
        if (FChar::IsAlpha(c) || c == '_')
        {
            FString s;
            while (Pos < Len && (FChar::IsAlnum(Peek()) || Peek() == '_')) s += Advance();
            return { EMathTokenType::Ident, s };
        }

        Advance();
        switch (c)
        {
        case '+': return { EMathTokenType::Plus,    TEXT("+") };
        case '-': return { EMathTokenType::Minus,   TEXT("-") };
        case '%': return { EMathTokenType::Percent, TEXT("%") };
        case '^': return { EMathTokenType::Caret,   TEXT("^") };
        case '(': return { EMathTokenType::LParen,  TEXT("(") };
        case ')': return { EMathTokenType::RParen,  TEXT(")") };
        case ',': return { EMathTokenType::Comma,   TEXT(",") };
        case '*':
            if (Pos < Len && Peek() == '*') { Advance(); return { EMathTokenType::StarStar, TEXT("**") }; }
            return { EMathTokenType::Star, TEXT("*") };
        case '/': return { EMathTokenType::Slash,   TEXT("/") };
        default:  return { EMathTokenType::Bad,     FString::Chr(c) };
        }
    }
};

// ---------------------------------------------------------------------------
// Parser (Pratt / recursive descent)
// ---------------------------------------------------------------------------

struct FParser
{
    TArray<FMathToken> Tokens;
    int32              Cur = 0;
    FString            Error;
    TSet<FString>      Variables;

    void Tokenize(const FString& Expr)
    {
        FLexer lex(Expr);
        while (true)
        {
            FMathToken t = lex.Next();
            Tokens.Add(t);
            if (t.Type == EMathTokenType::End || t.Type == EMathTokenType::Bad) break;
        }
    }

    const FMathToken& Peek() const { return Tokens[FMath::Min(Cur, Tokens.Num() - 1)]; }

    FMathToken Consume()
    {
        FMathToken t = Peek();
        if (Cur < Tokens.Num() - 1) ++Cur;
        return t;
    }

    bool Expect(EMathTokenType Type, const TCHAR* Desc)
    {
        if (Peek().Type != Type)
        {
            Error = FString::Printf(TEXT("Expected %s, got '%s'"), Desc, *Peek().Text);
            return false;
        }
        Consume();
        return true;
    }

    // Pratt precedence
    int32 Precedence(EMathTokenType t) const
    {
        switch (t)
        {
        case EMathTokenType::Plus:
        case EMathTokenType::Minus:   return 1;
        case EMathTokenType::Star:
        case EMathTokenType::Slash:
        case EMathTokenType::Percent: return 2;
        case EMathTokenType::Caret:
        case EMathTokenType::StarStar: return 3; // right-assoc handled in ParseBinary
        default: return 0;
        }
    }

    FMathNodePtr ParseExpr(int32 MinPrec = 0)
    {
        FMathNodePtr Left = ParseUnary();
        if (!Left) return nullptr;

        while (true)
        {
            int32 Prec = Precedence(Peek().Type);
            if (Prec <= MinPrec) break;

            FMathToken Op = Consume();
            // Right-associative for ^ and **
            bool bRightAssoc = (Op.Type == EMathTokenType::Caret || Op.Type == EMathTokenType::StarStar);
            FMathNodePtr Right = ParseExpr(bRightAssoc ? Prec - 1 : Prec);
            if (!Right) return nullptr;

            FMathNodePtr Node = MakeShared<FMathNode>();
            Node->Kind = EMathNodeKind::BinOp;
            Node->Op = Op.Type;
            Node->Children.Add(Left);
            Node->Children.Add(Right);
            Left = Node;
        }
        return Left;
    }

    FMathNodePtr ParseUnary()
    {
        if (Peek().Type == EMathTokenType::Minus)
        {
            Consume();
            FMathNodePtr Operand = ParseUnary();
            if (!Operand) return nullptr;
            FMathNodePtr Node = MakeShared<FMathNode>();
            Node->Kind = EMathNodeKind::UnaryMinus;
            Node->Children.Add(Operand);
            return Node;
        }
        return ParsePrimary();
    }

    FMathNodePtr ParsePrimary()
    {
        const FMathToken& T = Peek();

        if (T.Type == EMathTokenType::Number)
        {
            Consume();
            FMathNodePtr N = MakeShared<FMathNode>();
            N->Kind = EMathNodeKind::Number;
            N->NumValue = T.Number;
            return N;
        }

        if (T.Type == EMathTokenType::Ident)
        {
            FString Name = T.Text.ToLower();
            Consume();

            // Function call
            if (Peek().Type == EMathTokenType::LParen && kFunctions.Contains(Name))
            {
                Consume(); // (
                FMathNodePtr Node = MakeShared<FMathNode>();
                Node->Kind = EMathNodeKind::FuncCall;
                Node->Name = Name;

                if (Peek().Type != EMathTokenType::RParen)
                {
                    do
                    {
                        FMathNodePtr Arg = ParseExpr();
                        if (!Arg) return nullptr;
                        Node->Children.Add(Arg);
                        if (Peek().Type == EMathTokenType::Comma) Consume();
                        else break;
                    } while (true);
                }
                if (!Expect(EMathTokenType::RParen, TEXT(")"))) return nullptr;
                return Node;
            }

            // Constant
            if (kConstants.Contains(Name))
            {
                FMathNodePtr N = MakeShared<FMathNode>();
                N->Kind = EMathNodeKind::Constant;
                N->Name = Name;
                return N;
            }

            // Unknown function-like call → error
            if (Peek().Type == EMathTokenType::LParen)
            {
                Error = FString::Printf(TEXT("Unknown function '%s'"), *T.Text);
                return nullptr;
            }

            // Variable
            Variables.Add(Name);
            FMathNodePtr N = MakeShared<FMathNode>();
            N->Kind = EMathNodeKind::Variable;
            N->Name = Name;
            return N;
        }

        if (T.Type == EMathTokenType::LParen)
        {
            Consume();
            FMathNodePtr Inner = ParseExpr();
            if (!Inner) return nullptr;
            if (!Expect(EMathTokenType::RParen, TEXT(")"))) return nullptr;
            return Inner;
        }

        Error = FString::Printf(TEXT("Unexpected token '%s'"), *T.Text);
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

FMathParseResult FMathExprParser::Parse(const FString& Expression)
{
    FMathParseResult Result;

    FString Trimmed = Expression.TrimStartAndEnd();
    if (Trimmed.IsEmpty())
    {
        Result.Error = TEXT("Empty expression");
        return Result;
    }

    FParser P;
    P.Tokenize(Trimmed);

    // Check for Bad token
    for (const FMathToken& T : P.Tokens)
    {
        if (T.Type == EMathTokenType::Bad)
        {
            Result.Error = FString::Printf(TEXT("Unexpected character '%s'"), *T.Text);
            return Result;
        }
    }

    Result.Root = P.ParseExpr();
    Result.Error = P.Error;
    Result.Variables = P.Variables;

    if (!Result.Root && Result.Error.IsEmpty())
        Result.Error = TEXT("Empty expression");

    // Verify full consumption
    if (Result.Root && P.Peek().Type != EMathTokenType::End)
    {
        Result.Error = FString::Printf(TEXT("Unexpected token '%s'"), *P.Peek().Text);
        Result.Root.Reset();
    }

    return Result;
}
