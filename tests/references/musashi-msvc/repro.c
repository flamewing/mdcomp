#include <stdio.h>
#include <string.h>
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

typedef struct { unsigned short mask, match; } bits;
NOINLINE void parse(const char *pattern, bits *out) {
    bits *end = out + 2;
    for (; out < end; ++out) {
        int i;
        out->mask = out->match = 0;
        for (i = 0; i < 16; ++i) {
            out->mask |= (pattern[i] != '.') << (15-i);
            out->match |= (pattern[i] == '1') << (15-i);
        }
    }
}
int main(int argc, char **argv) {
    const char *pattern = argc > 1 ? argv[1] : "1111............";
    bits output[2];
    if (strlen(pattern) != 16) return 2;
    parse(pattern, output);
    printf("mask=%04x match=%04x\n", (unsigned)output[0].mask, (unsigned)output[0].match);
    return output[0].mask != 0xf000 || output[0].match != 0xf000;
}
