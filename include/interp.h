#ifndef CHAN_INTERP_H
#define CHAN_INTERP_H

#include "value.h"
#include "ast.h"

// A variable binding. Values are moved (the binding is deleted from the
// table), never copied, unless the program says `copy`.
typedef struct Binding {
    char* name;
    Value val;
    ValueType declared;  // VAL_NONE if the variable is untyped (host-registered)
    struct Binding* next;
} Binding;

typedef struct Scope {
    Binding* head;
    struct Scope* parent;
} Scope;

// One Chan instance per thread. Chan has no global state, so embedding
// applications can run many instances on separate threads.
typedef struct Chan {
    Scope* scope;
    int loop_depth;
    int flow;         // 0 none, 1 return, 2 break, 3 cont
    Value ret_val;    // value carried by a return
    char err[256];
    int line;         // current source line (for error messages)
} Chan;

// --- lifecycle ---
Chan* chan_new(void);
void chan_free(Chan* c);

// --- parsing ---
Program* chan_parse(Chan* c, const char* src); // NULL on error (c->err set)

// --- execution ---
// Runs the program in the global scope. Returns 0 on success, -1 on error
// (c->err is set). If `out` is non-NULL it receives an owned copy of the
// last statement's value; the caller must free_value() it.
int chan_run(Chan* c, Program* p, Value* out);

// --- embedding (the core has no IO; the host provides it) ---
void chan_register(Chan* c, const char* name, CFn fn, void* ud);
int chan_get(Chan* c, const char* name, Value* out); // owned copy; 0 ok, -1 not found

// --- errors ---
const char* chan_error_msg(Chan* c);
void chan_error(Chan* c, const char* fmt, ...);

#endif //CHAN_INTERP_H
