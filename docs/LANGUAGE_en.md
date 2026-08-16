# The Chan Language Reference

Chan is an ultra-compact interpreted language, written in pure C99, aimed at
systems where even a few KB is too large. Chan is designed as an **embedding
library** (V8-style): the core has no IO — everything such as printing,
reading files, or calling C comes from the host through the API.

Major design decisions:

- `int` and `float` are separate types — storing a `float` when only `int` is
  needed is wasteful on small systems.
- **Move semantics**: everything is a move by default; no GC, ARC, or
  borrow-checker is needed. `copy` is the keyword to duplicate a value.
- No `for`, no `switch` — everything is done with `if` and `while`.
- No multithreading support; embedding applications run one `Chan` instance
  per thread (Chan has no global state, so this is safe).

---

## 1. Lexical structure

### 1.1 Comments

`#` starts a comment that runs to the end of the line. Because `//` is not
used for comments, the `/` (division) operator never needs 2-character
lookahead.

```
let x: int = 5   # this is a comment
```

### 1.2 Identifiers

`[a-zA-Z_][a-zA-Z0-9_]*`. Keywords cannot be used as variable names.

### 1.3 Keywords (20 words)

```
if elif else while let bool array map str nil int float obj
fn cont break copy return true false
```

- `cont` = continue (resume a loop).
- `copy` = deep copy (the default is move).
- `true` / `false` are literals of the `bool` type.
- `bool array map str nil int float obj` are types (in declarations);
  `nil`, `true`, `false` can also be used as values.

### 1.4 Literals

| Kind | Example | Notes |
|------|---------|-------|
| int | `0`, `42`, `-7` | 64-bit integer |
| float | `3.14`, `0.5`, `10.0` | double (a digit is required after `.`) |
| str | `"chan"`, `"a\nb"` | escapes: `\n \t \r \\ \"` |
| bool | `true`, `false` | |
| nil | `nil` | |

### 1.5 Operators and delimiters

```
=  ==  !=  <  >  <=  >=  &&  ||  !
+  -  *  /  %
(  )  {  }  [  ]  ,  :  #
```

- `,` — separates items in lists (parameters, arguments, arrays, maps).
- `#` — line comment (see 1.1).
- `:` — type annotation: `let x: int = 1`, `fn f(a: int): int`.
- EOL (`\n`) terminates a statement. There is no `;`.

---

## 2. Data types

| Type | Description |
|------|-------------|
| `nil` | no value |
| `bool` | `true` / `false` |
| `int` | 64-bit integer |
| `float` | double-precision float |
| `str` | byte string |
| `array` | dynamic array; elements are moved in |
| `map` | hash map; keys are deep-copied on insert |
| `obj` | C-linked value wrapped by the host (`mk_obj`) |

Arithmetic rules: `int op int = int`; if a `float` participates, the result
is `float`.

```
10 / 4    # 2    (int)
10.0 / 4  # 2.5  (float)
7 % 3     # 1    (ints only)
```

Numeric comparisons work across `int`/`float` (`1 == 1.0` → `true`). `str`
comparison is lexicographic. `array`/`map` comparison is by identity
(pointer), not deep.

---

## 3. Expressions

Operator precedence (low → high):

| Level | Operators |
|-------|-----------|
| 1 | `=` (assignment) |
| 2 | `\|\|` |
| 3 | `&&` |
| 4 | `== !=` |
| 5 | `< > <= >=` |
| 6 | `+ -` |
| 7 | `* / %` |
| 8 | prefix `- ! copy` |
| 9 | call `f(...)`, index `a[i]` |

Expression forms:

```
42            # int literal
3.14          # float literal
"chan"        # str literal
true, nil     # bool / nil literal
x             # variable (borrowed when read, moved when transferred)
-f            # negation
!flag         # logical not
copy x        # deep copy
a + b * 2     # operators
f(1, 2)       # function call
arr[0]        # array element
m["k"]        # map value
[1, 2.5, x]   # array literal (elements move in)
{"a": 1, "b": 2}  # map literal
(a + b) * c   # grouping
```

Assignment is an expression: `x = expr`, `arr[i] = expr`, `m[k] = expr`.
The left side must be a variable or an index.

---

## 4. Statements

### 4.1 `let` — variable declaration (type required)

```
let x: int = 5
let s: str = "hello"
let arr: array = [1, 2, 3]
```

The right side is **moved** into the new variable.

### 4.2 `fn` — function definition

```
fn add(a: int, b: int): int {
    return a + b
}
```

- Name, parameters (typed, required) and return type (required, after `:`) —
  the parser never has to guess.
- A function that returns nothing declares `: nil`.
- Functions may recurse. `fn` is a statement — a function is an internal
  value (`<fn:name>`), called through its name.

### 4.3 `return`

```
fn fib(n: int): int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}
```

`return` stops the function and **moves** the value out of the frame. The
returned value is checked against the declared return type.

### 4.4 `if` / `elif` / `else`

```
if n < 2 {
    return n
} elif n < 10 {
    return fib(n - 1)
} else {
    return 0
}
```

`elif` is parsed as a nested `else { if ... }`. `{` must be on the same line
as the condition. Conditions read variables by **borrowing** (no move).

### 4.5 `while` / `cont` / `break`

```
let i: int = 0
while i < 10 {
    i = i + 1
    if i == 5 {
        cont     # skip the rest, re-check the condition
    }
    if i == 8 {
        break    # exit the loop
    }
}
```

`cont` / `break` outside a loop is a runtime error.

### 4.6 Bare expressions

```
print(x)          # call, discard the result
x + 1             # computed and discarded — testing only
```

---

## 5. Move semantics — the most important rule

There is no GC, ARC, or borrow-checker. Every value has **exactly one owner**
at any point in time. Everything is a **move** by default (zero cost); `copy`
is the only way to duplicate.

**Move points:**

| Situation | Example | Result |
|-----------|---------|--------|
| Declaration | `let b = a` | the binding `a` is **deleted from the symbol table** |
| Assignment | `x = y` | `y` is deleted; the value moves to `x` |
| Function argument | `f(x)` | `x` is deleted after the call |
| `return` | `return x` | `x` is deleted from the frame |
| Array/map element | `[1, x]` | `x` is deleted; the array owns the value |

**Reading is borrowing:** operands, `if`/`while` conditions, indexes — all
borrow only; no variable is deleted.

```
let a: int = 10
let b: int = a        # a is deleted
print(b)              # 10
print(a)              # ERROR: undefined variable 'a'
```

**`copy`:** creates a deep copy; both variables stay alive.

```
let a: int = 10
let b: int = copy a   # both a and b exist
print(a)              # 10
print(b)              # 10
```

Practical convention: to reuse a variable after passing it to a function,
pass `copy x`:

```
print(copy x)   # x is still alive
print(x)        # x is moved — from here on x does not exist
```

---

## 6. Arrays and maps

### 6.1 Arrays

```
let arr: array = [1, 2.5, "x"]
print(arr[1])         # 2.5
arr[0] = 99           # element assignment
print(len(copy arr))  # 3  (len is host-provided)
```

- Elements are moved into the array: `[1, 2.5, x]` deletes `x`.
- An out-of-range index is a runtime error.
- In a move context (argument, `let`), `arr[i]` **detaches** the element from
  the array; otherwise it only borrows.

### 6.2 Maps

```
let m: map = {"a": 1, "b": 2}
m["c"] = 3
print(m["b"])         # 2
```

- Keys are deep-copied into the hash table; values are moved in.
- `print(m["b"])` in a move context **detaches** key `"b"` from the map.

---

## 7. Runtime errors

Errors are reported as `line N: ...`:

```
undefined variable 'x'
'x': type mismatch, expected int, got str
index 5 out of range (len 2)
division by zero
break outside of a loop
cont outside of a loop
cannot call int
cannot index str
cannot apply '+' to int and str
```

`chan_run` returns `-1` on error; the message is in `chan_error_msg(c)`.

---

## 8. Embedding in C (library API)

Main API (`interp.h`, `value.h`):

| Function | Description |
|----------|-------------|
| `chan_new()` / `chan_free(c)` | create / destroy an instance |
| `chan_parse(c, src)` | parse a program → `Program*` (NULL on error) |
| `chan_run(c, p, out)` | run; `out` receives a copy of the last value (0 ok, -1 error) |
| `chan_register(c, name, fn, ud)` | register a C function for scripts |
| `chan_get(c, name, out)` | read a global (owned copy) |
| `chan_error_msg(c)` | error message |
| `mk_obj(ptr, "name", free_fn)` | wrap a C value as `obj` |
| `chan_take(&slot)` / `chan_drop(&slot)` | transfer ownership / clean up an argument inside a C function |

Example host registering `print`:

```c
static Value my_print(Chan* c, Value* args, int n, void* ud) {
    for (int i = 0; i < n; i++) {
        char* s = value_to_string(&args[i]);
        fputs(s, stdout);
        free(s);
        chan_drop(&args[i]);   // args are moved in — must be cleaned up
    }
    return mk_nil();
}
```

C function rules:

- Arguments arrive **moved in** (owned) — use `chan_drop` or `chan_take`.
- The function returns an owned `Value` (one of the `mk_*` constructors).
- Do not re-enter the script recursively from a C function (no reentrancy).

---

## 9. Current limitations

- `array`/`map` equality is by identity (pointer), not deep.
- No block comments — a deliberate limitation to keep the lexer tiny and save resources on constrained devices.
- No multiline strings — newlines delimit statements, so strings must stay on a single line; parsing multiline strings would cost extra resources.
- Map keys are always copied (the hash table owns its keys).
- The AST allocated by `chan_parse` is not deeply freed (`free_program` is
  still shallow).
- One `Chan*` per thread — but running multiple instances in parallel is safe. Chan targets embedded/IoT devices and deliberately has no multithreading; like V8 isolates, you can create independent `Chan*` instances on separate threads if you need parallelism.
