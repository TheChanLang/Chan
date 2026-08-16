#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "value.h"

// =================================================================
// Constructors
// =================================================================

Value mk_nil(void) { Value v; v.type = VAL_NIL; v.owned = 1; v.as.i = 0; return v; }
Value mk_bool(int b) { Value v; v.type = VAL_BOOL; v.owned = 1; v.as.b = b; return v; }
Value mk_int(int64_t i) { Value v; v.type = VAL_INT; v.owned = 1; v.as.i = i; return v; }
Value mk_float(double f) { Value v; v.type = VAL_FLOAT; v.owned = 1; v.as.f = f; return v; }
Value mk_str(char* s) { Value v; v.type = VAL_STR; v.owned = 1; v.as.s = s; return v; }
Value mk_str_copy(const char* s) {
    char* c = malloc(strlen(s) + 1);
    strcpy(c, s);
    return mk_str(c);
}
Value mk_array(void) {
    Value v; v.type = VAL_ARRAY; v.owned = 1;
    v.as.arr = calloc(1, sizeof(ChanArray));
    return v;
}
Value mk_map(void) {
    Value v; v.type = VAL_MAP; v.owned = 1;
    v.as.map = calloc(1, sizeof(ChanMap));
    return v;
}
Value mk_obj(void* ptr, const char* type_name, void (*free_fn)(void*)) {
    ChanObj* o = malloc(sizeof(ChanObj));
    o->ptr = ptr;
    o->type_name = type_name;
    o->free_fn = free_fn;
    Value v; v.type = VAL_OBJ; v.owned = 1; v.as.obj = o;
    return v;
}
Value mk_func(ChanFunc* fn) { Value v; v.type = VAL_FUNC; v.owned = 1; v.as.fn = fn; return v; }
Value mk_cfn(CFn fn, void* ud) { Value v; v.type = VAL_CFN; v.owned = 1; v.as.cfn.fn = fn; v.as.cfn.ud = ud; return v; }

// =================================================================
// Ownership
// =================================================================

void free_value(Value* v) {
    if (!v->owned) return; // borrowed: someone else owns the data
    switch (v->type) {
        case VAL_STR:
            free(v->as.s);
            break;
        case VAL_ARRAY: {
            ChanArray* a = v->as.arr;
            if (a) {
                for (int i = 0; i < a->len; i++) free_value(&a->items[i]);
                free(a->items);
                free(a);
            }
            break;
        }
        case VAL_MAP: {
            ChanMap* m = v->as.map;
            if (m) {
                for (int i = 0; i < m->cap; i++) {
                    if (m->slots[i].state == 1) {
                        free_value(&m->slots[i].key);
                        free_value(&m->slots[i].val);
                    }
                }
                free(m->slots);
                free(m);
            }
            break;
        }
        case VAL_OBJ: {
            ChanObj* o = v->as.obj;
            if (o) {
                if (o->free_fn) o->free_fn(o->ptr);
                free(o);
            }
            break;
        }
        case VAL_FUNC: {
            ChanFunc* f = v->as.fn;
            if (f) {
                free(f->name);
                free(f->ret_type);
                for (int i = 0; i < f->nparams; i++) {
                    free(f->params[i]);
                    free(f->param_types[i]);
                }
                free(f->params);
                free(f->param_types);
                free(f);
            }
            break;
        }
        default:
            break; // nil, bool, int, float, cfn have no heap data
    }
    v->type = VAL_NIL;
    v->owned = 1;
}

Value copy_value(Value* v) {
    switch (v->type) {
        case VAL_STR:
            return mk_str_copy(v->as.s);
        case VAL_ARRAY: {
            Value r = mk_array();
            for (int i = 0; i < v->as.arr->len; i++) {
                array_push(r.as.arr, copy_value(&v->as.arr->items[i]));
            }
            return r;
        }
        case VAL_MAP: {
            Value r = mk_map();
            for (int i = 0; i < v->as.map->cap; i++) {
                if (v->as.map->slots[i].state == 1) {
                    map_set(r.as.map, v->as.map->slots[i].key, copy_value(&v->as.map->slots[i].val));
                }
            }
            return r;
        }
        case VAL_OBJ: {
            // Shallow copy: both share the underlying C data; only the
            // original owns it (free_fn is cleared on the copy).
            ChanObj* o = v->as.obj;
            ChanObj* n = malloc(sizeof(ChanObj));
            n->ptr = o->ptr;
            n->type_name = o->type_name;
            n->free_fn = NULL;
            Value r; r.type = VAL_OBJ; r.owned = 1; r.as.obj = n;
            return r;
        }
        default: {
            Value r = *v;
            r.owned = 1;
            return r;
        }
    }
}

Value chan_take(Value* slot) {
    Value v = *slot;
    *slot = mk_nil();
    return v;
}

void chan_drop(Value* slot) {
    free_value(slot);
    *slot = mk_nil();
}

// =================================================================
// Semantics
// =================================================================

int value_eq(Value* a, Value* b) {
    if (a->type == b->type) {
        switch (a->type) {
            case VAL_NIL:  return 1;
            case VAL_BOOL: return a->as.b == b->as.b;
            case VAL_INT:  return a->as.i == b->as.i;
            case VAL_FLOAT:return a->as.f == b->as.f;
            case VAL_STR:  return strcmp(a->as.s, b->as.s) == 0;
            case VAL_ARRAY:return a->as.arr == b->as.arr; // identity
            case VAL_MAP:  return a->as.map == b->as.map; // identity
            case VAL_OBJ:  return a->as.obj == b->as.obj; // identity
            case VAL_FUNC: return a->as.fn == b->as.fn;
            case VAL_CFN:  return a->as.cfn.fn == b->as.cfn.fn;
            default:       return 0;
        }
    }
    // numeric equality across int/float
    if (a->type == VAL_INT && b->type == VAL_FLOAT) return (double)a->as.i == b->as.f;
    if (a->type == VAL_FLOAT && b->type == VAL_INT) return a->as.f == (double)b->as.i;
    return 0;
}

uint64_t value_hash(Value* v) {
    switch (v->type) {
        case VAL_STR: {
            uint64_t h = 1469598103934665603ULL; // FNV-1a
            for (const char* p = v->as.s; *p; p++) {
                h ^= (unsigned char)*p;
                h *= 1099511628211ULL;
            }
            return h;
        }
        case VAL_INT:  return (uint64_t)v->as.i;
        case VAL_FLOAT: { union { double d; uint64_t u; } x; x.d = v->as.f; return x.u; }
        case VAL_BOOL: return v->as.b ? 0x9E3779B97F4A7C15ULL : 0x0A2F3B4C5D6E7F80ULL;
        case VAL_NIL:  return 0x1234567890ABCDEFULL;
        default:       return 0x0DEADBEEF0CAFE00ULL;
    }
}

int value_truthy(Value* v) {
    switch (v->type) {
        case VAL_NIL:  return 0;
        case VAL_BOOL: return v->as.b;
        case VAL_INT:  return v->as.i != 0;
        case VAL_FLOAT:return v->as.f != 0.0;
        case VAL_STR:  return v->as.s[0] != '\0';
        case VAL_ARRAY:return v->as.arr->len > 0;
        case VAL_MAP:  return v->as.map->count > 0;
        default:       return 1; // obj, fn, cfn
    }
}

const char* value_type_name(ValueType t) {
    switch (t) {
        case VAL_NIL:   return "nil";
        case VAL_BOOL:  return "bool";
        case VAL_INT:   return "int";
        case VAL_FLOAT: return "float";
        case VAL_STR:   return "str";
        case VAL_ARRAY: return "array";
        case VAL_MAP:   return "map";
        case VAL_OBJ:   return "obj";
        case VAL_FUNC:  return "fn";
        case VAL_CFN:   return "cfn";
        default:        return "?";
    }
}

// =================================================================
// Stringify
// =================================================================

static void sb_grow(char** buf, int* len, int* cap, int need) {
    if (*len + need + 1 > *cap) {
        while (*len + need + 1 > *cap) *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
}

static void sb_append(char** buf, int* len, int* cap, const char* s) {
    int n = (int)strlen(s);
    sb_grow(buf, len, cap, n);
    memcpy(*buf + *len, s, n);
    *len += n;
}

static void sb_append_value(char** buf, int* len, int* cap, Value* v) {
    char tmp[64];
    switch (v->type) {
        case VAL_NIL:
            sb_append(buf, len, cap, "nil");
            break;
        case VAL_BOOL:
            sb_append(buf, len, cap, v->as.b ? "true" : "false");
            break;
        case VAL_INT:
            snprintf(tmp, sizeof tmp, "%lld", v->as.i);
            sb_append(buf, len, cap, tmp);
            break;
        case VAL_FLOAT:
            snprintf(tmp, sizeof tmp, "%.10g", v->as.f);
            sb_append(buf, len, cap, tmp);
            break;
        case VAL_STR:
            sb_append(buf, len, cap, v->as.s);
            break;
        case VAL_ARRAY: {
            sb_append(buf, len, cap, "[");
            for (int i = 0; i < v->as.arr->len; i++) {
                if (i) sb_append(buf, len, cap, ", ");
                sb_append_value(buf, len, cap, &v->as.arr->items[i]);
            }
            sb_append(buf, len, cap, "]");
            break;
        }
        case VAL_MAP: {
            sb_append(buf, len, cap, "{");
            int first = 1;
            for (int i = 0; i < v->as.map->cap; i++) {
                if (v->as.map->slots[i].state == 1) {
                    if (!first) sb_append(buf, len, cap, ", ");
                    sb_append_value(buf, len, cap, &v->as.map->slots[i].key);
                    sb_append(buf, len, cap, ": ");
                    sb_append_value(buf, len, cap, &v->as.map->slots[i].val);
                    first = 0;
                }
            }
            sb_append(buf, len, cap, "}");
            break;
        }
        case VAL_OBJ:
            snprintf(tmp, sizeof tmp, "<obj:%s>", v->as.obj->type_name);
            sb_append(buf, len, cap, tmp);
            break;
        case VAL_FUNC:
            snprintf(tmp, sizeof tmp, "<fn:%s>", v->as.fn->name);
            sb_append(buf, len, cap, tmp);
            break;
        case VAL_CFN:
            sb_append(buf, len, cap, "<cfn>");
            break;
        default:
            sb_append(buf, len, cap, "<?>");
            break;
    }
}

char* value_to_string(Value* v) {
    int cap = 32;
    int len = 0;
    char* buf = malloc(cap);
    sb_append_value(&buf, &len, &cap, v);
    buf[len] = '\0';
    return buf;
}

// =================================================================
// Arrays
// =================================================================

void array_push(ChanArray* a, Value v) {
    if (a->len >= a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4;
        a->items = realloc(a->items, a->cap * sizeof(Value));
    }
    a->items[a->len++] = v;
}

Value array_get(ChanArray* a, int64_t i) {
    if (i < 0 || i >= a->len) return mk_nil();
    Value v = a->items[i];
    v.owned = 0; // borrow
    return v;
}

Value array_detach(ChanArray* a, int64_t i) {
    if (i < 0 || i >= a->len) return mk_nil();
    Value v = a->items[i];
    a->items[i] = mk_nil();
    return v; // ownership moves out
}

int array_set(ChanArray* a, int64_t i, Value v) {
    if (i < 0 || i >= a->len) return 0;
    free_value(&a->items[i]);
    a->items[i] = v;
    return 1;
}

// =================================================================
// Maps
// =================================================================

static void map_grow(ChanMap* m) {
    int ncap = m->cap ? m->cap * 2 : 8;
    MapSlot* nslots = calloc(ncap, sizeof(MapSlot));
    for (int i = 0; i < m->cap; i++) {
        if (m->slots[i].state == 1) {
            uint64_t h = value_hash(&m->slots[i].key);
            int j = (int)(h % (uint64_t)ncap);
            while (nslots[j].state == 1) j = (j + 1) % ncap;
            nslots[j] = m->slots[i]; // struct copy moves ownership
        }
    }
    free(m->slots);
    m->slots = nslots;
    m->cap = ncap;
}

static int map_find(ChanMap* m, Value* key) {
    if (m->cap == 0) return -1;
    uint64_t h = value_hash(key);
    int i = (int)(h % (uint64_t)m->cap);
    for (int k = 0; k < m->cap; k++) {
        MapSlot* s = &m->slots[i];
        if (s->state == 0) return -1;
        if (s->state == 1 && value_eq(&s->key, key)) return i;
        i = (i + 1) % m->cap;
    }
    return -1;
}

void map_set(ChanMap* m, Value key, Value val) {
    if (m->count * 10 >= m->cap * 7) map_grow(m);
    int i = map_find(m, &key);
    if (i >= 0) {
        free_value(&m->slots[i].val);
        m->slots[i].val = val;
        return;
    }
    uint64_t h = value_hash(&key);
    int j = (int)(h % (uint64_t)m->cap);
    while (m->slots[j].state == 1) j = (j + 1) % m->cap;
    m->slots[j].state = 1;
    m->slots[j].key = copy_value(&key); // the map owns a copy of the key
    m->slots[j].val = val;
    m->count++;
}

Value map_get(ChanMap* m, Value key) {
    int i = map_find(m, &key);
    if (i < 0) return mk_nil();
    Value v = m->slots[i].val;
    v.owned = 0; // borrow
    return v;
}

Value map_detach(ChanMap* m, Value key) {
    int i = map_find(m, &key);
    if (i < 0) return mk_nil();
    Value r = m->slots[i].val;
    m->slots[i].val = mk_nil();
    free_value(&m->slots[i].key);
    m->slots[i].key = mk_nil();
    m->slots[i].state = 2; // tombstone
    m->count--;
    return r; // ownership moves out
}

int map_count(ChanMap* m) {
    return m->count;
}
