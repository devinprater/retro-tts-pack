/*
 * Leaf utilities: the 96-bit flag sets the stages carry around, the two node
 * walks that find word and phrase boundaries, the phoneme attribute test, and
 * one piece of fixed-point arithmetic.
 *
 * All seven are the English engine's, and five of them match it exactly by
 * shape.  Two do not match in convention: Phone_TestMask is stdcall here and
 * cdecl there, and it reads the attribute table directly rather than through
 * a Phone_Attr call.  The bit sets are indexed from the far end -- bit 0
 * lives in bits[2] -- which is why every one of them computes 2 - bit/32.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_phone_attr[];

/* @0x1001da50 */
int32_t TV_CDECL Bits_Test(int32_t bit, const uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    if (bits == NULL)
        return 0;
    return (bits[2 - bit / 32] & mask) != 0;
}

/* @0x1001da90 */
uint32_t TV_CDECL Bits_Set(int32_t bit, uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    bits[2 - bit / 32] |= mask;
    return mask;
}

/* @0x1001dac0 */
uint32_t TV_CDECL Bits_Clear(int32_t bit, uint32_t *bits)
{
    uint32_t mask = 1u << (bit & 31);
    bits[2 - bit / 32] &= ~mask;
    return ~mask;
}

/* The next set bit at or after bit + 1, or 0 when there is none -- so a
 * caller cannot tell "bit 0 is set" from "nothing is set", and the engine
 * relies on bit 0 never being used. */
/* @0x1001daf0 */
int32_t TV_CDECL Bits_Next(int32_t bit, const uint32_t *bits)
{
    int32_t k = bit + 1;
    int32_t w = 2 - k / 32;
    int32_t b = k & 31;
    uint32_t m = 1u << b;
    const uint32_t *p = &bits[w];

    while (w >= 0) {
        if (b < 32) {
            uint32_t v = *p;
            do {
                if (v & m)
                    return (2 - w) * 32 + b;
                m <<= 1;
                b++;
            } while (b < 32);
        }
        m = 1;
        b = 0;
        p--;
        w--;
    }
    return 0;
}

/* Back to the previous phrase boundary (type 3), stopping at a word
 * boundary (type 4) if one comes first. */
/* @0x1001a930 */
Node *TV_THISCALL Node_PrevBoundary(Engine *self, Node *n)
{
    uint32_t type;

    if (n == NULL)
        return NULL;
    do {
        n = Engine_StagePrev(self, n);
        if (n == NULL)
            return NULL;
        type = NODE_TYPE(n);
        if (type == 3)
            return n;
    } while (type != 4);
    return n;
}

/* @0x1001a9b0 */
Node *TV_THISCALL Node_NextWord(Engine *self, Node *n)
{
    if (n == NULL)
        return NULL;
    do {
        n = Engine_StageNext(self, n);
        if (n == NULL)
            return NULL;
    } while (NODE_TYPE(n) != 3);
    return n;
}

/* Test a phoneme's attribute bits.  A negative mask inverts the answer, and
 * a negative neg inverts it again, so both together cancel.  The high byte
 * of the mask selects which attribute row to read, shifted down one because
 * the row stride is 0x80 rather than 0x100. */
/* @0x1001a960 */
uint8_t TV_STDCALL Phone_TestMask(Node *n, int32_t mask, int32_t neg)
{
    uint8_t r;
    int32_t inv = 0;

    if (n == NULL)
        return 0;
    if (mask < 0) {
        mask = -mask;
        inv = 1;
    }
    r = (uint8_t)(g_phone_attr[(int32_t)(int8_t)n->value | ((mask & 0xff00) >> 1)] &
                  (uint8_t)mask);
    if (inv)
        r = (uint8_t)(r == 0);
    if (neg < 0)
        r = (uint8_t)(r == 0);
    return r;
}

/* @0x1000aed0 */
int32_t TV_STDCALL Synth_MulShr11(int32_t a, int32_t b)
{
    return (b * a) >> 11;
}

/* @0x1000aee0 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi)
{
    int32_t v = b * a;

    *hi = v >> 13;
    return v >> 12;
}

/* @0x1000af00 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b)
{
    return (b * a) / 0x7fff;
}

/* The four-operation gate over the two synthesiser flags, identical to the
 * English Synth_Gate at 0x10004750: 3 takes the gate if neither flag is set,
 * 4 opens it, 5 reports it, 6 closes it, and anything else fails. */
/* @0x1000ea70 */
uint8_t TV_THISCALL Synth_Gate(Engine *self, int32_t op)
{
    if (op == 3) {
        if (self->synth_19ad != 0)
            return 0;
        if (self->synth_19ae != 0)
            return 0;
        self->synth_19ae = 1;
        return 1;
    }
    if (op == 4) {
        self->synth_19ad = 1;
        self->synth_19ae = 0;
        return 1;
    }
    if (op == 5)
        return self->synth_19ad;
    if (op == 6) {
        self->synth_19ad = 0;
        return 1;
    }
    return 0;
}

/*
 * Two more of the 96-bit sets, asking the questions Bits_Test cannot: is
 * every bit of one set present in the other, and do the two sets share any
 * bit at all.  Both walk the three words from the low address up, so unlike
 * the single-bit operations above they do not care which end bit 0 lives at.
 */
/* @0x1001db60 */
int32_t TV_CDECL Bits_AllIn(const uint32_t *need, const uint32_t *have)
{
    int i;

    for (i = 0; i < 3; i++)
        if ((have[i] & need[i]) != need[i])
            return 0;
    return 1;
}

/* @0x1001db90 */
int32_t TV_CDECL Bits_AnyIn(const uint32_t *want, const uint32_t *have)
{
    int i;

    for (i = 0; i < 3; i++)
        if ((have[i] & want[i]) != 0)
            return 1;
    return 0;
}

/* Raw parameter value to the scale the track carries.  Case for case the
 * English Synth_ScaleParam at 0x1002c920, including the two indices that
 * multiply by two by two different routes -- but stdcall here where
 * English is cdecl, the same split Phone_TestMask has.  The English
 * function ends in a bare ret; this one ends in ret 8.
 *
 * The out-of-range test is unsigned, so a negative index falls through to
 * the default arm rather than indexing the jump table backwards. */
/* @0x1001bfd0 */
int32_t TV_STDCALL Synth_ScaleParam(int32_t index, uint8_t raw)
{
    int32_t v = raw;

    switch (index) {
    case 9:
        return v << 2;
    case 10:
        return v * 8 + 500;
    case 11:
    case 12:
        return v << 4;
    case 13:
    case 14:
    case 15:
        return v * 2;
    case 16:
        return v * 4 + 192;
    case 17:
        return v * 2;
    default:
        return v;
    }
}

/*
 * The Spanish five vowels, as letters rather than phonemes.  English has no
 * counterpart: its letter-to-sound code tests vowels by open comparison
 * because its vowel set is not five things in a row.
 *
 * Vowel_Index is emitted twice, at 0x100132b0 and 0x100121f0, byte for byte
 * the same 35 bytes down to the absolute address of the table -- one copy
 * per translation unit that used it, which is what a static function in a
 * shared header looks like after the linker has been through.  Both are
 * written out because the hook build patches by address, so the copy that
 * sub_10015720 calls has to be replaced as well as the one sub_10010ba0
 * does; a decompilation that covered only one would leave the other running
 * the original's code and would not say so.
 */
/* @0x10061440 */
extern const uint8_t g_vowels[5];

/* The vowel's position in "AEIOU", or -1.  Note that the loop leaves the
 * counter at 5 when nothing matched, so the two tests below are the same
 * test asked twice -- the original's shape, kept. */
/* @0x100132b0 */
int32_t TV_STDCALL Vowel_Index(uint8_t c)
{
    int32_t i = 0;

    while (g_vowels[i] != c && ++i < 5)
        ;
    return i == 5 ? -1 : i;
}

/* @0x100121f0 */
int32_t TV_STDCALL Vowel_Index2(uint8_t c)
{
    int32_t i = 0;

    while (g_vowels[i] != c && ++i < 5)
        ;
    return i == 5 ? -1 : i;
}

/* @0x100132e0 */
uint8_t TV_STDCALL Is_Vowel(uint8_t c)
{
    return (uint8_t)(c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U');
}

/* Which of the 128 ASCII codes Accent_Split lets through: 0x20 to 0x7e, plus
 * tab, newline, return, 0x12, 0x13, 0x1b and 0x1e. */
/* @0x10048880 */
extern const uint8_t g_ascii_ok[0x80];
/* The mark an accented letter carries, and the letter under it.  Indexed by
 * the low five bits, which is what makes 0xc0 and 0xe0 share an entry.  The
 * marks are ' for an acute, ~ for a tilde, ` for a diaeresis and e for the
 * ligature, all of them characters the rules read in their own right. */
/* @0x10048900 */
extern const uint8_t g_accent_mark[32];
/* @0x10048920 */
extern const uint8_t g_accent_base[32];
/* The same pair again, for the other caller: here a diaeresis becomes an E
 * after the vowel rather than a mark before it, and 0xdf gives s rather
 * than S. */
/* @0x10048b88 */
extern const uint8_t g_accent_mark2[32];
/* @0x10048ba8 */
extern const uint8_t g_accent_base2[32];

/*
 * One Latin-1 byte in, the letter under it out, and the mark it carried left
 * behind in the caller's byte for the next read.
 *
 * So an accented letter becomes two characters: the caller gets 'A' back and
 * finds '\'' waiting where the 0xc1 was.  An unaccented byte below 0x80 comes
 * back as itself if the table allows it and as zero if it does not, and the
 * byte is cleared either way.  0x80 to 0xbf are dropped, and 0xdf gives S
 * with s left over.
 */
/* @0x10014db0 */
uint8_t TV_CDECL Accent_Split(uint8_t *p)
{
    uint8_t c = *p;
    int32_t i;

    *p = 0;
    if (c < 0x80)
        return g_ascii_ok[c] != 0 ? c : 0;
    if (c < 0xc0)
        return 0;
    if (c == 0xdf) {
        *p = 's';
        return 'S';
    }
    /* the add is a byte add, so both halves of the range land on 0 to 0x1f */
    i = (uint8_t)(c >= 0xe0 ? c + 0x20 : c + 0x40);
    if (g_accent_mark[i] != 0) {
        *p = g_accent_base[i];
        return g_accent_mark[i];
    }
    return g_accent_base[i];
}

/*
 * Accent_Split's sibling, for the other table pair.
 *
 * Two differences beyond the tables: the case of the original letter is kept,
 * by adding 0x20 to both results when the byte came from the lower half of
 * the range, and the accepted range stops at 0xfc.  A return of 0x0d is
 * dropped, and anything outside the range comes back as zero with the
 * caller's byte untouched.
 */
/* @0x10020cf0 */
uint8_t TV_CDECL Accent_Fold(uint8_t *p)
{
    uint8_t c = *p;
    uint8_t mark, base;
    int lower, i;

    if (c == 0x0d) {
        *p = 0;
        return 0;
    }
    if (c < 0x80) {
        *p = 0;
        return c;
    }
    if (c < 0xc0 || c > 0xfc)
        return 0;
    if (c == 0xdf) {
        *p = 's';
        return 's';
    }
    lower = c >= 0xe0;
    i = lower ? c - 0xe0 : c - 0xc0;
    mark = g_accent_mark2[i];
    base = g_accent_base2[i];
    *p = mark;
    if (!lower)
        return base;
    if (base != 0)
        base = (uint8_t)(base + 0x20);
    if (mark != 0)
        *p = (uint8_t)(mark + 0x20);
    return base;
}
