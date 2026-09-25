#include "compiler_internal.h"
#include "memory.h"

static void varDeclaration(void);
static void printStatement(void);
static void versuchStatement(void);
static void returnStatement(void);
static void whileStatement(void);
static void breakStatement(void);
static void continueStatement(void);
static void ifStatement(void);
static void synchronize(void);
static void statement(void);
static void expressionStatement(void);
static void forStatement(void);
static void importStatement(void);

void block(void)
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
    {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}
static void varDeclaration(void)
{
    uint16_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL))
    {
        expression();
    }
    else
    {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    defineVariable(global);
}

static void printStatement(void)
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void versuchStatement(void)
{
    int handlerJump = emitJump(OP_PUSH_EXCEPTION_HANDLER);

    consume(TOKEN_LEFT_BRACE, "Expect '{' after 'versuch'.");
    beginScope();
    block();
    endScope();

    emitByte(OP_POP_EXCEPTION_HANDLER);

    int overFang = emitJump(OP_JUMP);

    patchJump(handlerJump);

    consume(TOKEN_FANGEN, "Expect 'fang' after versuch block.");
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'fang'.");

    // err variable — error string pushed onto stack by VM on exception
    beginScope();
    consume(TOKEN_IDENTIFIER, "Expect error variable name.");
    Token errToken = parser.previous;

    addLocal(errToken);
    markInitialized();

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after error variable.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' before fang body.");
    block();
    endScope();

    patchJump(overFang);
}

static void returnStatement(void)
{
    if (current->type == TYPE_SCRIPT)
    {
        error("Can't return from top-level code.");
    }
    if (match(TOKEN_SEMICOLON))
    {
        emitReturn();
    }
    else
    {
        if (current->type == TYPE_INITIALIZER)
        {
            error("can't return a value from an initializer.");
        }
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void whileStatement(void)
{

    int surroundingLoopStart = current->loopStart;
    int surroundingLoopScopeDepth = current->loopScopeDepth;
    int surroundingBreakJumpCount = current->breakJumpCount;

    current->loopStart = currentChunk()->count;
    current->loopScopeDepth = current->scopeDepth;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'while'.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    statement();

    emitLoop(current->loopStart);
    patchJump(exitJump);

    emitByte(OP_POP);

    for (int i = surroundingBreakJumpCount; i < current->breakJumpCount; i++)
    {
        patchJump(current->breakJumps[i]);
    }
    current->breakJumpCount = surroundingBreakJumpCount;

    current->loopStart = surroundingLoopStart;
    current->loopScopeDepth = surroundingLoopScopeDepth;
}

static void breakStatement(void)
{
    if (current->loopStart == -1)
    {
        error("Can't use 'break' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");

    for (int i = current->localCount - 1; i >= 0 && current->locals[i].depth > current->loopScopeDepth; i--)
    {
        if (current->locals[i].isCaptured)
        {
            emitByte(OP_CLOSE_UPVALUE);
        }
        else
        {
            emitByte(OP_POP);
        }
    }

    if (current->breakJumpCount == UINT8_COUNT)
    {
        error("Too many break statements in one loop.");
        return;
    }
    current->breakJumps[current->breakJumpCount++] = emitJump(OP_JUMP);
}

static void continueStatement(void)
{

    if (current->loopStart == -1)
    {

        error("Can't use 'continue' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

    for (int i = current->localCount - 1;
         i >= 0 && current->locals[i].depth > current->loopScopeDepth;
         i--)
    {
        if (current->locals[i].isCaptured)
            emitByte(OP_CLOSE_UPVALUE);
        else
            emitByte(OP_POP);
    }

    emitLoop(current->loopStart);
}

static void ifStatement(void)
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE))
        statement();

    patchJump(elseJump);
}

static void synchronize(void)
{
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF)
    {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;
        switch (parser.current.type)
        {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_BREAK:
        case TOKEN_CONTINUE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
        default:
            // do nothing
            ;
        }
        advance();
    }
}

void declaration(void)
{
    if (match(TOKEN_BENUTZEN))
    {
        importStatement();
    }
    else if (match(TOKEN_CLASS))
    {
        classDeclaration();
    }
    else if (match(TOKEN_FUN))
    {
        funDeclaration();
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
    }
    else
    {
        statement();
    }
    if (parser.panicMode)
        synchronize();
}

static void statement(void)
{
    if (match(TOKEN_PRINT))
    {
        printStatement();
    }

    else if (match(TOKEN_BREAK))
    {
        breakStatement();
    }
    else if (match(TOKEN_CONTINUE))
    {
        continueStatement();
    }
    else if (match(TOKEN_VERSUCHEN))
    {
        versuchStatement();
    }
    else if (match(TOKEN_FOR))
    {
        forStatement();
    }
    else if (match(TOKEN_IF))
    {
        ifStatement();
    }
    else if (match(TOKEN_RETURN))
    {
        returnStatement();
    }
    else if (match(TOKEN_WHILE))
    {
        whileStatement();
    }
    else if (match(TOKEN_LEFT_BRACE))
    {
        beginScope();
        block();
        endScope();
    }
    else
    {
        expressionStatement();
    }
}

static void expressionStatement(void)
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression");
    emitByte(OP_POP);
}

static void forStatement(void)
{
    beginScope();

    int surroundingLoopStart = current->loopStart;
    int surroundingLoopScopeDepth = current->loopScopeDepth;
    int surroundingBreakJumpCount = current->breakJumpCount;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // map iteration: for (k, v von map)
    if (check(TOKEN_IDENTIFIER) && peekToken().type == TOKEN_COMMA)
    {
        advance();
        Token keyName = parser.previous;
        consume(TOKEN_COMMA, "Expect ','.");
        consume(TOKEN_IDENTIFIER, "Expect value variable.");
        Token valName = parser.previous;
        consume(TOKEN_VON, "Expect 'von'.");
        expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')'.");

        int mapSlot = current->localCount;
        addLocal(syntheticToken("__iterMap"));
        markInitialized();

        emitConstant(NUMBER_VAL(0));
        int idxSlot = current->localCount;
        addLocal(syntheticToken("__iterIdx"));
        markInitialized();

        int loopStart = currentChunk()->count;

        emitBytes(OP_GET_LOCAL, idxSlot);
        emitBytes(OP_MAP_NEXT, (uint8_t)mapSlot);
        // stack: [key, value, nextIdx, hasMore]

        int exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // pop true

        // nextIdx on top → store into idxSlot
        emitBytes(OP_SET_LOCAL, idxSlot);
        emitByte(OP_POP);
        // stack: [key, value]

        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        emitLoop(loopStart);
        patchJump(bodyJump);

        current->loopStart = incrementStart;
        current->loopScopeDepth = current->scopeDepth;

        beginScope();
        addLocal(keyName);
        markInitialized();
        addLocal(valName);
        markInitialized();

        statement();
        endScope();

        emitLoop(incrementStart);

        patchJump(exitJump);
        emitByte(OP_POP); // pop false
        emitByte(OP_POP); // pop nextIdx (-1)
        emitByte(OP_POP); // pop nil (value)
        emitByte(OP_POP); // pop nil (key)

        for (int i = surroundingBreakJumpCount; i < current->breakJumpCount; i++)
            patchJump(current->breakJumps[i]);
        current->breakJumpCount = surroundingBreakJumpCount;
        current->loopStart = surroundingLoopStart;
        current->loopScopeDepth = surroundingLoopScopeDepth;

        endScope();
        return;
    }
    // check for "for (item von array)" pattern
    if (check(TOKEN_IDENTIFIER) && peekToken().type == TOKEN_VON)
    {
        advance();
        Token itemName = parser.previous;
        consume(TOKEN_VON, "Expect 'von' after loop variable.");
        expression();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for-von clause.");

        int arraySlot = current->localCount;
        addLocal(syntheticToken("__iterArr"));
        markInitialized();

        emitConstant(NUMBER_VAL(0));
        int indexSlot = current->localCount;
        addLocal(syntheticToken("__iterIdx"));
        markInitialized();

        int loopStart = currentChunk()->count;

        // condition: __iterIdx < __iterArr->len()
        emitBytes(OP_GET_LOCAL, indexSlot);
        emitBytes(OP_GET_LOCAL, arraySlot);
        Token lenToken = syntheticToken("len");
        uint16_t lenName = identifierConstant(&lenToken);
        emitByte(OP_INVOKE);
        emitIndex(lenName);
        emitByte(0);
        emitByte(OP_LESS);
        int exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);

        int bodyJump = emitJump(OP_JUMP);

        int incrementStart = currentChunk()->count;
        emitBytes(OP_GET_LOCAL, indexSlot);
        emitConstant(NUMBER_VAL(1));
        emitByte(OP_ADD);
        emitBytes(OP_SET_LOCAL, indexSlot);
        emitByte(OP_POP);
        emitLoop(loopStart);

        patchJump(bodyJump);

        current->loopStart = incrementStart;
        current->loopScopeDepth = current->scopeDepth;

        beginScope();
        emitBytes(OP_GET_LOCAL, arraySlot);
        emitBytes(OP_GET_LOCAL, indexSlot);
        emitByte(OP_INDEX_GET);
        addLocal(itemName);
        markInitialized();

        statement();
        endScope();

        emitLoop(incrementStart);

        patchJump(exitJump);
        emitByte(OP_POP);

        for (int i = surroundingBreakJumpCount; i < current->breakJumpCount; i++)
        {
            patchJump(current->breakJumps[i]);
        }
        current->breakJumpCount = surroundingBreakJumpCount;

        current->loopStart = surroundingLoopStart;
        current->loopScopeDepth = surroundingLoopScopeDepth;

        endScope();
        return;
    }

    // initializer
    if (match(TOKEN_SEMICOLON))
    {
        // no initializer --  do nothing
    }
    else if (match(TOKEN_VAR))
    {
        varDeclaration();
    }
    else
    {
        expressionStatement();
    }

    int loopStart = currentChunk()->count;

    // condition clause

    int exitJump = -1;
    if (!match(TOKEN_SEMICOLON))
    {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN))
    {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;

        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    current->loopStart = loopStart;
    current->loopScopeDepth = current->scopeDepth;

    statement();
    emitLoop(loopStart);

    if (exitJump != -1)
    {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    for (int i = surroundingBreakJumpCount; i < current->breakJumpCount; i++)
    {
        patchJump(current->breakJumps[i]);
    }
    current->breakJumpCount = surroundingBreakJumpCount;
    current->loopStart = surroundingLoopStart;
    current->loopScopeDepth = surroundingLoopScopeDepth;

    endScope();
}
static void importStatement(void)
{
    consume(TOKEN_STRING, "Expect module name after 'benutzen'.");

    // full path string e.g. "math" or "utils.rn"
    int fullLength = parser.previous.length - 2;
    const char *fullName = parser.previous.start + 1;

    // compute stem name - strip .rn if present
    int stemLength = fullLength;
    if (fullLength > 3 &&
        fullName[fullLength - 3] == '.' &&
        fullName[fullLength - 2] == 'r' &&
        fullName[fullLength - 1] == 'n')
    {
        stemLength = fullLength - 3;
    }

    // also strip directory prefix for stem
    int stemStart = 0;
    for (int i = stemLength - 1; i >= 0; i--)
    {
        if (fullName[i] == '/')
        {
            stemStart = i + 1;
            break;
        }
    }

    // push __import__ function onto stack
    Token importToken = syntheticToken("__import__");
    namedVariable(importToken, false);

    // push full path as argument
    emitConstant(OBJ_VAL(copyString(fullName, fullLength)));

    emitBytes(OP_CALL, 1);

    // define global variable with stem name
    // "math" → var math = ...
    // "utils.rn" → var utils = ...
    // "models/nn.rn" → var nn = ...
    uint16_t global = makeConstant(OBJ_VAL(copyString(fullName + stemStart, stemLength - stemStart)));
    emitByte(OP_DEFINE_GLOBAL);
    emitIndex(global);

    consume(TOKEN_SEMICOLON, "Expect ';' after module name.");
}
