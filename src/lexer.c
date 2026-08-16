#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

// Helper to create a new token
static Token make_token(TokenType type, const char* literal, int line) {
    Token tok;
    tok.type = type;
    tok.literal = literal;
    tok.line = line;
    return tok;
}

// The lexer owns every literal it allocates; free_lexer() releases them all.
// Pointers stay valid for the lifetime of the lexer (each literal is its own
// malloc, so the array realloc never invalidates them).
static const char* lex_keep(Lexer* l, char* s) {
    if (l->n_literals >= l->cap_literals) {
        l->cap_literals = l->cap_literals ? l->cap_literals * 2 : 32;
        l->literals = realloc(l->literals, l->cap_literals * sizeof(char*));
    }
    l->literals[l->n_literals++] = s;
    return s;
}

// Helper to create token from a single character
static Token make_char_token(Lexer* l, TokenType type, char ch, int line) {
    char* literal = malloc(2);
    literal[0] = ch;
    literal[1] = '\0';
    return make_token(type, lex_keep(l, literal), line);
}

// Helper to create a two-character operator token (==, !=, <=, >=, &&, ||)
static Token make_two_char_token(Lexer* l, TokenType type, char c1, char c2, int line) {
    char* literal = malloc(3);
    literal[0] = c1;
    literal[1] = c2;
    literal[2] = '\0';
    return make_token(type, lex_keep(l, literal), line);
}

void read_char(Lexer* l) {
    if (l->readPosition >= strlen(l->input)) {
        l->ch = 0; // NUL character, signifies EOF
    } else {
        l->ch = l->input[l->readPosition];
    }
    l->position = l->readPosition;
    l->readPosition += 1;
}

Lexer* new_lexer(const char* input) {
    Lexer* l = malloc(sizeof(Lexer));
    l->input = input;
    l->line = 1;
    l->readPosition = 0;
    l->position = 0;
    l->literals = NULL;
    l->n_literals = 0;
    l->cap_literals = 0;
    read_char(l);
    return l;
}

void free_lexer(Lexer* l) {
    if (!l) return;
    for (int i = 0; i < l->n_literals; i++) {
        free(l->literals[i]);
    }
    free(l->literals);
    free(l);
}

void skip_whitespace(Lexer* l) {
    while (l->ch == ' ' || l->ch == '\t') {
        read_char(l);
    }
}

char peek_char(Lexer* l) {
    if (l->readPosition >= strlen(l->input)) {
        return 0;
    }
    return l->input[l->readPosition];
}

// Reads an identifier (keywords or variable names)
const char* read_identifier(Lexer* l) {
    int startPos = l->position;
    while (isalnum(l->ch) || l->ch == '_') {
        read_char(l);
    }
    int length = l->position - startPos;
    char* ident = malloc(length + 1);
    strncpy(ident, l->input + startPos, length);
    ident[length] = '\0';
    return lex_keep(l, ident);
}

// Reads a number literal (int or float)
const char* read_number(Lexer* l) {
    int startPos = l->position;
    while (isdigit(l->ch)) {
        read_char(l);
    }
    // 3.14 — a '.' followed by a digit makes it a float
    if (l->ch == '.' && isdigit(peek_char(l))) {
        read_char(l);
        while (isdigit(l->ch)) {
            read_char(l);
        }
    }
    int length = l->position - startPos;
    char* num = malloc(length + 1);
    strncpy(num, l->input + startPos, length);
    num[length] = '\0';
    return lex_keep(l, num);
}

// Reads a string literal "..." — minimal escapes: \n \t \\ \"
const char* read_string(Lexer* l) {
    read_char(l); // consume opening "
    int cap = 8;
    int len = 0;
    char* buf = malloc(cap);
    while (l->ch != '"' && l->ch != 0 && l->ch != '\n') {
        if (l->ch == '\\') {
            read_char(l);
            char c = 0;
            switch (l->ch) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                default:   c = l->ch; break;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = c;
        } else {
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = l->ch;
        }
        read_char(l);
    }
    buf[len] = '\0';
    if (l->ch == '"') read_char(l); // consume closing "
    return lex_keep(l, buf);
}

// Lookup function for keywords
TokenType lookup_ident(const char* ident) {
    if (strcmp(ident, "fn") == 0)     return TOKEN_FN;
    if (strcmp(ident, "let") == 0)    return TOKEN_LET;
    if (strcmp(ident, "return") == 0) return TOKEN_RETURN;
    if (strcmp(ident, "if") == 0)     return TOKEN_IF;
    if (strcmp(ident, "elif") == 0)   return TOKEN_ELIF;
    if (strcmp(ident, "else") == 0)   return TOKEN_ELSE;
    if (strcmp(ident, "while") == 0)  return TOKEN_WHILE;
    if (strcmp(ident, "cont") == 0)   return TOKEN_CONT;
    if (strcmp(ident, "break") == 0)  return TOKEN_BREAK;
    if (strcmp(ident, "copy") == 0)   return TOKEN_COPY;
    if (strcmp(ident, "true") == 0)   return TOKEN_TRUE;
    if (strcmp(ident, "false") == 0)  return TOKEN_FALSE;
    // Type keywords — literal keeps the type name
    if (strcmp(ident, "bool") == 0)  return TOKEN_TYPE;
    if (strcmp(ident, "array") == 0) return TOKEN_TYPE;
    if (strcmp(ident, "map") == 0)   return TOKEN_TYPE;
    if (strcmp(ident, "str") == 0)   return TOKEN_TYPE;
    if (strcmp(ident, "nil") == 0)   return TOKEN_TYPE;
    if (strcmp(ident, "int") == 0)   return TOKEN_TYPE;
    if (strcmp(ident, "float") == 0) return TOKEN_TYPE;
    if (strcmp(ident, "obj") == 0)   return TOKEN_TYPE;
    return TOKEN_IDENT;
}

Token next_token(Lexer* l) {
    Token tok;

    skip_whitespace(l);

    switch (l->ch) {
        case '=':
            if (peek_char(l) == '=') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_EQ, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_ASSIGN, l->ch, l->line);
            }
            break;
        case '!':
            if (peek_char(l) == '=') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_NOT_EQ, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_BANG, l->ch, l->line);
            }
            break;
        case '<':
            if (peek_char(l) == '=') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_LE, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_LT, l->ch, l->line);
            }
            break;
        case '>':
            if (peek_char(l) == '=') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_GE, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_GT, l->ch, l->line);
            }
            break;
        case '&':
            if (peek_char(l) == '&') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_AND, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_ILLEGAL, l->ch, l->line);
            }
            break;
        case '|':
            if (peek_char(l) == '|') {
                char prev = l->ch;
                read_char(l);
                tok = make_two_char_token(l, TOKEN_OR, prev, l->ch, l->line);
            } else {
                tok = make_char_token(l, TOKEN_ILLEGAL, l->ch, l->line);
            }
            break;
        case '+':
            tok = make_char_token(l, TOKEN_PLUS, l->ch, l->line);
            break;
        case '-':
            tok = make_char_token(l, TOKEN_MINUS, l->ch, l->line);
            break;
        case '*':
            tok = make_char_token(l, TOKEN_ASTERISK, l->ch, l->line);
            break;
        case '/':
            tok = make_char_token(l, TOKEN_SLASH, l->ch, l->line);
            break;
        case '%':
            tok = make_char_token(l, TOKEN_PERCENT, l->ch, l->line);
            break;
        case '(':
            tok = make_char_token(l, TOKEN_LPAREN, l->ch, l->line);
            break;
        case ')':
            tok = make_char_token(l, TOKEN_RPAREN, l->ch, l->line);
            break;
        case '{':
            tok = make_char_token(l, TOKEN_LBRACE, l->ch, l->line);
            break;
        case '}':
            tok = make_char_token(l, TOKEN_RBRACE, l->ch, l->line);
            break;
        case '[':
            tok = make_char_token(l, TOKEN_LBRACKET, l->ch, l->line);
            break;
        case ']':
            tok = make_char_token(l, TOKEN_RBRACKET, l->ch, l->line);
            break;
        case ',':
            // The list separator
            tok = make_char_token(l, TOKEN_COMMA, l->ch, l->line);
            break;
        case '#':
            // Line comment: skip to end of line (or EOF)
            while (l->ch != '\n' && l->ch != 0) {
                read_char(l);
            }
            return next_token(l); // the \n is emitted as EOL by the next call
        case ':':
            tok = make_char_token(l, TOKEN_COLON, l->ch, l->line);
            break;
        case '\n':
            tok = make_char_token(l, TOKEN_EOL, l->ch, l->line);
            l->line++;
            break;
        case '\r': // Handle \r\n
            if (peek_char(l) == '\n') {
                read_char(l); // consume the \n
                tok = make_token(TOKEN_EOL, lex_keep(l, strdup("\\r\\n")), l->line);
            } else { // Standalone \r is illegal for now
                tok = make_char_token(l, TOKEN_ILLEGAL, l->ch, l->line);
            }
            l->line++;
            break;
        case '"':
            tok = make_token(TOKEN_STR, read_string(l), l->line);
            return tok; // Early return: read_string already advanced the lexer
        case 0:
            tok = make_token(TOKEN_EOF, lex_keep(l, strdup("")), l->line);
            break;
        default:
            if (isalpha(l->ch) || l->ch == '_') {
                const char* literal = read_identifier(l);
                TokenType type = lookup_ident(literal);
                tok = make_token(type, literal, l->line);
                return tok; // Early return because read_identifier already advanced the lexer
            } else if (isdigit(l->ch)) {
                const char* literal = read_number(l);
                TokenType type = strchr(literal, '.') ? TOKEN_FLOAT : TOKEN_INT;
                tok = make_token(type, literal, l->line);
                return tok; // Early return
            } else {
                tok = make_char_token(l, TOKEN_ILLEGAL, l->ch, l->line);
            }
            break;
    }

    read_char(l);
    return tok;
}
