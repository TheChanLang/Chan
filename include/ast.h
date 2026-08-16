#ifndef CHAN_AST_H
#define CHAN_AST_H

#include <stdint.h>
#include "token.h"

typedef enum {
    NODE_PROGRAM,
    NODE_LET_STATEMENT,
    NODE_RETURN_STATEMENT,
    NODE_FN_STATEMENT,
    NODE_IF_STATEMENT,
    NODE_WHILE_STATEMENT,
    NODE_CONT_STATEMENT,
    NODE_BREAK_STATEMENT,
    NODE_EXPRESSION_STATEMENT,
    NODE_BLOCK_STATEMENT,
    NODE_IDENTIFIER,
    NODE_INTEGER_LITERAL,
    NODE_FLOAT_LITERAL,
    NODE_STRING_LITERAL,
    NODE_BOOLEAN_LITERAL,
    NODE_NIL_LITERAL,
    NODE_ARRAY_LITERAL,
    NODE_MAP_LITERAL,
    NODE_PREFIX_EXPRESSION,
    NODE_INFIX_EXPRESSION,
    NODE_CALL_EXPRESSION,
    NODE_INDEX_EXPRESSION,
} NodeType;

// The base Node interface
typedef struct Node {
    NodeType type;
} Node;

// All expression nodes implement this
typedef struct Expression {
    Node node;
} Expression;

// All statement nodes implement this
typedef struct Statement {
    Node node;
} Statement;

// An integer literal
typedef struct IntegerLiteral {
    Expression expression;
    Token token;
    int64_t value;
} IntegerLiteral;

// A float literal — Chan keeps int and float as separate types
typedef struct FloatLiteral {
    Expression expression;
    Token token;
    double value;
} FloatLiteral;

// A string literal
typedef struct StringLiteral {
    Expression expression;
    Token token;
    const char* value; // escapes already processed by the lexer
} StringLiteral;

// A boolean literal: true / false
typedef struct BooleanLiteral {
    Expression expression;
    Token token;
    int value;
} BooleanLiteral;

// The nil literal
typedef struct NilLiteral {
    Expression expression;
    Token token;
} NilLiteral;

// An array literal: [1, 2, 3]
typedef struct ArrayLiteral {
    Expression expression;
    Token token;
    Expression** elements;
    int num_elements;
} ArrayLiteral;

// A map literal: {"a": 1, "b": 2}
typedef struct MapLiteral {
    Expression expression;
    Token token;
    Expression** keys;
    Expression** values;
    int num_pairs;
} MapLiteral;

// A prefix expression: -x, !x, copy x
typedef struct PrefixExpression {
    Expression expression;
    Token token;
    const char* operator;
    Expression* right;
} PrefixExpression;

// An infix expression, like `5 + 5`
typedef struct InfixExpression {
    Expression expression;
    Token token; // The operator token, e.g. +
    Expression* left;
    const char* operator;
    Expression* right;
} InfixExpression;

// A call expression: fib(1, 2)
typedef struct CallExpression {
    Expression expression;
    Token token;
    Expression* function;
    Expression** arguments;
    int num_arguments;
} CallExpression;

// An index expression: a[i] or m["k"]
typedef struct IndexExpression {
    Expression expression;
    Token token;
    Expression* left;
    Expression* index;
} IndexExpression;

// The root node of every AST
typedef struct Program {
    Statement** statements;
    int num_statements;
    Node node;
} Program;

// An identifier
typedef struct Identifier {
    Expression expression; // Implements Expression
    Token token;           // The TOKEN_IDENT token
    const char* value;
} Identifier;

// A let statement: let <name>: <type> = <value>;
typedef struct LetStatement {
    Statement statement;     // Implements Statement
    Token token;             // The TOKEN_LET token
    Identifier* name;
    Identifier* type;        // The type identifier (e.g., 'int')
    Expression* value;       // Can be NULL for default value
} LetStatement;

// A return statement: return <value>;
typedef struct ReturnStatement {
    Statement statement;
    Token token;             // The TOKEN_RETURN token
    Expression* return_value;
} ReturnStatement;

// A block of statements: { ... }
typedef struct BlockStatement {
    Statement statement;
    Token token;             // The TOKEN_LBRACE token
    Statement** statements;
    int num_statements;
} BlockStatement;

// A function parameter: name with an optional type
typedef struct Parameter {
    Identifier* name;
    Identifier* type;        // May be NULL
} Parameter;

// A function statement: fn <name>(a: int# b: int) <ret> { ... }
typedef struct FunctionStatement {
    Statement statement;
    Token token;             // The TOKEN_FN token
    Identifier* name;
    Parameter* parameters;
    int num_parameters;
    Identifier* return_type; // May be NULL
    BlockStatement* body;
} FunctionStatement;

// An if statement. elif is parsed as a nested else { if ... }.
typedef struct IfStatement {
    Statement statement;
    Token token;             // The TOKEN_IF token
    Expression* condition;
    BlockStatement* consequence;
    BlockStatement* alternative; // else block (or a block wrapping an elif)
} IfStatement;

// A while statement
typedef struct WhileStatement {
    Statement statement;
    Token token;             // The TOKEN_WHILE token
    Expression* condition;
    BlockStatement* body;
} WhileStatement;

// cont (continue)
typedef struct ContStatement {
    Statement statement;
    Token token;
} ContStatement;

// break
typedef struct BreakStatement {
    Statement statement;
    Token token;
} BreakStatement;

// An expression statement
typedef struct ExpressionStatement {
    Statement statement;
    Token token; // The first token of the expression
    Expression* expression;
} ExpressionStatement;


// Create a new program
Program* new_program();
void free_program(Program* p);

#endif //CHAN_AST_H
