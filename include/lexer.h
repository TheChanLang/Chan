#ifndef CHAN_LEXER_H
#define CHAN_LEXER_H

#include "token.h"

typedef struct {
    const char* input;
    int position;      // current position in input (points to current char)
    int readPosition;  // current reading position in input (after current char)
    char ch;           // current char under examination
    int line;

    // Every token literal produced by this lexer is malloc'd and owned
    // here, so it can be released wholesale in free_lexer(). The parser
    // strdup()s any literal it keeps in the AST.
    char** literals;
    int n_literals;
    int cap_literals;
} Lexer;

Lexer* new_lexer(const char* input);
void free_lexer(Lexer* l);
Token next_token(Lexer* l);

#endif //CHAN_LEXER_H
