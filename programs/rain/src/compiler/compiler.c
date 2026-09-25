#include <stdlib.h>

#include "compiler.h"
#include "compiler_internal.h"
#include "memory.h"
#include "scanner.h"

Parser parser;

Compiler *current = NULL;

Chunk *compilingChunk;

ClassCompiler *currentClass = NULL;

ObjFunction *compile(const char *source)
{
    initScanner(source);
    // On the heap: a Compiler is about 9.8 KB, and MarrowOS programs only get a 16 KB stack
    Compiler *compiler = (Compiler *)malloc(sizeof(Compiler));
    if (compiler == NULL)
    {
        return NULL;
    }
    initCompiler(compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF))
    {
        declaration();
    }
    ObjFunction *function = endCompiler();
    free(compiler);
    return parser.hadError ? NULL : function;
}

void markCompilerRoots(void)
{
    Compiler *compiler = current;
    while (compiler != NULL)
    {
        markObject((Obj *)compiler->function);
        compiler = compiler->enclosing;
    }
}
