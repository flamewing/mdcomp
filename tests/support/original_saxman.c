/* File framing for Clownacy's Okumura-derived, headerless Saxman codec. */
#include "LZSS.h"

#include <stdio.h>
#include <string.h>

struct input {
    FILE* file;
    long  remaining;
};

static int read_byte(void* opaque) {
    struct input* input = (struct input*)opaque;
    if (input->remaining == 0) {
        return EOF;
    }
    if (input->remaining > 0) {
        --input->remaining;
    }
    return fgetc(input->file);
}

int main(int argc, char** argv) {
    FILE *       source, *dest;
    struct input input;
    int          failed;
    if (argc != 4 || (strcmp(argv[1], "encode") && strcmp(argv[1], "decode"))) {
        return 2;
    }
    source = fopen(argv[2], "rb");
    if (!source) {
        return 1;
    }
    dest = fopen(argv[3], "wb");
    if (!dest) {
        fclose(source);
        return 1;
    }
    input.file      = source;
    input.remaining = -1;
    if (!strcmp(argv[1], "encode")) {
        long size;
        fputc(0, dest);
        fputc(0, dest);
        Encode(read_byte, &input, dest);
        size = ftell(dest) - 2;
        if (size < 0 || size > 65535 || fseek(dest, 0, SEEK_SET)) {
            fclose(source);
            fclose(dest);
            return 1;
        }
        fputc((int)(size & 255), dest);
        fputc((int)(size >> 8), dest);
    } else {
        int low = fgetc(source), high = fgetc(source);
        if (low == EOF || high == EOF) {
            fclose(source);
            fclose(dest);
            return 1;
        }
        input.remaining = low | (high << 8);
        Decode(read_byte, &input, dest);
    }
    failed = ferror(source) || ferror(dest);
    if (fclose(source)) {
        failed = 1;
    }
    if (fclose(dest)) {
        failed = 1;
    }
    return failed;
}
