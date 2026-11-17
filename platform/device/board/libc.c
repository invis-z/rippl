#include "libc.h"

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t __attribute__((may_alias)) Word;

enum {
    WORD_BYTES = sizeof(Word),
    WORD_MASK = WORD_BYTES - 1u,
    BLOCK_WORDS = 4
};

static bool aligned(const void *address)
{
    return ((uintptr_t)address & WORD_MASK) == 0;
}

static void copy_words(uint8_t *to, const uint8_t *from, size_t words)
{
    Word *out = (Word *)(void *)to;
    const Word *in = (const Word *)(const void *)from;
    for (; words >= BLOCK_WORDS; words -= BLOCK_WORDS) {
        Word first = in[0];
        Word second = in[1];
        Word third = in[2];
        Word fourth = in[3];
        out[0] = first;
        out[1] = second;
        out[2] = third;
        out[3] = fourth;
        in += BLOCK_WORDS;
        out += BLOCK_WORDS;
    }
    for (; words > 0; words--) {
        *out++ = *in++;
    }
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    while (count > 0 && !aligned(to)) {
        *to++ = *from++;
        count--;
    }
    if (aligned(from)) {
        size_t words = count / WORD_BYTES;
        copy_words(to, from, words);
        to += words * WORD_BYTES;
        from += words * WORD_BYTES;
        count -= words * WORD_BYTES;
    }
    while (count-- > 0) {
        *to++ = *from++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    if (to <= from || to >= from + count) {
        return memcpy(destination, source, count);
    }
    to += count;
    from += count;
    while (count > 0 && !aligned(to)) {
        *--to = *--from;
        count--;
    }
    if (aligned(from)) {
        while (count >= WORD_BYTES) {
            to -= WORD_BYTES;
            from -= WORD_BYTES;
            *(Word *)(void *)to = *(const Word *)(const void *)from;
            count -= WORD_BYTES;
        }
    }
    while (count-- > 0) {
        *--to = *--from;
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *to = destination;
    uint8_t byte = (uint8_t)value;
    while (count > 0 && !aligned(to)) {
        *to++ = byte;
        count--;
    }
    Word pattern = (Word)byte * 0x01010101u;
    Word *out = (Word *)(void *)to;
    for (; count >= BLOCK_WORDS * WORD_BYTES; count -= BLOCK_WORDS * WORD_BYTES) {
        out[0] = pattern;
        out[1] = pattern;
        out[2] = pattern;
        out[3] = pattern;
        out += BLOCK_WORDS;
    }
    for (; count >= WORD_BYTES; count -= WORD_BYTES) {
        *out++ = pattern;
    }
    to = (uint8_t *)out;
    while (count-- > 0) {
        *to++ = byte;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = left;
    const uint8_t *b = right;
    for (size_t index = 0; index < count; index++) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
    }
    return 0;
}
