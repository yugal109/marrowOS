#include <stdlib.h>
#include <string.h>

#include "compiler_internal.h"
#include "memory.h"
#include "vm.h"

static void binary(bool canAssign);
static void unary(bool canAssign);
static void number(bool canAssign);
static void string(bool canAssign);
static void grouping(bool canAssign);
static void array(bool canAssign);
static void map(bool canAssign);
static void arrayIndex(bool canAssign);
static void arrow(bool canAssign);
static void call(bool canAssign);
static void dot(bool canAssign);
static void and_(bool canAssign);
static void or_(bool canAssign);
static void literal(bool canAssign);
void variable(bool canAssign);
static void super_(bool canAssign);
static void this_(bool canAssign);
static void moduleAccess(bool canAssign);
static void anonymousFunction(bool canAssign);
static uint8_t argumentList(void);
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign)
{
    (void)canAssign;
    TokenType operatorType = parser.previous.type;

    // Compile the right operand.
    ParseRule *rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType)
    {
    case TOKEN_BANG_EQUAL:
        emitBytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emitByte(OP_EQUAL);
        break;
    case TOKEN_GREATER:
        emitByte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL:
        emitBytes(OP_LESS, OP_NOT);
        break;
    case TOKEN_LESS:
        emitByte(OP_LESS);
        break;
    case TOKEN_LESS_EQUAL:
        emitBytes(OP_GREATER, OP_NOT);
        break;
    case TOKEN_PLUS:
        emitByte(OP_ADD);
        break;
    case TOKEN_MINUS:
        emitByte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emitByte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emitByte(OP_DIVIDE);
        break;
    default:
        return; // Unreachable.
    }
}

static void call(bool canAssign)
{
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign)
{
    consume(TOKEN_IDENTIFIER, "Expect property name after '.' .");
    uint16_t name = identifierConstant(&parser.previous);
    if (canAssign && match(TOKEN_EQUAL))
    {
        expression();
        emitByte(OP_SET_PROPERTY);
        emitIndex(name);
    }
    else if (match(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = argumentList();
        emitByte(OP_INVOKE);
        emitIndex(name);
        emitByte(argCount);
    }
    else
    {

        emitByte(OP_GET_PROPERTY);
        emitIndex(name);
    }
}

static void dotSafe(bool canAssign)
{
    (void)canAssign;
    consume(TOKEN_IDENTIFIER, "Expect property name after '?.'.");
    uint16_t name = identifierConstant(&parser.previous);
    emitByte(OP_GET_PROPERTY_SAFE);
    emitIndex(name);
}

void expression(void)
{
    parsePrecedence(PREC_ASSIGNMENT);
}

static void anonymousFunction(bool canAssign)
{
    (void)canAssign;
    function(TYPE_FUNCTION);
}

static uint8_t argumentList(void)
{
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {
            expression();
            if (argCount == 255)
            {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void and_(bool canAssign)
{
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign)
{
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void grouping(bool canAssign)
{
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign)
{
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL); // convert lexeme to double
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign)
{
    (void)canAssign;
    const char *src = parser.previous.start + 1;
    int len = parser.previous.length - 2;

    Scanner savedScanner = scanner;
    Token savedCurrent = parser.current;
    Token savedPrevious = parser.previous;

    char *buffer = (char *)malloc(len + 1);
    int outLen = 0;
    bool didInterp = false;

    int i = 0;
    while (i < len)
    {
        if (src[i] == '\\' && i + 1 < len && src[i + 1] == '#')
        {
            buffer[outLen++] = '#';
            i += 2;
            if (i < len && src[i] == '{')
            {
                buffer[outLen++] = '{';
                i++;
            }
        }
        else if (src[i] == '#' && i + 1 < len && src[i + 1] == '{')
        {
            ObjString *part = copyString(buffer, outLen);
            push(OBJ_VAL(part));
            emitConstant(OBJ_VAL(part));
            pop();
            outLen = 0;

            if (didInterp)
                emitByte(OP_ADD);

            i += 2;

            int exprStart = i;
            int depth = 1;
            while (i < len && depth > 0)
            {
                if (src[i] == '"')
                {
                    i++;
                    while (i < len && src[i] != '"')
                    {
                        if (src[i] == '\\')
                            i++;
                        i++;
                    }
                    if (i < len)
                        i++;
                    continue;
                }
                if (src[i] == '{')
                    depth++;
                else if (src[i] == '}')
                    depth--;
                if (depth > 0)
                    i++;
            }

            if (depth != 0)
            {
                error("Unterminated string interpolation, expected '}'.");
                free(buffer);
                return;
            }

            int exprLen = i - exprStart;
            i++;

            char *exprBuf = (char *)malloc(exprLen + 2);
            memcpy(exprBuf, src + exprStart, exprLen);
            exprBuf[exprLen] = ';';
            exprBuf[exprLen + 1] = '\0';

            Scanner exprScanner;
            exprScanner.start = exprBuf;
            exprScanner.current = exprBuf;
            exprScanner.line = parser.previous.line;
            scanner = exprScanner;

            advance();
            expression();

            free(exprBuf);

            scanner = savedScanner;
            parser.current = savedCurrent;
            parser.previous = savedPrevious;

            emitByte(OP_ADD);
            didInterp = true;
        }
        else if (src[i] == '\\' && i + 1 < len)
        {
            i++;
            switch (src[i])
            {
            case 'n':
                buffer[outLen++] = '\n';
                break;
            case 't':
                buffer[outLen++] = '\t';
                break;
            case 'r':
                buffer[outLen++] = '\r';
                break;
            case '\\':
                buffer[outLen++] = '\\';
                break;
            case '"':
                buffer[outLen++] = '"';
                break;
            case '0':
                buffer[outLen++] = '\0';
                break;
            default:
                buffer[outLen++] = '\\';
                buffer[outLen++] = src[i];
                break;
            }
            i++;
        }
        else
        {
            buffer[outLen++] = src[i++];
        }
    }

    ObjString *rest = copyString(buffer, outLen);
    push(OBJ_VAL(rest));
    emitConstant(OBJ_VAL(rest));
    pop();

    if (didInterp)
        emitByte(OP_ADD);

    free(buffer);
}

void namedVariable(Token name, bool canAssign)
{
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);

    if (arg != -1)
    {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else if ((arg = resolveUpvalue(current, &name)) != -1)
    {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    }
    else
    {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        expression();
        if (setOp == OP_SET_GLOBAL)
        {
            emitByte(setOp);
            emitIndex((uint16_t)arg);
        }
        else
        {
            emitBytes(setOp, (uint8_t)arg);
        }
    }
    else
    {
        if (getOp == OP_GET_GLOBAL)
        {
            emitByte(getOp);
            emitIndex((uint16_t)arg);
        }
        else
        {
            emitBytes(getOp, (uint8_t)arg);
        }
    }
}

void variable(bool canAssign)
{
    namedVariable(parser.previous, canAssign);
}

static void super_(bool canAssign)
{
    (void)canAssign;
    if (currentClass == NULL)
    {
        error("can't user 'super' outside of a class.");
    }
    else if (!currentClass->hasSuperclass)
    {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint16_t name = identifierConstant(&parser.previous);

    namedVariable(syntheticToken("this"), false);

    if (match(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = argumentList();
        namedVariable(syntheticToken("super"), false);
        emitByte(OP_SUPER_INVOKE);
        emitIndex(name);
        emitByte(argCount);
    }
    else
    {
        namedVariable(syntheticToken("super"), false);
        emitByte(OP_GET_SUPER);
        emitIndex(name);
    }
}

static void this_(bool canAssign)
{
    (void)canAssign;
    if (currentClass == NULL)
    {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

static void unary(bool canAssign)
{
    (void)canAssign;
    TokenType operatorType = parser.previous.type;

    parsePrecedence(PREC_UNARY);

    switch (operatorType)
    {

    case TOKEN_BANG:
        emitByte(OP_NOT);
        break;
    case TOKEN_MINUS:
    {
        emitByte(OP_NEGATE);
        break;
    }
    default:
        return;
    }
}

static void map(bool canAssign)
{
    (void)canAssign;
    int count = 0;

    if (!check(TOKEN_RIGHT_BRACE))
    {
        do
        {
            consume(TOKEN_STRING, "Expect string key in map.");
            // consume() does not advance on failure — don't strip quotes from
            // whatever token is still in previous (e.g. '{' → length-2 < 0).
            if (parser.previous.type != TOKEN_STRING)
                return;

            const char *src = parser.previous.start + 1;
            int len = parser.previous.length - 2;
            ObjString *key = copyString(src, len);
            push(OBJ_VAL(key));
            emitConstant(OBJ_VAL(key));
            pop();

            consume(TOKEN_COLON, "Expect ':' after map key.");

            expression();

            if (count == 65535)
                error("Can't have more than 65535 entries in map literal.");
            count++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after map entries.");
    emitByte(OP_MAP_BUILD);
    emitIndex((uint16_t)count);
}

static void array(bool canAssign)
{
    (void)canAssign;
    int elementCount = 0;
    if (!check(TOKEN_RIGHT_BRACKET))
    {
        do
        {
            expression();
            if (elementCount == 65535)
            {
                error("Can't have more than 65535 elements in array literal.");
            }
            elementCount++;

        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitByte(OP_ARRAY_BUILD);
    emitIndex((uint16_t)elementCount);
}

static void arrayIndex(bool canAssign)
{
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
    if (canAssign && match(TOKEN_EQUAL))
    {
        expression();
        emitByte(OP_INDEX_SET);
    }
    else
    {
        emitByte(OP_INDEX_GET);
    }
}

static void arrow(bool canAssign)
{
    (void)canAssign;
    consume(TOKEN_IDENTIFIER, "expect method name after '->'.");
    uint16_t name = identifierConstant(&parser.previous);
    if (match(TOKEN_LEFT_PAREN))
    {
        uint8_t argCount = argumentList();
        emitByte(OP_INVOKE);
        emitIndex(name);
        emitByte(argCount);
    }
    else
    {
        emitByte(OP_GET_PROPERTY);
        emitIndex(name);
    }
}

static void moduleAccess(bool canAssign)
{
    (void)canAssign;
    consume(TOKEN_IDENTIFIER, "Expect field name after '::'.");
    uint16_t name = identifierConstant(&parser.previous);

    if (match(TOKEN_LEFT_PAREN))
    {
        emitByte(OP_GET_MODULE);
        emitIndex(name);
        uint8_t argCount = argumentList();
        emitBytes(OP_CALL, argCount);
    }
    else
    {
        emitByte(OP_GET_MODULE);
        emitIndex(name);
    }
}

static void literal(bool canAssign)
{
    (void)canAssign;
    switch (parser.previous.type)
    {
    case TOKEN_FALSE:
        emitByte(OP_FALSE);
        break;
    case TOKEN_NIL:
        emitByte(OP_NIL);
        break;
    case TOKEN_TRUE:
        emitByte(OP_TRUE);
        break;
    default:
        return;
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {map, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_DOT_DOT_DOT] = {NULL, NULL, PREC_NONE},
    [TOKEN_QUESTION_DOT] = {NULL, dotSafe, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISION},
    [TOKEN_LEFT_BRACKET] = {NULL, arrayIndex, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_HASH_LEFT_BRACKET] = {array, arrayIndex, PREC_CALL},
    [TOKEN_ARROW] = {NULL, arrow, PREC_CALL},
    [TOKEN_COLON_COLON] = {NULL, moduleAccess, PREC_CALL},
    [TOKEN_BENUTZEN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {anonymousFunction, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule *getRule(TokenType type)
{
    return &rules[type];
}

static void parsePrecedence(Precedence precedence)
{
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL)
    {
        error("Expect expression");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence)
    {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL))
    {
        error("Invalid assignment target");
    }
}
