/*
 * A sparse-record index built on a bitmap.
 *
 * Two tables of 5 rows by 67 bytes -- 536 bits a row -- say which records
 * exist, and the rank of a bit among the set bits before it is that
 * record's position in a packed array.  Callers test the result against -1
 * and, when it is -1, fall back on the whole-row count as a default index;
 * `sub_10012f30` does both within a dozen instructions of each other.
 *
 * The bit for a record is at `a + b * 23`, so each row is a 23-by-23 grid
 * with a few bits to spare.  What `a`, `b` and `kind` select is *not*
 * established: 23 is the stride and five is the number of rows, and neither
 * has been traced back to a phoneme set or a voice yet.  The names here
 * describe the mechanism, which is all the listing settles.
 *
 * The two rows of storage are one logical table split across two blocks.
 * Rows 0 to 2 are read from `g_100572d0` and rows 3 and 4 from
 * `g_1005f3cf`, but both are indexed by the same `kind * 67 + byte`, so the
 * second block's base is positioned for rows it does not hold the start of.
 *
 * Both blocks are byte-identical in Lernout & Hauspie's SPMtv160.dll, whose
 * counterparts to these two functions are `sub_1000efc8` and
 * `sub_1000f053`; see docs/SPANISH.md on reading the two builds together.
 */
#include "es_engine.h"

/* rows 0..2, indexed kind*67 + byte */
/* @0x100572d0 */
extern const uint8_t g_100572d0[201];
/* rows 3..4, indexed the same way, so the first 201 bytes are never read */
/* @0x1005f3cf */
extern const uint8_t g_1005f3cf[335];
/* @0x10061448 */
extern const uint8_t g_10061448[8];

/*: one byte of the bitmap.  `kind` is 0..4; the original reads an
 * uninitialised stack slot instead when it is not, which no caller does. */
static uint8_t row_byte(int32_t kind, int32_t i)
{
    int32_t off = kind * 67 + i;
    return kind <= 2 ? g_100572d0[off] : g_1005f3cf[off];
}

/*
 * Set bits in the first `nbytes` bytes of row `kind`.
 *
 * The original counts them by repeated subtraction rather than by shifting:
 * a running value starts at 0x100 and is halved -- with the signed
 * divide-by-two idiom, not a shift -- before each test, so it walks
 * 0x80, 0x40 ... 1, and each time the remaining bits are at least that
 * value it subtracts it and counts one.  It stops when nothing is left,
 * which for a byte is always by the time the running value reaches 1.
 */
/* @0x100122f0 */
int32_t TV_STDCALL BitTable_Count(int32_t kind, int32_t nbytes)
{
    int32_t total = 0;
    int32_t i;

    if (nbytes > 0x43)
        nbytes = 0x43;
    for (i = 0; i < nbytes; i++) {
        int32_t step = 0x100;
        uint32_t bits = row_byte(kind, i);

        while (bits != 0) {
            step = step / 2;
            if (bits >= (uint32_t)step) {
                bits -= (uint32_t)step;
                total++;
            }
        }
    }
    return total;
}

/*
 * The packed index of the record at (`a`, `b`) in table `kind`, or -1 when
 * that record does not exist.
 *
 * The bit number is `a + b * 23`; the byte and bit are the C quotient and
 * remainder by 8, negatives included -- the original uses the
 * add-the-sign-bits idiom, which rounds toward zero exactly as C does.
 */
/* @0x10012220 */
int32_t TV_STDCALL BitTable_Rank(int32_t a, int32_t b, int32_t kind)
{
    int32_t n = a + b * 23;
    int32_t byte = n / 8;
    int32_t bit = n - byte * 8;
    uint32_t bits = row_byte(kind, byte);
    int32_t total = 0;
    int32_t i;

    if ((bits & g_10061448[bit]) == 0)
        return -1;
    if (byte > 0)
        total = BitTable_Count(kind, byte);
    /* the bit itself counts, so the walk is inclusive and the result is
     * one less than the running total */
    for (i = 0; i <= bit; i++)
        if (bits & g_10061448[i])
            total++;
    return total - 1;
}

/* the variant table, one byte array per vowel: rows 0..2 here ... */
/* @0x100577e8 */
extern const tv_ref g_100577e8[3];
/* ... and rows 3..4 here, indexed by the same vowel number */
/* @0x1005f7e4 */
extern const tv_ref g_1005f7e4[5];

/*
 * The packed index of a context variant of record `rec`, or -1.
 *
 * Each record in the variant table is one flag byte followed by `nctx`
 * context characters.  Bit 0 of the flag picks which of the two characters
 * the caller offered is the one to match -- `ctx1` when it is set and
 * `ctx0` when it is not -- and the record matches if any of its context
 * bytes is that character.
 *
 * A match returns the record's place in the packed array *after* the
 * consonant-pair records, so the count of those is added to it; that is the
 * same `BitTable_Count(vowel, 0x43) + n - 1` the caller computes for itself
 * when this returns -1.
 *
 * `self` is passed and never used.  The original reloads it into ecx before
 * each call it makes, which is what a member function compiles to, but
 * nothing here reads it.  `vowel` above 4 leaves the table base
 * uninitialised in the original and is not reachable.
 */
/* @0x10012df0 */
int32_t TV_THISCALL Variant_Find(Engine *self, int32_t rec, int32_t vowel,
                                 int32_t ctx0, int32_t ctx1, int32_t nctx)
{
    const uint8_t *base = vowel <= 2 ? TV_REF(uint8_t, g_100577e8[vowel])
                                     : TV_REF(uint8_t, g_1005f7e4[vowel]);
    int32_t idx, off, want, found = 0, result = -1, i;

    (void)self;
    if (rec == 0)
        return -1;
    idx = (rec & 0xff) - 1;
    off = (nctx + 1) * idx;
    /* the flag byte chooses between the two characters offered */
    want = (base[off] & 1) ? (int32_t)(int8_t)ctx1 : (int32_t)(int8_t)ctx0;
    if (nctx + 1 <= 1)
        return -1;
    for (i = 1; i < nctx + 1; i++) {
        if (found)
            continue;
        if ((int32_t)base[off + i] != want)
            continue;
        found = 1;
        result = BitTable_Count(vowel, 0x43) + idx;
    }
    return result;
}
