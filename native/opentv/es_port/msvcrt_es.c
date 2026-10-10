/*
 * The MSVC 4.2 C runtime functions the Spanish engine calls, for a build with
 * no CGRM_ES.DLL to reach into.
 *
 * src/port/msvcrt.c is the same thing for English and most of it would serve
 * here unchanged, but not all: the character classes and atol read the "C"
 * locale table the original CRT built into its own data, and that table is at a
 * different address in each DLL.  So each language brings its own, and the
 * answers for bytes over 0x7f are the ones its own CRT gave.
 */
#include <stdlib.h>
#include <string.h>

#include "es_engine.h"

/*
 * The locale's character-class table.  The engine never calls setlocale, so
 * this stays what the CRT set it to at load: the "C" locale, where nothing
 * over 0x7f is a letter.  The pointer is two bytes into the table, so index
 * -1 (EOF) is a slot of its own.
 */
/* @0x1006b638 */
extern const tv_ref g_ctype;

#define CT_UPPER 0x001
#define CT_LOWER 0x002
#define CT_DIGIT 0x004
#define CT_SPACE 0x008

/* _isctype, which the engine calls only for a multi-byte code page; with the
 * "C" locale it never gets here, and the mask is the caller's own. */
int32_t TV_CDECL tv_isctype(int32_t c, int32_t mask)
{
    return (int32_t)TV_REF(uint16_t, g_ctype)[c] & mask;
}

/* Leading space, an optional sign, then digits, accumulated in 32 bits and
 * allowed to wrap. */
int32_t TV_CDECL tv_atol(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int32_t n = 0;
    unsigned char c, sign;

    while (TV_REF(uint16_t, g_ctype)[*p] & CT_SPACE)
        p++;
    c = *p++;
    sign = c;
    if (c == '-' || c == '+')
        c = *p++;
    while (TV_REF(uint16_t, g_ctype)[c] & CT_DIGIT) {
        n = n * 10 + (int32_t)c - '0';
        c = *p++;
    }
    return (sign == '-') ? -n : n;
}

int32_t TV_CDECL tv_atoi(const char *s)
{
    return (int32_t)tv_atol(s);
}

/* Lower case in place, letters only, the same table as the rest. */
char *TV_CDECL tv_strlwr(char *s)
{
    unsigned char *p = (unsigned char *)s;

    for (; *p != 0; p++)
        if (TV_REF(uint16_t, g_ctype)[*p] & CT_UPPER)
            *p = (unsigned char)(*p + ('a' - 'A'));
    return s;
}

/* _itoa.  The engine only ever asks for radix 10, but the whole function is
 * cheap and a partial one would be a trap for a later caller. */
char *TV_CDECL tv_itoa(int32_t value, char *buf, int32_t radix)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[36];
    uint32_t v;
    int n = 0;
    char *out = buf;

    if (radix < 2 || radix > 36) {
        *out = 0;
        return buf;
    }
    if (radix == 10 && value < 0) {
        *out++ = '-';
        v = (uint32_t)-(int64_t)value;
    } else {
        v = (uint32_t)value;
    }
    do {
        tmp[n++] = digits[v % (uint32_t)radix];
        v /= (uint32_t)radix;
    } while (v != 0);
    while (n > 0)
        *out++ = tmp[--n];
    *out = 0;
    return buf;
}

void *TV_CDECL tv_malloc(size_t n)
{
    return malloc(n);
}

void TV_CDECL tv_free(void *p)
{
    free(p);
}

/* operator new: this CRT's returns NULL on failure rather than throwing, and
 * the engine checks for it. */
void *TV_CDECL tv_new(size_t n)
{
    return malloc(n);
}

char *TV_CDECL tv_strstr(const char *s, const char *sub)
{
    return strstr((char *)s, sub);
}

char *TV_CDECL tv_strchr(const char *s, int32_t c)
{
    return strchr((char *)s, c);
}

void *TV_CDECL tv_bsearch(const void *key, const void *base, size_t n,
                          size_t width,
                          int (TV_CDECL *cmp)(const void *, const void *))
{
    return bsearch(key, base, n, width, cmp);
}
