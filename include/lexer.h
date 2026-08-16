#ifndef CHAN_LEXER_H
#define CHAN_LEXER_H

#include "token.h"

typedef struct {
    const char* input;
    int position;      // current position in input (points to current char)
    int readPosition;  // current reading position in input (after current char)
    char ch;           // current char under examination
    int line;
} Lexer;

Lexer* new_lexer(const char* input);
void free_lexer(Lexer* l);
Token next_token(Lexer* l);

#endif //CHAN_LEXER_H
