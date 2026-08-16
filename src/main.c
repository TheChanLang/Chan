#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "interp.h"
#include "ast.h"

// =================================================================
// Host C functions. Chan the library has NO builtin IO — everything
// comes from the embedding host, like V8.
// =================================================================

static Value c_print(Chan* c, Value* args, int n, void* ud) {
    (void)ud;
    for (int i = 0; i < n; i++) {
        char* s = value_to_string(&args[i]);
        fputs(s, stdout);
        if (i < n - 1) fputc(' ', stdout);
        free(s);
        chan_drop(&args[i]);
    }
    fputc('\n', stdout);
    return mk_nil();
}

static Value c_len(Chan* c, Value* args, int n, void* ud) {
    (void)ud;
    if (n != 1) { chan_error(c, "len: expected 1 argument"); return mk_nil(); }
    Value* v = &args[0];
    if (v->type == VAL_STR)  { int64_t l = (int64_t)strlen(v->as.s); chan_drop(&args[0]); return mk_int(l); }
    if (v->type == VAL_ARRAY){ int64_t l = v->as.arr->len; chan_drop(&args[0]); return mk_int(l); }
    if (v->type == VAL_MAP)  { int64_t l = map_count(v->as.map); chan_drop(&args[0]); return mk_int(l); }
    chan_error(c, "len: cannot take length of %s", value_type_name(v->type));
    return mk_nil();
}

static Value c_push(Chan* c, Value* args, int n, void* ud) {
    (void)ud;
    if (n != 2 || args[0].type != VAL_ARRAY) {
        chan_error(c, "push: expected (array, value)");
        return mk_nil();
    }
    array_push(args[0].as.arr, chan_take(&args[1]));
    return chan_take(&args[0]);
}

// =================================================================
// AST dump (debug mode: chan --ast)
// =================================================================

static void print_expression(Expression* expr);
static void print_statement(Statement* stmt);

static void print_block(BlockStatement* block) {
    printf("{\n");
    for (int i = 0; i < block->num_statements; i++) {
        print_statement(block->statements[i]);
    }
    printf("}");
}

static void print_statement(Statement* stmt) {
    if (!stmt) return;

    switch (stmt->node.type) {
        case NODE_LET_STATEMENT: {
            LetStatement* let_stmt = (LetStatement*)stmt;
            printf("let %s: %s = ", let_stmt->name->value, let_stmt->type->value);
            print_expression(let_stmt->value);
            printf("\n");
            break;
        }
        case NODE_RETURN_STATEMENT: {
            ReturnStatement* ret = (ReturnStatement*)stmt;
            printf("return ");
            print_expression(ret->return_value);
            printf("\n");
            break;
        }
        case NODE_FN_STATEMENT: {
            FunctionStatement* fn = (FunctionStatement*)stmt;
            printf("fn %s(", fn->name->value);
            for (int i = 0; i < fn->num_parameters; i++) {
                Parameter* p = &fn->parameters[i];
                printf("%s", p->name->value);
                if (p->type) printf(": %s", p->type->value);
                if (i < fn->num_parameters - 1) printf(", ");
            }
            printf(")");
            if (fn->return_type) printf(": %s", fn->return_type->value);
            printf(" ");
            print_block(fn->body);
            printf("\n");
            break;
        }
        case NODE_IF_STATEMENT: {
            IfStatement* if_stmt = (IfStatement*)stmt;
            printf("if ");
            print_expression(if_stmt->condition);
            printf(" ");
            print_block(if_stmt->consequence);
            if (if_stmt->alternative) {
                printf(" else ");
                print_block(if_stmt->alternative);
            }
            printf("\n");
            break;
        }
        case NODE_WHILE_STATEMENT: {
            WhileStatement* wh = (WhileStatement*)stmt;
            printf("while ");
            print_expression(wh->condition);
            printf(" ");
            print_block(wh->body);
            printf("\n");
            break;
        }
        case NODE_CONT_STATEMENT:
            printf("cont\n");
            break;
        case NODE_BREAK_STATEMENT:
            printf("break\n");
            break;
        case NODE_BLOCK_STATEMENT:
            print_block((BlockStatement*)stmt);
            printf("\n");
            break;
        case NODE_EXPRESSION_STATEMENT: {
            ExpressionStatement* es = (ExpressionStatement*)stmt;
            print_expression(es->expression);
            printf("\n");
            break;
        }
        default:
            printf("Unknown statement type: %d\n", stmt->node.type);
            break;
    }
}

static void print_expression(Expression* expr) {
    if (!expr) {
        printf("<null expr>");
        return;
    }

    switch (expr->node.type) {
        case NODE_IDENTIFIER: {
            Identifier* ident = (Identifier*)expr;
            printf("%s", ident->value);
            break;
        }
        case NODE_INTEGER_LITERAL: {
            IntegerLiteral* integer = (IntegerLiteral*)expr;
            printf("%lld", integer->value);
            break;
        }
        case NODE_FLOAT_LITERAL: {
            FloatLiteral* f = (FloatLiteral*)expr;
            printf("%g", f->value);
            break;
        }
        case NODE_STRING_LITERAL: {
            StringLiteral* s = (StringLiteral*)expr;
            printf("\"%s\"", s->value);
            break;
        }
        case NODE_BOOLEAN_LITERAL: {
            BooleanLiteral* b = (BooleanLiteral*)expr;
            printf(b->value ? "true" : "false");
            break;
        }
        case NODE_NIL_LITERAL:
            printf("nil");
            break;
        case NODE_ARRAY_LITERAL: {
            ArrayLiteral* arr = (ArrayLiteral*)expr;
            printf("[");
            for (int i = 0; i < arr->num_elements; i++) {
                print_expression(arr->elements[i]);
                if (i < arr->num_elements - 1) printf(", ");
            }
            printf("]");
            break;
        }
        case NODE_MAP_LITERAL: {
            MapLiteral* m = (MapLiteral*)expr;
            printf("{");
            for (int i = 0; i < m->num_pairs; i++) {
                if (i) printf(", ");
                print_expression(m->keys[i]);
                printf(": ");
                print_expression(m->values[i]);
            }
            printf("}");
            break;
        }
        case NODE_PREFIX_EXPRESSION: {
            PrefixExpression* pfx = (PrefixExpression*)expr;
            printf("(%s ", pfx->operator);
            print_expression(pfx->right);
            printf(")");
            break;
        }
        case NODE_INFIX_EXPRESSION: {
            InfixExpression* infix = (InfixExpression*)expr;
            printf("(");
            print_expression(infix->left);
            printf(" %s ", infix->operator);
            print_expression(infix->right);
            printf(")");
            break;
        }
        case NODE_CALL_EXPRESSION: {
            CallExpression* call = (CallExpression*)expr;
            print_expression(call->function);
            printf("(");
            for (int i = 0; i < call->num_arguments; i++) {
                print_expression(call->arguments[i]);
                if (i < call->num_arguments - 1) printf(", ");
            }
            printf(")");
            break;
        }
        case NODE_INDEX_EXPRESSION: {
            IndexExpression* ix = (IndexExpression*)expr;
            print_expression(ix->left);
            printf("[");
            print_expression(ix->index);
            printf("]");
            break;
        }
        default:
            printf("Unknown expression type: %d", expr->node.type);
            break;
    }
}

// =================================================================
// Demo program (also examples/demo.chan)
// =================================================================

static const char* DEMO =
    "fn fib(n: int): int {\n"
    "    if n < 2 {\n"
    "        return n\n"
    "    } else {\n"
    "        return fib(n - 1) + fib(n - 2)\n"
    "    }\n"
    "}\n"
    "fn add(a: int, b: int): int {\n"
    "    return a + b\n"
    "}\n"
    "let x: int = fib(10)\n"
    "print(copy x)\n"
    "let y: float = 3.14\n"
    "print(y)\n"
    "let flag: bool = true\n"
    "print(flag)\n"
    "let s: str = \"chan # tiny\"\n"
    "print(copy s)\n"
    "let arr: array = [1, 2.5, x, s]\n"
    "print(copy arr)\n"
    "print(len(copy arr))\n"
    "let m: map = {\"a\": 1, \"b\": 2}\n"
    "m[\"c\"] = 3\n"
    "print(m[\"b\"])\n"
    "print(add(2, 3))\n"
    "let z: int = copy m[\"a\"]\n"
    "print(z)\n"
    "let i: int = 0\n"
    "let total: int = 0\n"
    "while i < 10 {\n"
    "    i = i + 1\n"
    "    if i == 5 {\n"
    "        cont\n"
    "    }\n"
    "    if i == 8 {\n"
    "        break\n"
    "    }\n"
    "    total = total + i\n"
    "}\n"
    "print(total)\n";

// =================================================================
// Main
// =================================================================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Keep \n as \n (text mode would translate it to \r\n) and make
    // stdout unbuffered so stdout/stderr interleave in real order.
    _setmode(_fileno(stdout), _O_BINARY);
    setvbuf(stdout, NULL, _IONBF, 0);
#endif
    Chan* c = chan_new();
    // The core has no IO: the host registers what it wants.
    chan_register(c, "print", c_print, NULL);
    chan_register(c, "len", c_len, NULL);
    chan_register(c, "push", c_push, NULL);

    if (argc >= 2 && strcmp(argv[1], "--ast") == 0) {
        printf("--- AST of the embedded demo ---\n");
        Program* p = chan_parse(c, DEMO);
        if (!p) { fprintf(stderr, "parse error: %s\n", chan_error_msg(c)); chan_free(c); return 1; }
        for (int i = 0; i < p->num_statements; i++) print_statement(p->statements[i]);
        free_program(p);
        chan_free(c);
        return 0;
    }

    const char* src = DEMO;
    char* filebuf = NULL;
    if (argc >= 2) {
        FILE* f = fopen(argv[1], "rb");
        if (!f) { fprintf(stderr, "chan: cannot open '%s'\n", argv[1]); chan_free(c); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        filebuf = malloc(sz + 1);
        if (sz > 0 && fread(filebuf, 1, (size_t)sz, f) != (size_t)sz) {
            fprintf(stderr, "chan: read error\n");
            fclose(f);
            free(filebuf);
            chan_free(c);
            return 1;
        }
        filebuf[sz] = '\0';
        fclose(f);
        src = filebuf;
    }

    Program* p = chan_parse(c, src);
    if (!p) {
        fprintf(stderr, "chan: parse error: %s\n", chan_error_msg(c));
        free(filebuf);
        chan_free(c);
        return 1;
    }

    Value out;
    if (chan_run(c, p, &out) != 0) {
        fprintf(stderr, "chan: error: %s\n", chan_error_msg(c));
        free_program(p);
        free(filebuf);
        chan_free(c);
        return 1;
    }
    if (out.type != VAL_NIL) {
        char* s = value_to_string(&out);
        printf("=> %s\n", s);
        free(s);
    }
    free_value(&out);

    free_program(p);
    free(filebuf);
    chan_free(c);
    return 0;
}
