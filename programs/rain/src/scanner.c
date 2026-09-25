#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

Scanner scanner; // single global scanner instance

void initScanner(const char *source)
{
    scanner.start = source;   // point to first character
    scanner.current = source; // point to first character
    scanner.line = 1;         // start on line 1
}

static bool isAtEnd(void)
{
    return *scanner.current == '\0';
}

static char advance(void)
{
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void)
{
    return *scanner.current;
}

static char peekNext(void)
{
    if (isAtEnd())
        return '\0';
    return scanner.current[1];
}

static bool match(char expected)
{
    if (isAtEnd())
        return false;
    if (*scanner.current != expected)
        return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type)
{
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;

    return token;
}

static Token errorToken(const char *message)
{
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;

    return token;
}

static Token string(void)
{
    while (peek() != '"' && !isAtEnd())
    {
        if (peek() == '\n')
            scanner.line++;
        if (peek() == '\\' && peekNext() == '"')
        {
            advance(); // skip backslash
            advance(); // skip quote
            continue;
        }
        // skip #{...} blocks including nested strings
        if (peek() == '#' && peekNext() == '{')
        {
            advance(); // #
            advance(); // {
            int depth = 1;
            while (!isAtEnd() && depth > 0)
            {
                if (peek() == '"')
                {
                    advance(); // opening "
                    while (!isAtEnd() && peek() != '"')
                    {
                        if (peek() == '\\')
                            advance(); // skip escape
                        advance();
                    }
                    if (!isAtEnd())
                        advance(); // closing "
                    continue;
                }
                if (peek() == '{')
                    depth++;
                else if (peek() == '}')
                    depth--;
                if (depth > 0)
                    advance();
            }
            if (!isAtEnd())
                advance(); // closing }
            continue;
        }
        advance();
    }

    if (isAtEnd())
        return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

static bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

static Token number(void)
{
    while (isDigit(peek()))
        advance(); // consume integer part

    // look for fractional part
    if (peek() == '.' && isDigit(peekNext()))
    {
        advance(); // consume the '.'
        while (isDigit(peek()))
            advance(); // consume fractional part
    }

    return makeToken(TOKEN_NUMBER);
}

static bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(int start, int length, const char *rest, TokenType type)
{
    if (scanner.current - scanner.start == start + length && memcmp(scanner.start + start, rest, length) == 0)
    {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void)
{
    switch (scanner.start[0])
    { // check first character
    case 'a':
        return checkKeyword(1, 2, "nd", TOKEN_AND);
    case 'b':
        switch (scanner.start[1])
        {
        case 'e':
            return checkKeyword(2, 6, "nutzen", TOKEN_BENUTZEN);
        case 'r':
            return checkKeyword(2, 3, "eak", TOKEN_BREAK);
        }
    case 'c':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'l':
                return checkKeyword(2, 3, "ass", TOKEN_CLASS);
            case 'o':
                return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
            }
        }
    case 'e':
        return checkKeyword(1, 3, "lse", TOKEN_ELSE);
    case 'f':
        if (scanner.current - scanner.start > 1)
        { // more than one char scanned
            switch (scanner.start[1])
            { // check second character
            case 'a':
                if (scanner.current - scanner.start > 2)
                {
                    switch (scanner.start[2])
                    {
                    case 'l':
                        return checkKeyword(3, 2, "se", TOKEN_FALSE);
                    case 'n':
                        return checkKeyword(3, 1, "g", TOKEN_FANGEN);
                    }
                }
                break;
            case 'o':
                return checkKeyword(2, 1, "r", TOKEN_FOR);
            case 'u':
                return checkKeyword(2, 1, "n", TOKEN_FUN);
            }
        }
        break;
    case 'i':
        return checkKeyword(1, 1, "f", TOKEN_IF);
    case 'n':
        return checkKeyword(1, 2, "il", TOKEN_NIL);
    case 'o':
        return checkKeyword(1, 1, "r", TOKEN_OR);
    case 'p':
        return checkKeyword(1, 4, "rint", TOKEN_PRINT);
    case 'r':
        return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
    case 's':
        return checkKeyword(1, 4, "uper", TOKEN_SUPER);
    case 't':
        if (scanner.current - scanner.start > 1)
        { // more than one char scanned
            switch (scanner.start[1])
            { // check second character
            case 'h':
                return checkKeyword(2, 2, "is", TOKEN_THIS);
            case 'r':
                return checkKeyword(2, 2, "ue", TOKEN_TRUE);
            }
        }
        break;
    case 'v':
        if (scanner.current - scanner.start > 1)
        {
            switch (scanner.start[1])
            {
            case 'o':
                return checkKeyword(2, 1, "n", TOKEN_VON);
            case 'a':
                return checkKeyword(2, 1, "r", TOKEN_VAR);

            case 'e':
                return checkKeyword(2, 5, "rsuch", TOKEN_VERSUCHEN);
            }
        }
        break;
    case 'w':
        return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER; // no keyword matched → user defined identifier
}

static void skipWhitespace(void)
{
    for (;;)
    {
        char c = peek();
        switch (c)
        {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '\n':
            scanner.line++;
            advance();
            break;
        case '/': // ← add this case
            if (peekNext() == '/')
            { // ← check for //
                while (peek() != '\n' && !isAtEnd())
                    advance(); // skip to end of line
            }
            else
            {
                return; // just a division slash
            }
            break;
        default:
            return;
        }
    }
}

static Token identifier(void)
{
    while (isAlpha(peek()) || isDigit(peek()))
        advance();
    return makeToken(identifierType());
}

Token scanToken(void)
{

    skipWhitespace();
    scanner.start = scanner.current;
    if (isAtEnd())
        return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c))
        return identifier();
    if (isDigit(c))
        return number();

    switch (c)
    {
    case '[':
        return makeToken(TOKEN_LEFT_BRACKET);
    case ']':
        return makeToken(TOKEN_RIGHT_BRACKET);
    case '#':
        if (match('['))
        {
            return makeToken(TOKEN_HASH_LEFT_BRACKET);
        }
        return errorToken("Unexpected character after '#'.");
    case '(':
        return makeToken(TOKEN_LEFT_PAREN);
    case ')':
        return makeToken(TOKEN_RIGHT_PAREN);
    case '{':
        return makeToken(TOKEN_LEFT_BRACE);
    case '}':
        return makeToken(TOKEN_RIGHT_BRACE);
    case ';':
        return makeToken(TOKEN_SEMICOLON);
    case ',':
        return makeToken(TOKEN_COMMA);
    case '.':
        if (peek() == '.' && peekNext() == '.')
        {
            advance();
            advance();
            return makeToken(TOKEN_DOT_DOT_DOT);
        }
        return makeToken(TOKEN_DOT);
    case ':':
    {
        if (match(':'))
        {
            return makeToken(TOKEN_COLON_COLON);
        }
        return makeToken(TOKEN_COLON);
    }
    case '?':
        if (match('.'))
        {
            return makeToken(TOKEN_QUESTION_DOT);
        }
        return errorToken("Unexpected character '?'.");

    case '-':
        if (match('>'))
            return makeToken(TOKEN_ARROW);
        return makeToken(TOKEN_MINUS);
    case '+':
        return makeToken(TOKEN_PLUS);
    case '/':
        return makeToken(TOKEN_SLASH);
    case '*':
        return makeToken(TOKEN_STAR);
        // two character tokens
    case '!':
        return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
        return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
        return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
        return string();
    }

    return errorToken("Unexpected character.");
}

Token peekToken(void)
{
    Scanner saved = scanner;
    Token token = scanToken();
    scanner = saved;
    return token;
}
