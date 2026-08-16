#ifndef CHAN_VALUE_H
#define CHAN_VALUE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VAL_NIL,     // nil
    VAL_BOOL,    // bool
    VAL_INT,     // int  — separate from float to save space on small systems
    VAL_FLOAT,   // float
    VAL_STR,     // str
    VAL_ARRAY,   // array
    VAL_MAP,     // map
    VAL_OBJ,     // obj — C-linked value
    VAL_FUNC,    // fn  — user function
    VAL_CFN,     // host C function (registered by the embedding library)
    VAL_NONE,    // type annotation absent
} ValueType;

typedef struct Chan Chan;
typedef struct Value Value;
typedef struct Node Node;

// Host C function signature. Args arrive owned (moved in). The function
// returns an owned value. Consumed args must be dropped with chan_drop()
// or taken with chan_take() so the caller's cleanup does not double-free.
typedef Value (*CFn)(Chan* c, Value* args, int nargs, void* ud);

struct Value {
    ValueType type;
    int owned;             // 1 = owns its heap data; move transfers ownership
    union {
        int b;
        int64_t i;
        double f;
        char* s;
        struct ChanArray* arr;
        struct ChanMap* map;
        struct ChanObj* obj;
        struct ChanFunc* fn;
        struct { CFn fn; void* ud; } cfn;
    } as;
};

typedef struct ChanArray {
    Value* items;
    int len;
    int cap;
} ChanArray;

typedef struct MapSlot {
    Value key;   // owned by the map (deep-copied on insert)
    Value val;   // owned by the map
    int state;   // 0 empty, 1 used, 2 tombstone
} MapSlot;

typedef struct ChanMap {
    MapSlot* slots;
    int count;
    int cap;
} ChanMap;

typedef struct ChanObj {
    void* ptr;
    const char* type_name;
    void (*free_fn)(void*);
} ChanObj;

typedef struct ChanFunc {
    char* name;
    char* ret_type;        // may be NULL
    char** params;         // parameter names
    char** param_types;    // parameter types, parallel to params
    int nparams;
    struct Node* body;     // BlockStatement (owned by the AST, not the func)
} ChanFunc;

// --- constructors (all return owned values) ---
Value mk_nil(void);
Value mk_bool(int b);
Value mk_int(int64_t i);
Value mk_float(double f);
Value mk_str(char* s);             // takes ownership of s
Value mk_str_copy(const char* s);
Value mk_array(void);
Value mk_map(void);
Value mk_obj(void* ptr, const char* type_name, void (*free_fn)(void*));
Value mk_func(ChanFunc* fn);
Value mk_cfn(CFn fn, void* ud);

// --- ownership helpers ---
void free_value(Value* v);         // frees owned heap data and resets to nil
Value copy_value(Value* v);        // deep copy (owned)
Value chan_take(Value* slot);      // transfer ownership out of a slot (slot -> nil)
void chan_drop(Value* slot);       // free slot contents and reset to nil

// --- semantics ---
int value_eq(Value* a, Value* b);
uint64_t value_hash(Value* v);
int value_truthy(Value* v);
const char* value_type_name(ValueType t);
char* value_to_string(Value* v);   // malloc'd; caller frees

// --- arrays ---
void array_push(ChanArray* a, Value v);          // v moved in
Value array_get(ChanArray* a, int64_t i);        // borrow (owned=0) or nil
Value array_detach(ChanArray* a, int64_t i);     // owned or nil
int array_set(ChanArray* a, int64_t i, Value v); // v moved in; 0 if out of range

// --- maps ---
void map_set(ChanMap* m, Value key, Value val);  // key deep-copied, val moved in
Value map_get(ChanMap* m, Value key);            // borrow (owned=0) or nil
Value map_detach(ChanMap* m, Value key);         // owned or nil
int map_count(ChanMap* m);

#endif //CHAN_VALUE_H
