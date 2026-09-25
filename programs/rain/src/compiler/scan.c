#include <stdio.h>

#include "compiler_internal.h"

void errorAt(Token *token, const char *message)
{
    if (parser.panicMode)
        return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF)
    {
        fprintf(stderr, " at end");
    }
    else if (token->type == TOKEN_ERROR)
    {
        // NOTHING
    }
    else
    {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

void error(const char *message)
{
    errorAt(&parser.previous, message);
}

void errorAtCurrent(const char *message)
{
    errorAt(&parser.current, message);
}

void advance(void)
{
    parser.previous = parser.current;
    for (;;)
    {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR)
            break;
        errorAtCurrent(parser.current.start);
    }
}

void consume(TokenType type, const char *message)
{
    if (parser.current.type == type)
    {
        advance();
        return;
    }
    errorAtCurrent(message);
}

bool check(TokenType type)
{
    return parser.current.type == type;
}

bool match(TokenType type)
{
    if (!check(type))
        return false;
    advance();
    return true;
}
