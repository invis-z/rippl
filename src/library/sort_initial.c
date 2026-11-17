#include <stdbool.h>
#include <stdint.h>

#include "core/resident.h"
#include "core/text.h"
#include "core/utf8.h"
#include "library/sort_key.h"

enum {
    LATIN_FIRST = 0xC0,
    LATIN_LAST = 0x17F,
    OTHER_SCRIPT = 0x80,
    UNMAPPED = '~'
};

RIPPL_RESIDENT static const char LATIN_INITIALS[] =
    "aaaaaa~ceeeeiiii~nooooo~~uuuuy~saaaaaa~ceeeeiiii~nooooo~~uuuuy~y"
    "aaaaaaccccccccdd~~eeeeeeeeeegggggggghh~~iiiiiiiii~iijjkk~lllllll"
    "l~~nnnnnn~~~oooooo~~rrrrrrsssssssstttt~~uuuuuuuuuuuuwwyyyzzzzzzs";

static uint32_t lowered(uint32_t code_point)
{
    return code_point >= 'A' && code_point <= 'Z' ? code_point + ('a' - 'A') : code_point;
}

static const char *skip_spaces(const char *text)
{
    const char *cursor = text;
    while (true) {
        const char *before = cursor;
        uint32_t code_point = utf8_next(&cursor);
        if (code_point == 0 || !text_is_space(code_point)) {
            return before;
        }
    }
}

static const char *after_article(const char *text)
{
    const char *cursor = text;
    if (lowered(utf8_next(&cursor)) != 't' || lowered(utf8_next(&cursor)) != 'h' ||
        lowered(utf8_next(&cursor)) != 'e') {
        return text;
    }
    const char *after_word = cursor;
    const char *rest = skip_spaces(cursor);
    if (rest == after_word || *rest == '\0') {
        return text;
    }
    return rest;
}

char sort_key_initial(const char *text)
{
    const char *cursor = after_article(skip_spaces(text));
    uint32_t code_point = lowered(utf8_next(&cursor));
    if (code_point < OTHER_SCRIPT) {
        return (char)code_point;
    }
    if (code_point >= LATIN_FIRST && code_point <= LATIN_LAST) {
        char initial = LATIN_INITIALS[code_point - LATIN_FIRST];
        return initial == UNMAPPED ? (char)OTHER_SCRIPT : initial;
    }
    return (char)OTHER_SCRIPT;
}
