#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "ast.h"


// =================================================================
// Pratt Parser Declarations
// =================================================================

// Define operator precedences
typedef enum {
    PREC_LOWEST,
    PREC_ASSIGN,      // =
    PREC_OR,          // ||
    PREC_AND,         // &&
    PREC_EQUALS,      // == !=
    PREC_LESSGREATER, // > < >= <=
    PREC_SUM,         // + -
    PREC_PRODUCT,     // * /
    PREC_PREFIX,      // -X !X copy X
    PREC_CALL         // myFunction(X)
} Precedence;

// Function pointer types for Pratt parsing
typedef Expression* (*PrefixParseFn)(Parser*);
typedef Expression* (*InfixParseFn)(Parser*, Expression*);

typedef struct {
    PrefixParseFn prefix_fn;
    InfixParseFn infix_fn;
    Precedence precedence;
} ParseRule;

// Forward declarations for recursive parsing
static Expression* parse_expression(Parser* p, Precedence precedence);
static ParseRule* get_rule(TokenType type);
static Expression* parse_integer_literal(Parser* p);
static Expression* parse_float_literal(Parser* p);
static Expression* parse_string_literal(Parser* p);
static Expression* parse_boolean_literal(Parser* p);
static Expression* parse_nil_literal(Parser* p);
static Identifier* parse_identifier(Parser* p);
static Expression* parse_ident_prefix(Parser* p);
static Expression* parse_prefix_expression(Parser* p);
static Expression* parse_grouped_expression(Parser* p);
static Expression* parse_array_literal(Parser* p);
static Expression* parse_map_literal(Parser* p);
static Expression* parse_call_expression(Parser* p, Expression* function);
static Expression* parse_index_expression(Parser* p, Expression* left);
static Expression* parse_infix_expression(Parser* p, Expression* left);
static Expression** parse_expression_list(Parser* p, TokenType ending, int* out_count);
static Statement* parse_statement(Parser* p);
static Statement* parse_let_statement(Parser* p);
static Statement* parse_return_statement(Parser* p);
static Statement* parse_function_statement(Parser* p);
static Statement* parse_if_statement(Parser* p);
static Statement* parse_while_statement(Parser* p);
static Statement* parse_cont_statement(Parser* p);
static Statement* parse_break_statement(Parser* p);
static Statement* parse_expression_statement(Parser* p);
static BlockStatement* parse_block_statement(Parser* p);

// =================================================================
// Main Parser Logic
// =================================================================

// Helper to advance tokens
static void next_token_p(Parser* p) {
    p->curToken = p->peekToken;
    p->peekToken = next_token(p->l);
}

static void parser_error(Parser* p, const char* msg) {
    p->num_errors++;
    p->errors = realloc(p->errors, p->num_errors * sizeof(char*));
    char* error_msg = malloc(strlen(msg) + 80);
    sprintf(error_msg, "line %d: %s [cur=%d '%s' peek=%d '%s']", p->l->line, msg,
            p->curToken.type, p->curToken.literal ? p->curToken.literal : "",
            p->peekToken.type, p->peekToken.literal ? p->peekToken.literal : "");
    p->errors[p->num_errors - 1] = error_msg;
}

Parser* new_parser(Lexer* l) {
    Parser* p = malloc(sizeof(Parser));
    p->l = l;
    p->num_errors = 0;
    p->errors = NULL;

    // Read two tokens, so curToken and peekToken are both set
    next_token_p(p);
    next_token_p(p);

    return p;
}

void free_parser(Parser* p) {
    if (p->errors) {
        for (int i = 0; i < p->num_errors; i++) {
            free(p->errors[i]);
        }
        free(p->errors);
    }
    free(p);
}


static Identifier* new_identifier_p(Parser* p, Token token, const char* value) {
    Identifier* ident = malloc(sizeof(Identifier));
    ident->expression.node.type = NODE_IDENTIFIER;
    ident->token = token;
    ident->value = strdup(value); // the AST owns its own copy
    return ident;
}

// Check the current token is of the expected type
static int cur_is(Parser* p, TokenType type) {
    return p->curToken.type == type;
}

// Check the next token is of the expected type; if so, advance
static int expect_peek(Parser* p, TokenType type) {
    if (p->peekToken.type == type) {
        next_token_p(p);
        return 1;
    }
    parser_error(p, "expected next token");
    return 0;
}

static Identifier* parse_identifier(Parser* p) {
    if (p->curToken.type != TOKEN_IDENT) {
        parser_error(p, "expected identifier");
        return NULL;
    }
    return new_identifier_p(p, p->curToken, p->curToken.literal);
}

// Chan is EOL-terminated: skip blank lines
static void skip_eols(Parser* p) {
    while (p->curToken.type == TOKEN_EOL) {
        next_token_p(p);
    }
}


static Statement* parse_return_statement(Parser* p) {
    ReturnStatement* stmt = malloc(sizeof(ReturnStatement));
    stmt->statement.node.type = NODE_RETURN_STATEMENT;
    stmt->token = p->curToken;

    next_token_p(p);
    stmt->return_value = parse_expression(p, PREC_LOWEST);

    return (Statement*)stmt;
}

static Statement* parse_let_statement(Parser* p) {
    LetStatement* stmt = malloc(sizeof(LetStatement));
    stmt->statement.node.type = NODE_LET_STATEMENT;
    stmt->token = p->curToken;

    next_token_p(p);
    if (p->curToken.type != TOKEN_IDENT) {
        parser_error(p, "Expected identifier after 'let'");
        free(stmt); return NULL;
    }
    stmt->name = new_identifier_p(p, p->curToken, p->curToken.literal);

    next_token_p(p);
    if (p->curToken.type != TOKEN_COLON) {
        parser_error(p, "Expected ':' for type annotation in let statement");
        free(stmt->name); free(stmt); return NULL;
    }

    next_token_p(p);
    if (p->curToken.type != TOKEN_TYPE) {
        parser_error(p, "Expected type keyword after ':'");
        free(stmt->name); free(stmt); return NULL;
    }
    stmt->type = new_identifier_p(p, p->curToken, p->curToken.literal);

    next_token_p(p);
    if (p->curToken.type == TOKEN_ASSIGN) {
        next_token_p(p);
        stmt->value = parse_expression(p, PREC_LOWEST);
    } else {
        stmt->value = NULL;
    }

    return (Statement*)stmt;
}

static Statement* parse_function_statement(Parser* p) {
    FunctionStatement* stmt = malloc(sizeof(FunctionStatement));
    stmt->statement.node.type = NODE_FN_STATEMENT;
    stmt->token = p->curToken;

    next_token_p(p); // curToken is now the function name
    stmt->name = parse_identifier(p);
    if (stmt->name == NULL) { free(stmt); return NULL; }

    if (!expect_peek(p, TOKEN_LPAREN)) { free(stmt); return NULL; }

    stmt->parameters = NULL;
    stmt->num_parameters = 0;

    // Parameters: a: int, b: int, ...
    if (p->peekToken.type != TOKEN_RPAREN) {
        next_token_p(p);
        do {
            if (p->curToken.type != TOKEN_IDENT) {
                parser_error(p, "Expected parameter name");
                free(stmt); return NULL;
            }
            stmt->parameters = realloc(stmt->parameters, (stmt->num_parameters + 1) * sizeof(Parameter));
            Parameter* param = &stmt->parameters[stmt->num_parameters];
            param->name = new_identifier_p(p, p->curToken, p->curToken.literal);
            param->type = NULL;
            stmt->num_parameters++;

            // Parameter types are mandatory: fn f(a: int, b: int)
            if (p->peekToken.type != TOKEN_COLON) {
                parser_error(p, "Expected ':' type after parameter name");
                free(stmt); return NULL;
            }
            next_token_p(p); // consume ':'
            next_token_p(p);
            if (p->curToken.type != TOKEN_TYPE) {
                parser_error(p, "Expected type keyword after ':' in parameter");
                free(stmt); return NULL;
            }
            param->type = new_identifier_p(p, p->curToken, p->curToken.literal);
            if (p->peekToken.type == TOKEN_COMMA) {
                next_token_p(p); // consume ','
                next_token_p(p); // advance to the next parameter
            }
        } while (p->peekToken.type != TOKEN_RPAREN);
    }
    if (!expect_peek(p, TOKEN_RPAREN)) { free(stmt); return NULL; }

    // Return type is mandatory and always uses ':': fn f(a: int, b: int): int { ... }
    stmt->return_type = NULL;
    if (p->peekToken.type == TOKEN_COLON) {
        next_token_p(p); // consume ':'
        if (p->peekToken.type != TOKEN_TYPE) {
            parser_error(p, "Expected return type after ':'");
            free(stmt); return NULL;
        }
        next_token_p(p);
        stmt->return_type = new_identifier_p(p, p->curToken, p->curToken.literal);
    } else {
        parser_error(p, "Expected ':' return type after parameter list");
        free(stmt); return NULL;
    }

    if (!expect_peek(p, TOKEN_LBRACE)) { free(stmt); return NULL; }
    stmt->body = parse_block_statement(p);

    return (Statement*)stmt;
}

static Statement* parse_if_statement(Parser* p) {
    // curToken is TOKEN_IF (or TOKEN_ELIF when parsed as a nested else-if)
    IfStatement* stmt = malloc(sizeof(IfStatement));
    stmt->statement.node.type = NODE_IF_STATEMENT;
    stmt->token = p->curToken;

    next_token_p(p);
    stmt->condition = parse_expression(p, PREC_LOWEST);

    if (!expect_peek(p, TOKEN_LBRACE)) { free(stmt); return NULL; }
    stmt->consequence = parse_block_statement(p);

    stmt->alternative = NULL;
    if (p->peekToken.type == TOKEN_ELIF || p->peekToken.type == TOKEN_ELSE) {
        next_token_p(p); // curToken is now ELIF or ELSE
        if (p->curToken.type == TOKEN_ELIF) {
            // elif cond2 block2 ... => else { if cond2 block2 ... }
            Statement* nested = parse_if_statement(p);
            if (nested == NULL) { free(stmt); return NULL; }
            BlockStatement* wrap = malloc(sizeof(BlockStatement));
            wrap->statement.node.type = NODE_BLOCK_STATEMENT;
            wrap->token = ((IfStatement*)nested)->token;
            wrap->statements = malloc(sizeof(Statement*));
            wrap->statements[0] = nested;
            wrap->num_statements = 1;
            stmt->alternative = wrap;
        } else {
            if (!expect_peek(p, TOKEN_LBRACE)) { free(stmt); return NULL; }
            stmt->alternative = parse_block_statement(p);
        }
    }

    return (Statement*)stmt;
}

static Statement* parse_while_statement(Parser* p) {
    WhileStatement* stmt = malloc(sizeof(WhileStatement));
    stmt->statement.node.type = NODE_WHILE_STATEMENT;
    stmt->token = p->curToken;

    next_token_p(p);
    stmt->condition = parse_expression(p, PREC_LOWEST);

    if (!expect_peek(p, TOKEN_LBRACE)) { free(stmt); return NULL; }
    stmt->body = parse_block_statement(p);

    return (Statement*)stmt;
}

static Statement* parse_cont_statement(Parser* p) {
    ContStatement* stmt = malloc(sizeof(ContStatement));
    stmt->statement.node.type = NODE_CONT_STATEMENT;
    stmt->token = p->curToken;
    return (Statement*)stmt;
}

static Statement* parse_break_statement(Parser* p) {
    BreakStatement* stmt = malloc(sizeof(BreakStatement));
    stmt->statement.node.type = NODE_BREAK_STATEMENT;
    stmt->token = p->curToken;
    return (Statement*)stmt;
}

static Statement* parse_expression_statement(Parser* p) {
    ExpressionStatement* stmt = malloc(sizeof(ExpressionStatement));
    stmt->statement.node.type = NODE_EXPRESSION_STATEMENT;
    stmt->token = p->curToken;
    stmt->expression = parse_expression(p, PREC_LOWEST);
    return (Statement*)stmt;
}

static BlockStatement* parse_block_statement(Parser* p) {
    // curToken is TOKEN_LBRACE
    BlockStatement* block = malloc(sizeof(BlockStatement));
    block->statement.node.type = NODE_BLOCK_STATEMENT;
    block->token = p->curToken;
    block->statements = NULL;
    block->num_statements = 0;

    int capacity = 0;
    next_token_p(p);

    while (p->curToken.type != TOKEN_RBRACE && p->curToken.type != TOKEN_EOF) {
        skip_eols(p);
        if (p->curToken.type == TOKEN_RBRACE || p->curToken.type == TOKEN_EOF) break;
        Statement* stmt = parse_statement(p);
        if (stmt != NULL) {
            if (block->num_statements >= capacity) {
                capacity = (capacity == 0) ? 1 : capacity * 2;
                block->statements = realloc(block->statements, capacity * sizeof(Statement*));
            }
            block->statements[block->num_statements] = stmt;
            block->num_statements++;
        }
        next_token_p(p);
    }
    // curToken is RBRACE or EOF — the caller advances past it
    return block;
}


static Statement* parse_statement(Parser* p) {
    switch (p->curToken.type) {
        case TOKEN_LET:
            return parse_let_statement(p);
        case TOKEN_RETURN:
            return parse_return_statement(p);
        case TOKEN_FN:
            return parse_function_statement(p);
        case TOKEN_IF:
            return parse_if_statement(p);
        case TOKEN_WHILE:
            return parse_while_statement(p);
        case TOKEN_CONT:
            return parse_cont_statement(p);
        case TOKEN_BREAK:
            return parse_break_statement(p);
        default:
            return parse_expression_statement(p);
    }
}


Program* parse_program(Parser* p) {
    Program* program = new_program();
    program->node.type = NODE_PROGRAM;
    program->statements = NULL;
    program->num_statements = 0;

    int capacity = 0;

    while (p->curToken.type != TOKEN_EOF) {
        skip_eols(p);
        if (p->curToken.type == TOKEN_EOF) break;
        Statement* stmt = parse_statement(p);
        if (stmt != NULL) {
            if (program->num_statements >= capacity) {
                capacity = (capacity == 0) ? 1 : capacity * 2;
                program->statements = realloc(program->statements, capacity * sizeof(Statement*));
            }
            program->statements[program->num_statements] = stmt;
            program->num_statements++;
        }
        next_token_p(p);
    }
    return program;
}

// =================================================================
// Pratt Parser Implementation
// =================================================================

ParseRule rules[] = {
    [TOKEN_ASSIGN]   = {NULL, parse_infix_expression, PREC_ASSIGN},
    [TOKEN_PLUS]     = {NULL, parse_infix_expression, PREC_SUM},
    [TOKEN_MINUS]    = {parse_prefix_expression, parse_infix_expression, PREC_SUM},
    [TOKEN_SLASH]    = {NULL, parse_infix_expression, PREC_PRODUCT},
    [TOKEN_ASTERISK] = {NULL, parse_infix_expression, PREC_PRODUCT},
    [TOKEN_PERCENT]  = {NULL, parse_infix_expression, PREC_PRODUCT},
    [TOKEN_EQ]       = {NULL, parse_infix_expression, PREC_EQUALS},
    [TOKEN_NOT_EQ]   = {NULL, parse_infix_expression, PREC_EQUALS},
    [TOKEN_LT]       = {NULL, parse_infix_expression, PREC_LESSGREATER},
    [TOKEN_GT]       = {NULL, parse_infix_expression, PREC_LESSGREATER},
    [TOKEN_LE]       = {NULL, parse_infix_expression, PREC_LESSGREATER},
    [TOKEN_GE]       = {NULL, parse_infix_expression, PREC_LESSGREATER},
    [TOKEN_AND]      = {NULL, parse_infix_expression, PREC_AND},
    [TOKEN_OR]       = {NULL, parse_infix_expression, PREC_OR},
    [TOKEN_BANG]     = {parse_prefix_expression, NULL, PREC_LOWEST},
    [TOKEN_COPY]     = {parse_prefix_expression, NULL, PREC_LOWEST},
    [TOKEN_LPAREN]   = {parse_grouped_expression, parse_call_expression, PREC_CALL},
    [TOKEN_LBRACE]   = {parse_map_literal, NULL, PREC_LOWEST},
    [TOKEN_LBRACKET] = {parse_array_literal, parse_index_expression, PREC_CALL},
    [TOKEN_TYPE]     = {parse_nil_literal, NULL, PREC_LOWEST}, // only "nil" is a value
    [TOKEN_INT]      = {parse_integer_literal, NULL, PREC_LOWEST},
    [TOKEN_FLOAT]    = {parse_float_literal, NULL, PREC_LOWEST},
    [TOKEN_STR]      = {parse_string_literal, NULL, PREC_LOWEST},
    [TOKEN_TRUE]     = {parse_boolean_literal, NULL, PREC_LOWEST},
    [TOKEN_FALSE]    = {parse_boolean_literal, NULL, PREC_LOWEST},
    [TOKEN_IDENT]    = {parse_ident_prefix, NULL, PREC_LOWEST},
};

// Adapter: Identifier* is a subtype of Expression*, but C function pointers
// must match exactly, so we wrap it for the Pratt table.
static Expression* parse_ident_prefix(Parser* p) {
    return (Expression*)parse_identifier(p);
}

static ParseRule* get_rule(TokenType type) {
    if (type >= sizeof(rules) / sizeof(rules[0])) {
        return NULL;
    }
    return &rules[type];
}


static Expression* parse_expression(Parser* p, Precedence precedence) {
    ParseRule* prefix_rule = get_rule(p->curToken.type);
    if (prefix_rule == NULL || prefix_rule->prefix_fn == NULL) {
        parser_error(p, "Expected expression");
        return NULL;
    }
    Expression* leftExpr = prefix_rule->prefix_fn(p);

    while (p->peekToken.type != TOKEN_EOL && p->peekToken.type != TOKEN_EOF) {
        ParseRule* peek_rule = get_rule(p->peekToken.type);
        if (peek_rule == NULL || precedence >= peek_rule->precedence) {
            break;
        }

        InfixParseFn infix = peek_rule->infix_fn;
        if (infix == NULL) {
            break;
        }
        next_token_p(p);
        leftExpr = infix(p, leftExpr);
    }
    return leftExpr;
}

static Expression* parse_integer_literal(Parser* p) {
    IntegerLiteral* literal = malloc(sizeof(IntegerLiteral));
    literal->expression.node.type = NODE_INTEGER_LITERAL;
    literal->token = p->curToken;
    char* end;
    literal->value = strtoll(literal->token.literal, &end, 10);
    if (*end != '\0') {
        parser_error(p, "Invalid integer literal");
        free(literal);
        return NULL;
    }
    return (Expression*)literal;
}

static Expression* parse_float_literal(Parser* p) {
    FloatLiteral* literal = malloc(sizeof(FloatLiteral));
    literal->expression.node.type = NODE_FLOAT_LITERAL;
    literal->token = p->curToken;
    char* end;
    literal->value = strtod(literal->token.literal, &end);
    if (*end != '\0') {
        parser_error(p, "Invalid float literal");
        free(literal);
        return NULL;
    }
    return (Expression*)literal;
}

static Expression* parse_string_literal(Parser* p) {
    StringLiteral* literal = malloc(sizeof(StringLiteral));
    literal->expression.node.type = NODE_STRING_LITERAL;
    literal->token = p->curToken;
    literal->value = strdup(p->curToken.literal); // escapes already processed by the lexer
    return (Expression*)literal;
}

static Expression* parse_boolean_literal(Parser* p) {
    BooleanLiteral* literal = malloc(sizeof(BooleanLiteral));
    literal->expression.node.type = NODE_BOOLEAN_LITERAL;
    literal->token = p->curToken;
    literal->value = (p->curToken.type == TOKEN_TRUE);
    return (Expression*)literal;
}

static Expression* parse_nil_literal(Parser* p) {
    if (strcmp(p->curToken.literal, "nil") != 0) {
        parser_error(p, "type name is not a value");
        return NULL;
    }
    NilLiteral* literal = malloc(sizeof(NilLiteral));
    literal->expression.node.type = NODE_NIL_LITERAL;
    literal->token = p->curToken;
    return (Expression*)literal;
}

static Expression* parse_prefix_expression(Parser* p) {
    PrefixExpression* expr = malloc(sizeof(PrefixExpression));
    expr->expression.node.type = NODE_PREFIX_EXPRESSION;
    expr->token = p->curToken;
    expr->operator = strdup(p->curToken.literal); // the AST owns its own copy
    next_token_p(p);
    expr->right = parse_expression(p, PREC_PREFIX);
    return (Expression*)expr;
}

static Expression* parse_grouped_expression(Parser* p) {
    next_token_p(p);
    Expression* expr = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_RPAREN)) {
        return NULL;
    }
    return expr;
}

static Expression* parse_array_literal(Parser* p) {
    ArrayLiteral* arr = malloc(sizeof(ArrayLiteral));
    arr->expression.node.type = NODE_ARRAY_LITERAL;
    arr->token = p->curToken;
    arr->elements = parse_expression_list(p, TOKEN_RBRACKET, &arr->num_elements);
    return (Expression*)arr;
}

// Parses a map literal: {"a": 1, "b": 2}
static Expression* parse_map_literal(Parser* p) {
    MapLiteral* m = malloc(sizeof(MapLiteral));
    m->expression.node.type = NODE_MAP_LITERAL;
    m->token = p->curToken; // LBRACE
    m->keys = NULL;
    m->values = NULL;
    m->num_pairs = 0;

    int cap = 0;
    next_token_p(p); // past '{'
    while (1) {
        if (p->curToken.type == TOKEN_RBRACE) { next_token_p(p); break; }
        if (p->curToken.type == TOKEN_EOF) { parser_error(p, "expected } in map literal"); break; }

        Expression* k = parse_expression(p, PREC_LOWEST);
        if (!expect_peek(p, TOKEN_COLON)) { break; }
        next_token_p(p); // past ':'
        Expression* v = parse_expression(p, PREC_LOWEST);

        if (m->num_pairs >= cap) {
            cap = cap ? cap * 2 : 2;
            m->keys = realloc(m->keys, cap * sizeof(Expression*));
            m->values = realloc(m->values, cap * sizeof(Expression*));
        }
        m->keys[m->num_pairs] = k;
        m->values[m->num_pairs] = v;
        m->num_pairs++;

        if (p->peekToken.type == TOKEN_COMMA) {
            next_token_p(p); // consume ','
            next_token_p(p);
        } else if (p->peekToken.type == TOKEN_RBRACE) {
            next_token_p(p);
            break;
        } else {
            parser_error(p, "expected , or } in map literal");
            break;
        }
    }
    return (Expression*)m;
}

static Expression* parse_call_expression(Parser* p, Expression* function) {
    CallExpression* expr = malloc(sizeof(CallExpression));
    expr->expression.node.type = NODE_CALL_EXPRESSION;
    expr->token = p->curToken; // the LPAREN
    expr->function = function;
    expr->arguments = parse_expression_list(p, TOKEN_RPAREN, &expr->num_arguments);
    return (Expression*)expr;
}

static Expression* parse_index_expression(Parser* p, Expression* left) {
    IndexExpression* expr = malloc(sizeof(IndexExpression));
    expr->expression.node.type = NODE_INDEX_EXPRESSION;
    expr->token = p->curToken; // the LBRACKET
    expr->left = left;
    next_token_p(p);
    expr->index = parse_expression(p, PREC_LOWEST);
    if (!expect_peek(p, TOKEN_RBRACKET)) {
        // parse error; the partial expression is still returned
    }
    return (Expression*)expr;
}

// Parses a list of expressions separated by ',': a, b, c
static Expression** parse_expression_list(Parser* p, TokenType ending, int* out_count) {
    Expression** list = NULL;
    int n = 0;

    if (p->peekToken.type == ending) {
        next_token_p(p);
        *out_count = 0;
        return list;
    }

    next_token_p(p);
    list = realloc(list, sizeof(Expression*));
    list[0] = parse_expression(p, PREC_LOWEST);
    n = 1;

    while (p->peekToken.type == TOKEN_COMMA) {
        next_token_p(p); // consume ','
        next_token_p(p);
        list = realloc(list, (n + 1) * sizeof(Expression*));
        list[n] = parse_expression(p, PREC_LOWEST);
        n++;
    }

    if (!expect_peek(p, ending)) {
        // parse error; caller may still use the partial list
    }

    *out_count = n;
    return list;
}

static Expression* parse_infix_expression(Parser* p, Expression* left) {
    InfixExpression* expr = malloc(sizeof(InfixExpression));
    expr->expression.node.type = NODE_INFIX_EXPRESSION;
    expr->token = p->curToken;
    expr->operator = strdup(p->curToken.literal); // the AST owns its own copy
    expr->left = left;

    Precedence precedence = get_rule(p->curToken.type)->precedence;
    next_token_p(p);
    expr->right = parse_expression(p, precedence);

    return (Expression*)expr;
}


// =================================================================
// AST Node Implementations
// =================================================================

Program* new_program() {
    Program* p = malloc(sizeof(Program));
    p->node.type = NODE_PROGRAM;
    p->statements = NULL;
    p->num_statements = 0;
    return p;
}

// =================================================================
// Deep AST freeing
// =================================================================
// Every node struct and every strdup'd string owned by the AST is
// released here. Token literals are NOT freed: they are owned by the
// lexer and die together with it in free_lexer().

static void free_expression(Expression* e) {
    if (!e) return;
    switch (e->node.type) {
        case NODE_IDENTIFIER: {
            Identifier* id = (Identifier*)e;
            free((void*)id->value);
            free(id);
            break;
        }
        case NODE_STRING_LITERAL: {
            StringLiteral* sl = (StringLiteral*)e;
            free((void*)sl->value);
            free(sl);
            break;
        }
        case NODE_ARRAY_LITERAL: {
            ArrayLiteral* al = (ArrayLiteral*)e;
            for (int i = 0; i < al->num_elements; i++) free_expression(al->elements[i]);
            free(al->elements);
            free(al);
            break;
        }
        case NODE_MAP_LITERAL: {
            MapLiteral* ml = (MapLiteral*)e;
            for (int i = 0; i < ml->num_pairs; i++) {
                free_expression(ml->keys[i]);
                free_expression(ml->values[i]);
            }
            free(ml->keys);
            free(ml->values);
            free(ml);
            break;
        }
        case NODE_PREFIX_EXPRESSION: {
            PrefixExpression* px = (PrefixExpression*)e;
            free_expression(px->right);
            free((void*)px->operator);
            free(px);
            break;
        }
        case NODE_INFIX_EXPRESSION: {
            InfixExpression* ix = (InfixExpression*)e;
            free_expression(ix->left);
            free_expression(ix->right);
            free((void*)ix->operator);
            free(ix);
            break;
        }
        case NODE_CALL_EXPRESSION: {
            CallExpression* ce = (CallExpression*)e;
            free_expression(ce->function);
            for (int i = 0; i < ce->num_arguments; i++) free_expression(ce->arguments[i]);
            free(ce->arguments);
            free(ce);
            break;
        }
        case NODE_INDEX_EXPRESSION: {
            IndexExpression* ie = (IndexExpression*)e;
            free_expression(ie->left);
            free_expression(ie->index);
            free(ie);
            break;
        }
        default:
            // Literals (int/float/bool/nil) have no children to free.
            free(e);
            break;
    }
}

static void free_identifier(Identifier* id) {
    if (!id) return;
    free((void*)id->value);
    free(id);
}

static void free_statement(Statement* s) {
    if (!s) return;
    switch (s->node.type) {
        case NODE_LET_STATEMENT: {
            LetStatement* ls = (LetStatement*)s;
            free_identifier(ls->name);
            free_identifier(ls->type);
            free_expression(ls->value);
            free(ls);
            break;
        }
        case NODE_RETURN_STATEMENT: {
            ReturnStatement* rs = (ReturnStatement*)s;
            free_expression(rs->return_value);
            free(rs);
            break;
        }
        case NODE_FN_STATEMENT: {
            FunctionStatement* fs = (FunctionStatement*)s;
            free_identifier(fs->name);
            free_identifier(fs->return_type);
            for (int i = 0; i < fs->num_parameters; i++) {
                free_identifier(fs->parameters[i].name);
                free_identifier(fs->parameters[i].type);
            }
            free(fs->parameters);
            free_statement((Statement*)fs->body);
            free(fs);
            break;
        }
        case NODE_IF_STATEMENT: {
            IfStatement* is = (IfStatement*)s;
            free_expression(is->condition);
            free_statement((Statement*)is->consequence);
            free_statement((Statement*)is->alternative);
            free(is);
            break;
        }
        case NODE_WHILE_STATEMENT: {
            WhileStatement* ws = (WhileStatement*)s;
            free_expression(ws->condition);
            free_statement((Statement*)ws->body);
            free(ws);
            break;
        }
        case NODE_EXPRESSION_STATEMENT: {
            ExpressionStatement* es = (ExpressionStatement*)s;
            free_expression(es->expression);
            free(es);
            break;
        }
        case NODE_BLOCK_STATEMENT: {
            BlockStatement* blk = (BlockStatement*)s;
            for (int i = 0; i < blk->num_statements; i++) free_statement(blk->statements[i]);
            free(blk->statements);
            free(blk);
            break;
        }
        default:
            free(s); // cont, break — no children
            break;
    }
}

void free_program(Program* p) {
    if (!p) return;
    for (int i = 0; i < p->num_statements; i++) {
        free_statement(p->statements[i]);
    }
    free(p->statements);
    free(p);
}
