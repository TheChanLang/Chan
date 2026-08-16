# Chan

> 🌐 Tiếng Việt: [README_vi.md](README_vi.md)

Chan is an ultra-compact interpreted language, written in pure C99, aimed at
systems where even a few KB is too large. Chan is designed as an **embedding
library** (V8-style), not a complete standalone language: the core has no IO —
everything such as `print` is registered by the C host through the API.

## Language reference

- [docs/LANGUAGE_en.md](docs/LANGUAGE_en.md) — English
- [docs/LANGUAGE_vi.md](docs/LANGUAGE_vi.md) — tiếng Việt

Full reference for lexical structure, types, expressions, statements, move
semantics, and the C embedding API.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

Size-optimized build (also builds `chan_core`, a minimal embedding used to
measure the core without the demo host):

```bash
cmake -S . -B build-size -DCHAN_SIZE_OPT=ON
cmake --build build-size --config Release
```

## Size (Windows x64, MSVC)

| Build | `chan.exe` (with demo host) | `chan_core.exe` (core only) |
|-------|----------------------------|-----------------------------|
| Release `/O2` (default) | 51.5 KiB | 45.0 KiB |
| Release `/O1` + `/MD` (`CHAN_SIZE_OPT=ON`) | 47.0 KiB | 42.5 KiB |

These are Windows PE binaries (including the PE header and the dynamic CRT).
On a microcontroller with `gcc -Os` and no PE/CRT it gets smaller still. Note:
the current tree-walking implementation with a full AST-building parser sits
at ~42 KiB — reaching the "a few KB" goal means moving to a bytecode compiler
+ VM (see Implementation notes).

### Size vs other embedded languages

Same toolchain: MSVC `cl` Release x64, `-O2`, dynamic CRT (`/MD`). Binaries
include the interpreter plus a minimal demo host, as in the tables above.

| Language | Binary size |
|----------|------------:|
| **Chan** | **51.5 KiB** |
| Wren | 123.0 KiB |
| Lua | 258.0 KiB |
| Squirrel | 264.0 KiB |
| MicroPython | 562.0 KiB |
| AngelScript | 1222.0 KiB |
| ChaiScript | 1441.0 KiB |

Chan is ~2.4× smaller than Wren, ~5× smaller than Lua/Squirrel, and
~24–28× smaller than AngelScript/ChaiScript.

### The trade-off: speed

The tiny footprint comes at a price: Chan is a **tree-walking interpreter**
while Lua, Squirrel, Wren, AngelScript and MicroPython all compile to
**bytecode** and run on a VM. In a `fib(30)` microbenchmark (median of 3
runs, same machine) Chan takes ~1.30 s vs ~0.11–0.24 s for the others —
about **5–11× slower**. This is a deliberate trade-off: the current
interpreter favours simplicity and RAM footprint over execution speed.
Closing the gap (and approaching the "a few KB" goal) means moving to a
bytecode compiler + VM. Full benchmark details and methodology:
[docs/BENCHMARK_vi.md](docs/BENCHMARK_vi.md).

## Usage

```bash
./build/Release/chan            # run the embedded demo
./build/Release/chan file.chan  # run a file
./build/Release/chan --ast      # print the AST of the demo
CHAN=./build/Release/chan bash tests/run_tests.sh
```

## Data types

`nil`, `bool` (`true`/`false`), `int`, `float`, `str`, `array`, `map`, `obj`.
`int` and `float` are **separate types** — storing a float when only an int is
needed is wasteful on small systems. Arithmetic: `int + int = int`; if a
`float` participates, the result is `float` (`10 / 4 = 2`, `10.0 / 4 = 2.5`).

## Syntax

```
fn fib(n: int): int {
    if n < 2 {
        return n
    } else {
        return fib(n - 1) + fib(n - 2)
    }
}

let x: int = fib(10)
let y: float = 3.14
let flag: bool = true
let s: str = "chan # tiny"
let arr: array = [1, 2.5, x, s]
let m: map = {"a": 1, "b": 2}
m["c"] = 3

while i < 10 {
    if i == 5 {
        cont
    }
    if i == 8 {
        break
    }
    i = i + 1
}
```

- Lists (parameters, arguments, array elements) are separated by `,`.
- `#` is a **line comment** — because `//` is not used for comments, the `/`
  (division) operator never needs 2-character lookahead, saving lexer
  resources.
- No `for`, no `switch` — everything is done with `if` and `while`.
- A function's return type is mandatory after `:`
  (`fn f(a: int, b: int): int`) so the parser never has to guess.

Keywords (20): `if elif else while let bool array map str nil int float obj
fn cont break copy return true false`.

## Move semantics (no GC, no ARC)

In Chan everything is **a move by default**, at zero cost. No GC/ARC/
borrow-checker is needed — every value has exactly one owner:

- `let b = a` **moves** `a` into `b`: the binding `a` is **deleted from the
  symbol table** (no `nil` left behind) to save resources.
- Passing arguments also moves: `print(x)` deletes `x`. To keep it, use
  `print(copy x)`.
- `copy` makes a deep copy: `let b = copy a` — both `a` and `b` stay alive.
- Reading a variable (an operand, an `if`/`while` condition, an index) is a
  **borrow**: free, and the variable is not deleted.
- `return x` moves the value out of the function frame.
- Arrays/maps own their elements: `[1, 2.5, x, s]` moves `x`, `s` into the
  array.

Example (`examples/move.chan`):

```
let a: int = 10
let b: int = a     # a is moved into b — a no longer exists
print(b)           # 10
print(a)           # error: undefined variable 'a'
```

## Embedding in C (library, V8-style)

The core does no printing and no file IO. The host registers C functions and
reads results:

```c
#include "interp.h"

static Value my_print(Chan* c, Value* args, int n, void* ud) {
    for (int i = 0; i < n; i++) {
        char* s = value_to_string(&args[i]);
        fputs(s, stdout);
        free(s);
        chan_drop(&args[i]);   // args are moved in — the host must clean up
    }
    return mk_nil();
}

int main(void) {
    Chan* c = chan_new();
    chan_register(c, "print", my_print, NULL);   // IO comes from the host

    Program* p = chan_parse(c, "let x: int = 40 + 2\nprint(x)\n");
    if (!p) { fprintf(stderr, "%s\n", chan_error_msg(c)); return 1; }
    if (chan_run(c, p, NULL) != 0) {
        fprintf(stderr, "%s\n", chan_error_msg(c));
        return 1;
    }
    Value x;
    if (chan_get(c, "x", &x) == 0) { /* read a global */ free_value(&x); }
    chan_free(c);
    return 0;
}
```

- C functions receive arguments **moved in** (owned). Use `chan_drop(&args[i])`
  to release, `chan_take(&args[i])` to transfer ownership into the return
  value (see `c_push` in `src/main.c`).
- `obj` is the C-linked type: `mk_obj(ptr, "name", free_fn)` wraps a C pointer
  for scripts; a `copy` of an `obj` is a shared reference that does not own
  the data.
- **No multithreading support**: each thread runs its own `Chan*`. Chan has no
  global state, so running multiple instances across threads is safe.

## Implementation notes

- Simple arena-style memory: the AST is allocated by `chan_parse` and released
  with `free_program`; values free themselves when a scope exits
  (`scope_free`) — no GC.
- `array`/`map` equality is deep (recursive, with a bounded depth so
  self-referential values don't loop forever).
- Array/map elements are moved in; map keys are copied into the hash table
  (open addressing, tombstones).
