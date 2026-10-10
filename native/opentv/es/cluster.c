/*
 * Pick the record for a consonant-vowel-consonant environment and set four
 * track parameters from it.
 *
 * The phonemes are on the stage-3 node list.  `scan` is at the vowel, and
 * the consonant on each side is what selects the record: the one before is
 * `ctl`'s value, the one after is the value of the node following `scan`.
 * `g_100613c0` maps the twenty-three consonant letters -- B C D F G K L M N
 * P R S T X Y b d g n r y ~ and space -- to 0..22 and everything else to
 * -1, which is the same twenty-three the `a + b * 23` grid in lang/spa/engine/bittab.c
 * is indexed by.  So `BitTable_Rank` of the pair is this record's place in
 * the packed array, and there is one array per vowel.
 *
 * Two substitutions happen first, and both are ordinary Spanish phonology:
 * `N` before `C` becomes `n`, the velar nasal, and `Z` becomes `S` wherever
 * it appears.  A leading `&` or `%` on the list is a marker rather than a
 * phoneme and is stepped over.
 *
 * Each record is `hdr[0] + 15` bytes: `hdr[0]` bytes of context, then the
 * payload, whose last byte is a variant id.  A non-zero variant id sends
 * the lookup through `Variant_Find`, and if that fails it is retried once
 * against the record the id itself names.  The first four payload bytes are
 * the answer, scaled on the way into the track parameters.
 *
 * What the four parameters are is not established, nor why rows 9 to 12 of
 * `trk_param` are the ones written.  The scalings -- x4, x8 + 500, x16 and
 * x16 -- are recorded as they are.
 */
#include "es_engine.h"

/* per-vowel record tables: base, record count and header, vowels 0..2 ... */
/* @0x100573a0 */
extern const tv_ref g_100573a0[3];
/* @0x100573e0 */
extern const int32_t g_100573e0[3];
/* @0x100573f0 */
extern const tv_ref g_100573f0[3];
/* ... and the same three for vowels 3 and 4.  The count table is the only
 * one of the six indexed by vowel - 3 rather than by vowel. */
/* @0x1005f524 */
extern const tv_ref g_1005f524[5];
/* @0x1005f528 */
extern const int32_t g_1005f528[2];
/* @0x1005f54c */
extern const tv_ref g_1005f54c[5];
/* consonant letter -> 0..22, or -1 */
/* @0x100613c0 */
extern const int8_t g_100613c0[128];

/*: 'A' 'E' 'I' 'O' 'U' -> 0..4 and every other letter up to 'U' -> 5.  The
 * original keeps this as 21 bytes at 0x10013298 and jumps through a table
 * of five arms, three of which are the same block. */
static int vowel_arm(int32_t c)
{
    switch (c) {
    case 'A': return 0;
    case 'E': return 1;
    case 'I': return 2;
    case 'O': return 3;
    case 'U': return 4;
    default:  return 5;
    }
}

/*: the variant id in the last byte of record `idx` */
static uint8_t record_vid(const uint8_t *base, int32_t hdr0, int32_t stride,
                          int32_t idx)
{
    return base[hdr0 + idx * stride + 14];
}

/* @0x10012f30 */
uint8_t TV_THISCALL Cluster_SetTracks(Engine *self, int32_t vowel)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *scan = st->scan;
    Node *n;
    int32_t c_cur = st->cur->value;
    int32_t c_ctl = st->ctl->value;
    int32_t c_nuc = scan->value;
    int32_t c_a, c_b;
    int32_t left, right, rank, count, hdr0, hdr9, stride, off;
    const uint8_t *base;
    const uint8_t *hdr;
    const uint8_t *p;

    n = scan->next;
    c_a = n->value;
    n = n->next;
    c_b = n->value;
    /* a '&' or '%' is a marker, not a phoneme: step past it, taking the
     * three values that follow it instead */
    if (c_nuc == '&' || c_nuc == '%') {
        c_nuc = c_b;
        n = n->next;
        c_a = n->value;
        c_b = n->next->value;
    } else if (c_a == '&' || c_a == '%') {
        c_a = c_b;
        n = n->next;
        c_b = n->value;
    }

    if (c_a == 'N' && c_b == 'C')
        c_a = 'n';
    if (c_ctl == 'Z')
        c_ctl = 'S';
    if (c_a == 'Z')
        c_a = 'S';

    left = g_100613c0[(int8_t)c_ctl];
    if (left == -1)
        return 0;
    right = g_100613c0[(int8_t)c_a];
    if (right == -1)
        return 0;

    rank = BitTable_Rank(right, left, vowel);

    switch (vowel_arm((int8_t)c_nuc)) {
    case 0:
    case 1:
    case 2:
        base = TV_REF(uint8_t, g_100573a0[vowel]);
        hdr = TV_REF(uint8_t, g_100573f0[vowel]);
        count = g_100573e0[vowel];
        break;
    case 3:
    case 4:
        base = TV_REF(uint8_t, g_1005f524[vowel]);
        hdr = TV_REF(uint8_t, g_1005f54c[vowel]);
        count = g_1005f528[vowel - 3];
        break;
    default:
        /* Not a vowel.  The original leaves the three above unset and only
         * survives it for '@' and '|', which skip the header read and fall
         * through to the return of 0 at the bottom; with any other value,
         * or with a rank that is not -1, it reads uninitialised locals.  So
         * 0 is the answer for every case that is reachable at all. */
        return 0;
    }

    hdr0 = hdr[0];
    hdr9 = hdr[9];
    stride = hdr0 + 15;

    /* The original decodes four more big-endian pairs out of the header
     * here -- hdr[1..2], hdr[3..4], hdr[5..6] and hdr[7] -- and runs each
     * through a loop that halves it until it reaches 1, doubling a counter
     * as it goes.  Every one of those counters is overwritten before it is
     * read, so the four loops compute nothing; they are left out.  (They
     * would not terminate on a 0 or a 1, and the shipped headers are 246,
     * 177, 3392 and 154, so they always do.) */

    if (rank == -1) {
        off = (hdr[3] << 8) + hdr[4];   /* the default record */
    } else {
        uint8_t vid;

        off = rank * stride;
        vid = record_vid(base, hdr0, stride, rank);
        if (vid != 0) {
            int32_t r = Variant_Find(self, vid, vowel, c_cur, c_b, hdr9);

            if (r != -1) {
                rank = r;
                off = r * stride;
            } else {
                /* retry once against the record the id itself names */
                int32_t idx2 = BitTable_Count(vowel, 0x43) + vid - 1;
                uint8_t vid2 = record_vid(base, hdr0, stride, idx2);

                if (vid2 != 0) {
                    r = Variant_Find(self, vid2, vowel, c_cur, c_b, hdr9);
                    if (r != -1) {
                        rank = r;
                        off = r * stride;
                    }
                }
            }
        }
    }

    if (rank == -1 || rank >= count)
        return 0;
    if (c_nuc == '@' || c_nuc == '|')
        return 0;

    p = base + hdr0 + off;
    self->trk_param[9][6] = p[0] * 4;
    self->trk_param[10][6] = p[1] * 8 + 0x1f4;
    self->trk_param[11][6] = p[2] * 16;
    self->trk_param[12][6] = p[3] * 16;
    return 1;
}
