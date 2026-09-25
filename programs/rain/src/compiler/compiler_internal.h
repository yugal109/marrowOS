#ifndef rain_compiler_internal_h
#define rain_compiler_internal_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "object.h"
#include "scanner.h"

typedef struct
{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISION,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct
{
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct
{
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT
} FunctionType;

typedef struct Compiler
{
    struct Compiler *enclosing;

    ObjFunction *function;
    FunctionType type;
    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    int loopStart;
    int loopScopeDepth;
    int breakJumps[UINT8_COUNT];
    int breakJumpCount;

} Compiler;

typedef struct ClassCompiler
{
    struct ClassCompiler *enclosing;
    Token name;
    bool hasSuperclass;
} ClassCompiler;

extern Parser parser;
extern Compiler *current;
extern Chunk *compilingChunk;
extern ClassCompiler *currentClass;

/* scan.c */
void errorAt(Token *token, const char *message);
void error(const char *message);
void errorAtCurrent(const char *message);
void advance(void);
void consume(TokenType type, const char *message);
bool check(TokenType type);
bool match(TokenType type);

/* emit.c */
Chunk *currentChunk(void);
void emitByte(uint8_t byte);
void emitBytes(uint8_t byte1, uint8_t byte2);
int emitJump(uint8_t instruction);
void emitReturn(void);
void emitLoop(int loopStart);
void emitIndex(uint16_t index);
uint16_t makeConstant(Value value);
void emitConstant(Value value);
void patchJump(int offset);

/* scope.c */
void initCompiler(Compiler *compiler, FunctionType type);
ObjFunction *endCompiler(void);
void beginScope(void);
void endScope(void);
bool identifiersEqual(Token *a, Token *b);
int resolveLocal(Compiler *compiler, Token *name);
int resolveUpvalue(Compiler *compiler, Token *name);
void addLocal(Token name);
void declareVariable(void);
uint16_t parseVariable(const char *errorMessage);
void markInitialized(void);
void defineVariable(uint16_t global);

/* decl.c */
void function(FunctionType type);
void classDeclaration(void);
void funDeclaration(void);
Token syntheticToken(const char *text);
uint16_t identifierConstant(Token *name);

/* expr.c */
void expression(void);
void namedVariable(Token name, bool canAssign);
void variable(bool canAssign);

/* stmt.c */
void block(void);
void declaration(void);

#endif
