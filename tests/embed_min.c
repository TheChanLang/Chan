// Minimal Chan embedding: measures the core library footprint.
// Build with: cmake -S . -B build-size -DCHAN_SIZE_OPT=ON
#include <stdio.h>
#include "interp.h"

int main(void) {
    Chan* c = chan_new();
    Program* p = chan_parse(c, "fn add(a: int, b: int): int {\n    return a + b\n}\nlet x: int = add(2, 3)\n");
    if (!p) { fprintf(stderr, "%s\n", chan_error_msg(c)); return 1; }
    if (chan_run(c, p, NULL) != 0) { fprintf(stderr, "%s\n", chan_error_msg(c)); return 1; }
    Value v;
    if (chan_get(c, "x", &v) == 0) {
        printf("%lld\n", v.as.i); // 5
        free_value(&v);
    }
    free_program(p);
    chan_free(c);
    return 0;
}
