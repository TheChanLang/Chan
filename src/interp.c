#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "interp.h"
#include "lexer.h"
#include "parser.h"

enum { FLOW_NONE = 0, FLOW_RETURN = 1, FLOW_BREAK = 2, FLOW_CONT = 3 };

// =================================================================
// Scopes
// =================================================================

static Scope* scope_new(Scope* parent) {
    Scope* s = malloc(sizeof(Scope));
    s->head = NULL;
    s->parent = parent;
    return s;
}

static void scope_free(Scope* s) {
    Binding* b = s->head;
    while (b) {
        Binding* nx = b->next;
        free(b->name);
        free_value(&b->val);
        free(b);
        b = nx;
    }
    free(s);
}

static Binding* scope_find(Scope* s, const char* name) {
    for (; s; s = s->parent) {
        for (Binding* b = s->head; b; b = b->next) {
            if (!strcmp(b->name, name)) return b;
        }
    }
    return NULL;
}

static void scope_declare(Chan* c, const char* name, Value v, ValueType declared) {
    Binding* b = malloc(sizeof(Binding));
    b->name = strdup(name);
    b->val = v;
    b->declared = declared;
    b->next = c->scope->head;
    c->scope->head = b;
}

// Read: borrow. The returned value is not owned; do not free its data.
static Value scope_get(Scope* s, const char* name, int* found) {
    Binding* b = scope_find(s, name);
    if (!b) { *found = 0; return mk_nil(); }
    Value v = b->val;
    v.owned = 0;
    *found = 1;
    return v;
}

// Move: delete the binding from the table and hand its value to the caller.
// This is how Chan saves resources — a moved variable no longer exists.
static Value scope_take(Scope* s, const char* name, int* found) {
    for (Scope* sc = s; sc; sc = sc->parent) {
        Binding** pp = &sc->head;
        while (*pp) {
            if (!strcmp((*pp)->name, name)) {
                Binding* b = *pp;
                Value v = b->val;
                *pp = b->next;
                free(b->name);
                free(b);
                *found = 1;
                return v;
            }
            pp = &(*pp)->next;
        }
    }
    *found = 0;
    return mk_nil();
}

// =================================================================
// Types
// =================================================================

static ValueType type_from_name(const char* n) {
    if (!n) return VAL_NONE;
    if (!strcmp(n, "bool"))  return VAL_BOOL;
    if (!strcmp(n, "int"))   return VAL_INT;
    if (!strcmp(n, "float")) return VAL_FLOAT;
    if (!strcmp(n, "str"))   return VAL_STR;
    if (!strcmp(n, "array")) return VAL_ARRAY;
    if (!strcmp(n, "map"))   return VAL_MAP;
    if (!strcmp(n, "nil"))   return VAL_NIL;
    if (!strcmp(n, "obj"))   return VAL_OBJ;
    return VAL_NONE;
}

static int type_ok(ValueType declared, Value v) {
    return declared == VAL_NONE || declared == v.type;
}

// =================================================================
// Lifecycle
// =================================================================

Chan* chan_new(void) {
    Chan* c = calloc(1, sizeof(Chan));
    c->scope = scope_new(NULL);
    return c;
}

void chan_free(Chan* c) {
    if (!c) return;
    while (c->scope) {
        Scope* s = c->scope;
        c->scope = s->parent;
        scope_free(s);
    }
    free_value(&c->ret_val);
    free(c);
}

void chan_error(Chan* c, const char* fmt, ...) {
    if (!c || c->err[0]) return;
    char tmp[220];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    snprintf(c->err, sizeof c->err, "line %d: %s", c->line, tmp);
}

const char* chan_error_msg(Chan* c) {
    return c->err;
}

void chan_register(Chan* c, const char* name, CFn fn, void* ud) {
    scope_declare(c, name, mk_cfn(fn, ud), VAL_NONE);
}

int chan_get(Chan* c, const char* name, Value* out) {
    Binding* b = scope_find(c->scope, name);
    if (!b) return -1;
    *out = copy_value(&b->val);
    return 0;
}

// =================================================================
// Evaluation
// =================================================================

static Value eval_expr(Chan* c, Node* e, int move);
static Value eval_stmt(Chan* c, Node* s);
static Value eval_block(Chan* c, Node* b);
static Value call_value(Chan* c, Value callee, Node* call);

static int stmt_line(Node* s) {
    switch (s->type) {
        case NODE_LET_STATEMENT:     return ((LetStatement*)s)->token.line;
        case NODE_RETURN_STATEMENT:  return ((ReturnStatement*)s)->token.line;
        case NODE_FN_STATEMENT:      return ((FunctionStatement*)s)->token.line;
        case NODE_IF_STATEMENT:      return ((IfStatement*)s)->token.line;
        case NODE_WHILE_STATEMENT:   return ((WhileStatement*)s)->token.line;
        case NODE_CONT_STATEMENT:    return ((ContStatement*)s)->token.line;
        case NODE_BREAK_STATEMENT:   return ((BreakStatement*)s)->token.line;
        case NODE_EXPRESSION_STATEMENT: return ((ExpressionStatement*)s)->token.line;
        default: return 0;
    }
}

static int expr_line(Node* e) {
    switch (e->type) {
        case NODE_IDENTIFIER:        return ((Identifier*)e)->token.line;
        case NODE_INTEGER_LITERAL:   return ((IntegerLiteral*)e)->token.line;
        case NODE_FLOAT_LITERAL:     return ((FloatLiteral*)e)->token.line;
        case NODE_STRING_LITERAL:    return ((StringLiteral*)e)->token.line;
        case NODE_BOOLEAN_LITERAL:   return ((BooleanLiteral*)e)->token.line;
        case NODE_NIL_LITERAL:       return ((NilLiteral*)e)->token.line;
        case NODE_ARRAY_LITERAL:     return ((ArrayLiteral*)e)->token.line;
        case NODE_MAP_LITERAL:       return ((MapLiteral*)e)->token.line;
        case NODE_PREFIX_EXPRESSION: return ((PrefixExpression*)e)->token.line;
        case NODE_INFIX_EXPRESSION:  return ((InfixExpression*)e)->token.line;
        case NODE_CALL_EXPRESSION:   return ((CallExpression*)e)->token.line;
        case NODE_INDEX_EXPRESSION:  return ((IndexExpression*)e)->token.line;
        default: return 0;
    }
}

// Evaluate a block in its own child scope.
static Value eval_block_scope(Chan* c, Node* b) {
    Scope* parent = c->scope;
    Scope* ns = scope_new(parent);
    c->scope = ns;
    Value v = eval_block(c, b);
    c->scope = parent;
    scope_free(ns);
    return v;
}

static Value eval_block(Chan* c, Node* b) {
    Value last = mk_nil();
    if (!b) return last;
    BlockStatement* blk = (BlockStatement*)b;
    for (int i = 0; i < blk->num_statements; i++) {
        if (c->err[0] || c->flow) break;
        free_value(&last);
        last = eval_stmt(c, blk->statements[i]);
    }
    return last;
}

static Value eval_stmt(Chan* c, Node* s) {
    if (!s || c->err[0]) return mk_nil();
    switch (s->type) {
        case NODE_LET_STATEMENT: {
            LetStatement* ls = (LetStatement*)s;
            c->line = ls->token.line;
            Value v = eval_expr(c, ls->value, 1); // move the value in
            if (c->err[0]) { free_value(&v); return mk_nil(); }
            ValueType dt = type_from_name(ls->type->value);
            if (!type_ok(dt, v)) {
                chan_error(c, "'%s': type mismatch, expected %s, got %s",
                           ls->name->value, ls->type->value, value_type_name(v.type));
                free_value(&v);
                return mk_nil();
            }
            scope_declare(c, ls->name->value, v, dt);
            return mk_nil();
        }
        case NODE_RETURN_STATEMENT: {
            ReturnStatement* rs = (ReturnStatement*)s;
            c->line = rs->token.line;
            Value v = eval_expr(c, rs->return_value, 1); // move out of the frame
            if (c->err[0]) { free_value(&v); return mk_nil(); }
            free_value(&c->ret_val);
            c->ret_val = v;
            c->flow = FLOW_RETURN;
            return mk_nil();
        }
        case NODE_FN_STATEMENT: {
            FunctionStatement* fs = (FunctionStatement*)s;
            c->line = fs->token.line;
            ChanFunc* f = calloc(1, sizeof(ChanFunc));
            f->name = strdup(fs->name->value);
            f->ret_type = fs->return_type ? strdup(fs->return_type->value) : NULL;
            f->nparams = fs->num_parameters;
            if (f->nparams > 0) {
                f->params = malloc(f->nparams * sizeof(char*));
                f->param_types = malloc(f->nparams * sizeof(char*));
                for (int i = 0; i < f->nparams; i++) {
                    f->params[i] = strdup(fs->parameters[i].name->value);
                    f->param_types[i] = fs->parameters[i].type ? strdup(fs->parameters[i].type->value) : NULL;
                }
            }
            f->body = (Node*)fs->body;
            scope_declare(c, f->name, mk_func(f), VAL_NONE);
            return mk_nil();
        }
        case NODE_IF_STATEMENT: {
            IfStatement* is = (IfStatement*)s;
            c->line = is->token.line;
            Value cv = eval_expr(c, is->condition, 0);
            if (c->err[0]) { free_value(&cv); return mk_nil(); }
            int t = value_truthy(&cv);
            free_value(&cv);
            if (t) return eval_block_scope(c, (Node*)is->consequence);
            if (is->alternative) return eval_block_scope(c, (Node*)is->alternative);
            return mk_nil();
        }
        case NODE_WHILE_STATEMENT: {
            WhileStatement* ws = (WhileStatement*)s;
            c->line = ws->token.line;
            c->loop_depth++;
            Value r = mk_nil();
            while (1) {
                if (c->err[0] || c->flow) break;
                Value cv = eval_expr(c, ws->condition, 0);
                if (c->err[0]) { free_value(&cv); break; }
                int t = value_truthy(&cv);
                free_value(&cv);
                if (!t) break;
                free_value(&r);
                r = eval_block_scope(c, (Node*)ws->body);
                if (c->err[0]) break;
                if (c->flow == FLOW_BREAK) { c->flow = FLOW_NONE; break; }
                if (c->flow == FLOW_CONT) { c->flow = FLOW_NONE; continue; }
                // FLOW_RETURN propagates out of the loop
            }
            c->loop_depth--;
            free_value(&r);
            return mk_nil();
        }
        case NODE_CONT_STATEMENT: {
            ContStatement* cs = (ContStatement*)s;
            c->line = cs->token.line;
            if (!c->loop_depth) chan_error(c, "cont outside of a loop");
            else c->flow = FLOW_CONT;
            return mk_nil();
        }
        case NODE_BREAK_STATEMENT: {
            BreakStatement* bs = (BreakStatement*)s;
            c->line = bs->token.line;
            if (!c->loop_depth) chan_error(c, "break outside of a loop");
            else c->flow = FLOW_BREAK;
            return mk_nil();
        }
        case NODE_EXPRESSION_STATEMENT: {
            ExpressionStatement* es = (ExpressionStatement*)s;
            c->line = es->token.line;
            return eval_expr(c, es->expression, 0);
        }
        default:
            return mk_nil();
    }
}

// =================================================================
// Expressions
// =================================================================

static int is_num(Value* v) { return v->type == VAL_INT || v->type == VAL_FLOAT; }
static double as_double(Value* v) { return v->type == VAL_INT ? (double)v->as.i : v->as.f; }

static Value num_op(Chan* c, const char* op, Value* l, Value* r, int* ok) {
    *ok = 1;
    if (!is_num(l) || !is_num(r)) { *ok = 0; return mk_nil(); }
    if (l->type == VAL_INT && r->type == VAL_INT) {
        int64_t a = l->as.i, b = r->as.i;
        if (!strcmp(op, "+")) return mk_int(a + b);
        if (!strcmp(op, "-")) return mk_int(a - b);
        if (!strcmp(op, "*")) return mk_int(a * b);
        if (!strcmp(op, "/")) {
            if (b == 0) { chan_error(c, "division by zero"); *ok = 0; return mk_nil(); }
            return mk_int(a / b);
        }
        if (!strcmp(op, "%")) {
            if (b == 0) { chan_error(c, "modulo by zero"); *ok = 0; return mk_nil(); }
            return mk_int(a % b);
        }
    }
    double a = as_double(l), b = as_double(r);
    if (!strcmp(op, "+")) return mk_float(a + b);
    if (!strcmp(op, "-")) return mk_float(a - b);
    if (!strcmp(op, "*")) return mk_float(a * b);
    if (!strcmp(op, "/")) {
        if (b == 0.0) { chan_error(c, "division by zero"); *ok = 0; return mk_nil(); }
        return mk_float(a / b);
    }
    *ok = 0;
    return mk_nil();
}

static Value cmp_op(const char* op, Value* l, Value* r, int* ok) {
    *ok = 1;
    int res;
    if (l->type == VAL_STR && r->type == VAL_STR) {
        res = strcmp(l->as.s, r->as.s);
    } else if (is_num(l) && is_num(r)) {
        double a = as_double(l), b = as_double(r);
        res = a < b ? -1 : (a > b ? 1 : 0);
    } else {
        *ok = 0;
        return mk_nil();
    }
    if (!strcmp(op, "<"))  return mk_bool(res < 0);
    if (!strcmp(op, ">"))  return mk_bool(res > 0);
    if (!strcmp(op, "<=")) return mk_bool(res <= 0);
    return mk_bool(res >= 0); // ">="
}

static Value eval_assign(Chan* c, InfixExpression* e) {
    // LHS must be a variable or an index: x = v  |  a[i] = v  |  m[k] = v
    Value v = eval_expr(c, e->right, 1); // move the RHS
    if (c->err[0]) { free_value(&v); return mk_nil(); }

    if (e->left->node.type == NODE_IDENTIFIER) {
        Identifier* id = (Identifier*)e->left;
        Binding* b = scope_find(c->scope, id->value);
        if (!b) {
            chan_error(c, "undefined variable '%s'", id->value);
            free_value(&v);
            return mk_nil();
        }
        if (!type_ok(b->declared, v)) {
            chan_error(c, "'%s': type mismatch, expected %s, got %s",
                       id->value, value_type_name(b->declared), value_type_name(v.type));
            free_value(&v);
            return mk_nil();
        }
        free_value(&b->val);
        b->val = v;
        return mk_nil();
    }

    if (e->left->node.type == NODE_INDEX_EXPRESSION) {
        IndexExpression* ix = (IndexExpression*)e->left;
        Value base = eval_expr(c, ix->left, 0);
        Value idx = eval_expr(c, ix->index, 0);
        if (c->err[0]) { free_value(&base); free_value(&idx); free_value(&v); return mk_nil(); }
        if (base.type == VAL_ARRAY) {
            if (idx.type != VAL_INT) {
                chan_error(c, "array index must be int");
                free_value(&v);
            } else if (!array_set(base.as.arr, idx.as.i, v)) {
                chan_error(c, "index %lld out of range (len %d)", idx.as.i, base.as.arr->len);
                free_value(&v);
            } else {
                v = mk_nil(); // moved into the array
            }
        } else if (base.type == VAL_MAP) {
            map_set(base.as.map, idx, v);
            v = mk_nil(); // moved into the map
        } else {
            chan_error(c, "cannot index %s", value_type_name(base.type));
            free_value(&v);
        }
        free_value(&base);
        free_value(&idx);
        return mk_nil();
    }

    chan_error(c, "invalid assignment target");
    free_value(&v);
    return mk_nil();
}

static Value eval_infix(Chan* c, InfixExpression* e) {
    const char* op = e->operator;

    if (!strcmp(op, "=")) return eval_assign(c, e);

    if (!strcmp(op, "&&")) {
        Value l = eval_expr(c, e->left, 0);
        if (c->err[0]) { free_value(&l); return mk_nil(); }
        int t = value_truthy(&l);
        free_value(&l);
        if (!t) return mk_bool(0);
        Value r = eval_expr(c, e->right, 0);
        if (c->err[0]) return mk_nil();
        int rt = value_truthy(&r);
        free_value(&r);
        return mk_bool(rt);
    }
    if (!strcmp(op, "||")) {
        Value l = eval_expr(c, e->left, 0);
        if (c->err[0]) { free_value(&l); return mk_nil(); }
        int t = value_truthy(&l);
        free_value(&l);
        if (t) return mk_bool(1);
        Value r = eval_expr(c, e->right, 0);
        if (c->err[0]) return mk_nil();
        int rt = value_truthy(&r);
        free_value(&r);
        return mk_bool(rt);
    }

    Value l = eval_expr(c, e->left, 0);
    Value r = eval_expr(c, e->right, 0);
    if (c->err[0]) { free_value(&l); free_value(&r); return mk_nil(); }

    Value result = mk_nil();
    if (!strcmp(op, "+") && l.type == VAL_STR && r.type == VAL_STR) {
        int n = (int)strlen(l.as.s) + (int)strlen(r.as.s);
        char* buf = malloc(n + 1);
        strcpy(buf, l.as.s);
        strcat(buf, r.as.s);
        result = mk_str(buf);
    } else if (!strcmp(op, "==")) {
        result = mk_bool(value_eq(&l, &r));
    } else if (!strcmp(op, "!=")) {
        result = mk_bool(!value_eq(&l, &r));
    } else if (!strcmp(op, "<") || !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=")) {
        int ok;
        result = cmp_op(op, &l, &r, &ok);
        if (!ok) chan_error(c, "cannot compare %s with %s", value_type_name(l.type), value_type_name(r.type));
    } else if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) {
        int ok;
        result = num_op(c, op, &l, &r, &ok);
        if (!ok && !c->err[0]) {
            chan_error(c, "cannot apply '%s' to %s and %s", op, value_type_name(l.type), value_type_name(r.type));
        }
    } else {
        chan_error(c, "unknown operator '%s'", op);
    }

    free_value(&l);
    free_value(&r);
    return result;
}

static Value call_func(Chan* c, ChanFunc* f, Node* call) {
    CallExpression* ce = (CallExpression*)call;
    int n = ce->num_arguments;

    Scope* parent = c->scope;
    Scope* ns = scope_new(parent);
    c->scope = ns;

    if (n != f->nparams) {
        chan_error(c, "%s: expected %d arguments, got %d", f->name, f->nparams, n);
    } else {
        for (int i = 0; i < n; i++) {
            Value v = eval_expr(c, ce->arguments[i], 1); // move args into the frame
            if (c->err[0]) { free_value(&v); break; }
            ValueType dt = type_from_name(f->param_types[i]);
            if (!type_ok(dt, v)) {
                chan_error(c, "%s: argument %d type mismatch, expected %s, got %s",
                           f->name, i + 1, f->param_types[i], value_type_name(v.type));
                free_value(&v);
                break;
            }
            scope_declare(c, f->params[i], v, dt);
        }
    }

    Value r = mk_nil();
    if (!c->err[0]) eval_block(c, f->body);
    if (c->flow == FLOW_RETURN) {
        r = c->ret_val;
        c->ret_val = mk_nil();
        c->flow = FLOW_NONE;
    }

    c->scope = parent;
    scope_free(ns);

    if (f->ret_type && !type_ok(type_from_name(f->ret_type), r)) {
        chan_error(c, "%s: return type mismatch, expected %s, got %s",
                   f->name, f->ret_type, value_type_name(r.type));
        free_value(&r);
        return mk_nil();
    }
    return r;
}

static Value call_value(Chan* c, Value callee, Node* call) {
    if (callee.type == VAL_CFN) {
        CallExpression* ce = (CallExpression*)call;
        int n = ce->num_arguments;
        Value* args = calloc(n ? n : 1, sizeof(Value));
        for (int i = 0; i < n; i++) {
            args[i] = eval_expr(c, ce->arguments[i], 1); // move args in
        }
        Value r = mk_nil();
        if (!c->err[0]) r = callee.as.cfn.fn(c, args, n, callee.as.cfn.ud);
        for (int i = 0; i < n; i++) free_value(&args[i]);
        free(args);
        return r;
    }
    if (callee.type == VAL_FUNC) {
        return call_func(c, callee.as.fn, call);
    }
    chan_error(c, "cannot call %s", value_type_name(callee.type));
    return mk_nil();
}

static Value eval_expr(Chan* c, Node* e, int move) {
    if (c->err[0] || c->flow || !e) return mk_nil();
    switch (e->type) {
        case NODE_INTEGER_LITERAL:
            return mk_int(((IntegerLiteral*)e)->value);
        case NODE_FLOAT_LITERAL:
            return mk_float(((FloatLiteral*)e)->value);
        case NODE_STRING_LITERAL:
            return mk_str_copy(((StringLiteral*)e)->value);
        case NODE_BOOLEAN_LITERAL:
            return mk_bool(((BooleanLiteral*)e)->value);
        case NODE_NIL_LITERAL:
            return mk_nil();
        case NODE_IDENTIFIER: {
            Identifier* id = (Identifier*)e;
            int found;
            if (move) {
                Value v = scope_take(c->scope, id->value, &found);
                if (!found) chan_error(c, "undefined variable '%s'", id->value);
                return v;
            }
            Value v = scope_get(c->scope, id->value, &found);
            if (!found) chan_error(c, "undefined variable '%s'", id->value);
            return v;
        }
        case NODE_ARRAY_LITERAL: {
            ArrayLiteral* al = (ArrayLiteral*)e;
            ChanArray* arr = calloc(1, sizeof(ChanArray));
            for (int i = 0; i < al->num_elements; i++) {
                Value v = eval_expr(c, al->elements[i], 1); // elements move in
                if (c->err[0]) {
                    free_value(&v);
                    Value tmp; tmp.type = VAL_ARRAY; tmp.owned = 1; tmp.as.arr = arr;
                    free_value(&tmp);
                    return mk_nil();
                }
                array_push(arr, v);
            }
            Value r; r.type = VAL_ARRAY; r.owned = 1; r.as.arr = arr;
            return r;
        }
        case NODE_MAP_LITERAL: {
            MapLiteral* ml = (MapLiteral*)e;
            ChanMap* m = calloc(1, sizeof(ChanMap));
            for (int i = 0; i < ml->num_pairs; i++) {
                Value k = eval_expr(c, ml->keys[i], 0);   // key is borrowed (map copies it)
                Value v = eval_expr(c, ml->values[i], 1); // value moves in
                if (c->err[0]) {
                    free_value(&k); free_value(&v);
                    Value tmp; tmp.type = VAL_MAP; tmp.owned = 1; tmp.as.map = m;
                    free_value(&tmp);
                    return mk_nil();
                }
                map_set(m, k, v);
                free_value(&k);
            }
            Value r; r.type = VAL_MAP; r.owned = 1; r.as.map = m;
            return r;
        }
        case NODE_PREFIX_EXPRESSION: {
            PrefixExpression* px = (PrefixExpression*)e;
            if (!strcmp(px->operator, "copy")) {
                Value v = eval_expr(c, px->right, 0); // borrow, then duplicate
                if (c->err[0]) return mk_nil();
                Value r = copy_value(&v);
                free_value(&v);
                return r;
            }
            if (!strcmp(px->operator, "!")) {
                Value v = eval_expr(c, px->right, 0);
                if (c->err[0]) return mk_nil();
                int t = value_truthy(&v);
                free_value(&v);
                return mk_bool(!t);
            }
            // "-"
            Value v = eval_expr(c, px->right, 0);
            if (c->err[0]) return mk_nil();
            if (v.type == VAL_INT) {
                int64_t n = -v.as.i;
                free_value(&v);
                return mk_int(n);
            }
            if (v.type == VAL_FLOAT) {
                double f = -v.as.f;
                free_value(&v);
                return mk_float(f);
            }
            chan_error(c, "cannot negate %s", value_type_name(v.type));
            free_value(&v);
            return mk_nil();
        }
        case NODE_INFIX_EXPRESSION:
            return eval_infix(c, (InfixExpression*)e);
        case NODE_CALL_EXPRESSION: {
            CallExpression* ce = (CallExpression*)e;
            Value callee = eval_expr(c, ce->function, 0);
            if (c->err[0]) { free_value(&callee); return mk_nil(); }
            Value r = call_value(c, callee, ce);
            free_value(&callee);
            return r;
        }
        case NODE_INDEX_EXPRESSION: {
            IndexExpression* ix = (IndexExpression*)e;
            Value base = eval_expr(c, ix->left, 0);
            Value idx = eval_expr(c, ix->index, 0);
            if (c->err[0]) { free_value(&base); free_value(&idx); return mk_nil(); }
            Value r = mk_nil();
            if (base.type == VAL_ARRAY) {
                if (idx.type != VAL_INT) chan_error(c, "array index must be int");
                else if (idx.as.i < 0 || idx.as.i >= base.as.arr->len)
                    chan_error(c, "index %lld out of range (len %d)", idx.as.i, base.as.arr->len);
                else r = move ? array_detach(base.as.arr, idx.as.i) : array_get(base.as.arr, idx.as.i);
            } else if (base.type == VAL_MAP) {
                r = move ? map_detach(base.as.map, idx) : map_get(base.as.map, idx);
            } else if (base.type == VAL_STR) {
                chan_error(c, "cannot index str");
            } else {
                chan_error(c, "cannot index %s", value_type_name(base.type));
            }
            free_value(&base);
            free_value(&idx);
            return r;
        }
        default:
            return mk_nil();
    }
}

// =================================================================
// Public API
// =================================================================

Program* chan_parse(Chan* c, const char* src) {
    Lexer* l = new_lexer(src);
    Parser* p = new_parser(l);
    Program* prog = parse_program(p);
    if (p->num_errors > 0) {
        snprintf(c->err, sizeof c->err, "%s", p->errors[0]);
        free_program(prog);
        prog = NULL;
    }
    free_parser(p);
    free_lexer(l);
    return prog;
}

int chan_run(Chan* c, Program* p, Value* out) {
    if (!c || !p) return -1;
    c->err[0] = '\0';
    c->flow = FLOW_NONE;
    c->line = 0;

    Value last = mk_nil();
    for (int i = 0; i < p->num_statements; i++) {
        if (c->err[0] || c->flow) break;
        free_value(&last);
        last = eval_stmt(c, p->statements[i]);
    }
    c->flow = FLOW_NONE;

    if (c->err[0]) {
        free_value(&last);
        return -1;
    }
    if (out) *out = copy_value(&last);
    free_value(&last);
    return 0;
}
