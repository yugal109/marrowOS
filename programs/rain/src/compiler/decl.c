#include <stdlib.h>
#include <string.h>

#include "compiler_internal.h"
#include "memory.h"
#include "vm_internal.h"

static void method(void);

void function(FunctionType type)
{
    // On the heap, like the script compiler: nesting functions would otherwise stack up 9.8 KB each
    Compiler *compiler = (Compiler *)malloc(sizeof(Compiler));
    if (compiler == NULL)
    {
        errorAtCurrent("Not enough memory to compile this function.");
        return;
    }
    initCompiler(compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN))
    {
        do
        {

            if (match(TOKEN_DOT_DOT_DOT))
            {
                current->function->isVariadic = true;
                current->function->arity++;
                if (current->function->arity > 255)
                {
                    errorAtCurrent("Can't have more than 255 parameters.");
                }
                uint16_t paramConstant = parseVariable("Expect parameter name after '...'.");
                defineVariable(paramConstant);
                break;
            }
            current->function->arity++;
            if (current->function->arity > 255)
            {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint16_t paramConstant = parseVariable("Expect parameter name.");
            defineVariable(paramConstant);
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction *function = endCompiler();
    push(OBJ_VAL(function));
    emitByte(OP_CLOSURE);
    emitIndex(makeConstant(OBJ_VAL(function)));

    for (int i = 0; i < function->upvalueCount; i++)
    {
        emitByte(compiler->upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler->upvalues[i].index);
    }
    pop();
    free(compiler);
}

static void method(void)
{
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint16_t constant = identifierConstant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0)
    {
        type = TYPE_INITIALIZER;
    }
    function(type);
    emitByte(OP_METHOD);
    emitIndex(constant);
}

// intern identifier name as ObjString, store in constants[], return its index
uint16_t identifierConstant(Token *name)
{
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

void classDeclaration(void)
{
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint16_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitByte(OP_CLASS);
    emitIndex(nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.name = parser.previous;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    if (match(TOKEN_LESS))
    {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);
        if (identifiersEqual(&className, &parser.previous))
        {
            error("A class can't inherit from itself.");
        }

        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);

        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    namedVariable(className, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        method();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emitByte(OP_POP);

    if (classCompiler.hasSuperclass)
    {
        endScope();
    }

    currentClass = currentClass->enclosing;
}

void funDeclaration(void)
{
    uint16_t global = parseVariable("expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

Token syntheticToken(const char *text)
{
    Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}
