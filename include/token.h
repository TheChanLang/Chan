#ifndef CHAN_TOKEN_H
#define CHAN_TOKEN_H

typedef enum {
    // Special tokens
    TOKEN_ILLEGAL, // Token/character not recognized
    TOKEN_EOF,     // End of file
    TOKEN_EOL,     // End of line (\n or \r\n)

    // Identifiers + literals
    TOKEN_IDENT,   // main, foo, x, y, ...
    TOKEN_INT,     // 12345
    TOKEN_FLOAT,   // 3.14
    TOKEN_STR,     // "text"

    // Operators
    TOKEN_ASSIGN,  // =
    TOKEN_PLUS,    // +
    TOKEN_MINUS,   // -
    TOKEN_ASTERISK,// *
    TOKEN_SLASH,   // /
    TOKEN_PERCENT, // %
    TOKEN_EQ,      // ==
    TOKEN_NOT_EQ,  // !=
    TOKEN_LT,      // <
    TOKEN_GT,      // >
    TOKEN_LE,      // <=
    TOKEN_GE,      // >=
    TOKEN_AND,     // &&
    TOKEN_OR,      // ||
    TOKEN_BANG,    // !

    // Delimiters
    TOKEN_LPAREN,  // (
    TOKEN_RPAREN,  // )
    TOKEN_LBRACE,  // {
    TOKEN_RBRACE,  // }
    TOKEN_LBRACKET,// [
    TOKEN_RBRACKET,// ]
    TOKEN_COMMA,   // , — list separator
    TOKEN_COLON,   // :

    // Keywords
    TOKEN_FN,      // fn
    TOKEN_LET,     // let
    TOKEN_RETURN,  // return
    TOKEN_IF,      // if
    TOKEN_ELIF,    // elif
    TOKEN_ELSE,    // else
    TOKEN_WHILE,   // while
    TOKEN_CONT,    // cont (continue)
    TOKEN_BREAK,   // break
    TOKEN_COPY,    // copy — explicit copy; move is the default
    TOKEN_TRUE,    // true
    TOKEN_FALSE,   // false

    // Type keywords: bool array map str nil int float obj
    // The token's literal holds the type name.
    TOKEN_TYPE,
} TokenType;

typedef struct {
    TokenType type;
    const char* literal;
    int line;
} Token;

#endif //CHAN_TOKEN_H
