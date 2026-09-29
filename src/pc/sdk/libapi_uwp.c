#include <stdint.h>
#include <stddef.h>

long Krom2RawAdd(unsigned long sjis)
{
    (void)sjis;
    static uint8_t s_blankGlyph[32] = {0};
    return (long)(intptr_t)s_blankGlyph;
}

long Krom2RawAdd2(unsigned short sjis)
{
    (void)sjis;
    static uint8_t s_blankGlyph[32] = {0};
    return (long)(intptr_t)s_blankGlyph;
}
