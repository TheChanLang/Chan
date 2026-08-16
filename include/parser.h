#ifndef CHAN_PARSER_H
#define CHAN_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer* l;
    Token curToken;
    Token peekToken;

    char** errors;
    int num_errors;
} Parser;

Parser* new_parser(Lexer* l);
void free_parser(Parser* p);
Program* parse_program(Parser* p);

#endif //CHAN_PARSER_H
