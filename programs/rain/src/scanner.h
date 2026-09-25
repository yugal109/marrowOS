#ifndef rain_scanner_h
#define rain_scanner_h
typedef enum
{
    // Single-character tokens.
    TOKEN_LEFT_PAREN,        // 0
    TOKEN_RIGHT_PAREN,       // 1
    TOKEN_LEFT_BRACE,        // 2
    TOKEN_RIGHT_BRACE,       // 3
    TOKEN_LEFT_BRACKET,      // 4  [
    TOKEN_RIGHT_BRACKET,     // 5  ]
    TOKEN_COMMA,             // 6
    TOKEN_DOT,               // 7
    TOKEN_MINUS,             // 8
    TOKEN_PLUS,              // 9
    TOKEN_SEMICOLON,         // 10
    TOKEN_SLASH,             // 11
    TOKEN_STAR,              // 12
                             // One or two character tokens.
    TOKEN_BANG,              // 13
    TOKEN_BANG_EQUAL,        // 14
    TOKEN_EQUAL,             // 15
    TOKEN_EQUAL_EQUAL,       // 16
    TOKEN_GREATER,           // 17
    TOKEN_GREATER_EQUAL,     // 18
    TOKEN_LESS,              // 19
    TOKEN_LESS_EQUAL,        // 20
    TOKEN_COLON,             // 21  :
    TOKEN_COLON_COLON,       // 22  ::
    TOKEN_DOT_DOT_DOT,       // 23  ...
    TOKEN_QUESTION_DOT,      // 24  ?.
                             // Array tokens.
    TOKEN_HASH_LEFT_BRACKET, // 25  #[
                             // Literals.
    TOKEN_IDENTIFIER,        // 26
    TOKEN_STRING,            // 27
    TOKEN_NUMBER,            // 28
                             // Keywords.
    TOKEN_AND,               // 29
    TOKEN_CLASS,             // 30
    TOKEN_ELSE,              // 31
    TOKEN_FALSE,             // 32
    TOKEN_FOR,               // 33
    TOKEN_FUN,               // 34
    TOKEN_IF,                // 35
    TOKEN_BENUTZEN,          // 36
    TOKEN_NIL,               // 37
    TOKEN_OR,                // 38
    TOKEN_PRINT,             // 39
    TOKEN_RETURN,            // 40
    TOKEN_SUPER,             // 41
    TOKEN_THIS,              // 42
    TOKEN_TRUE,              // 43
    TOKEN_VAR,               // 44
    TOKEN_WHILE,             // 45
    TOKEN_ARROW,             // 46  ->
    TOKEN_VERSUCHEN,         // 47
    TOKEN_FANGEN,            // 48
    TOKEN_VON,               // 49
    TOKEN_BREAK,             // 50
    TOKEN_CONTINUE,          // 51
    TOKEN_ERROR,             // 52
    TOKEN_EOF                // 53
} TokenType;
typedef struct
{
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;
typedef struct
{
    const char *start;
    const char *current;
    int line;
} Scanner;
extern Scanner scanner;
void initScanner(const char *source);
Token scanToken(void);
Token peekToken(void);
#endif
