/*
 * Build the whole parameter set for one phoneme segment: four tracks, each
 * with its contour and its partner.
 *
 * This is the top of the subtree.  It finds the record for the consonant
 * pair the way lang/spa/engine/cluster.c does -- same twenty-three consonants, same
 * `a + b * 23` grid, same `N` before `C` becomes `n` and `Z` becomes `S` --
 * and then unpacks that record and hands the pieces to Track_Contour,
 * Track_Set and Ramp_Fill.
 *
 * The record is a bit stream, not a byte array.  Two runs of fields are
 * read out of it:
 *
 *   - four fields from the very start of the record, whose widths are not
 *     fixed.  Each is as wide as the header value it indexes needs --
 *     ceil(log2) of it -- so the table author could size the fields to the
 *     data.  With the shipped headers those widths are 8, 8, 12 and 8.
 *   - nine fields from eight bytes into the payload, three of six bits and
 *     six of five, which become the partner levels.
 *
 * The four wide fields index four more per-vowel tables, six bytes to an
 * entry, and those six bytes are the contour: four levels and two bytes of
 * packed nibbles that become the four breakpoints.  A breakpoint is
 * (nibble-derived constant * (seg_len - 1) + 50) / 100, so the contour
 * scales with the segment rather than being measured in samples.
 *
 * Unreachable paths.  A nucleus that is not one of A E I O U leaves the
 * table pointers unset and the original then dereferences them; '@' and
 * '|' take a shortcut that skips the header but still reads the base
 * pointer.  Zero is the answer for anything that does not crash.
 */
#include "es_engine.h"

/* per-vowel record tables, vowels 0..2 then 3..4 */
/* @0x100573a0 */
extern const tv_ref g_100573a0[3];
/* @0x100573e0 */
extern const int32_t g_100573e0[3];
/* @0x100573f0 */
extern const tv_ref g_100573f0[3];
/* @0x1005f524 */
extern const tv_ref g_1005f524[5];
/* @0x1005f51c */
extern const int32_t g_1005f51c[5];
/* @0x1005f54c */
extern const tv_ref g_1005f54c[5];
/* the four contour tables, one per track, six bytes an entry */
/* @0x10057400 */
extern const tv_ref g_10057400[3];
/* @0x10057410 */
extern const tv_ref g_10057410[3];
/* @0x10057420 */
extern const tv_ref g_10057420[3];
/* @0x10057430 */
extern const tv_ref g_10057430[3];
/* @0x1005f560 */
extern const tv_ref g_1005f560[2];
/* @0x1005f568 */
extern const tv_ref g_1005f568[2];
/* @0x1005f570 */
extern const tv_ref g_1005f570[2];
/* @0x1005f578 */
extern const tv_ref g_1005f578[2];
/* consonant letter -> 0..22, or -1 */
/* @0x100613c0 */
extern const int8_t g_100613c0[128];

/*
 * The width of a field that has to hold values up to `x`: ceil(log2(x)).
 *
 * The original works it out by halving rather than by counting bits, and
 * would not terminate on a 0 or a 1.  The shipped headers are 246, 177,
 * 3392 and 154, which give 8, 8, 12 and 8.
 */
static int32_t nbits(int32_t x)
{
    int32_t n = 1;
    int32_t p = 2;
    int32_t v = x / 2;

    while (v != 1) {
        p += p;
        n++;
        v /= 2;
    }
    if (x > p)
        n++;
    return n;
}

/*: one field out of the stream, most significant bit first.
 *
 * `prefetch` is the one place the two runs differ: when a field ends
 * exactly on a byte boundary the first run steps to the next byte and the
 * second does not.  It only shows when a field is followed by another, and
 * in the second run the only field that lands on a boundary is the last. */
static int32_t bits_take(const uint8_t *p, int32_t *pos, int32_t *left,
                         uint32_t *cur, int32_t width, int prefetch)
{
    uint32_t v;

    if (*left == 8)
        v = *cur;
    else
        v = *cur & ((1u << *left) - 1u);

    if (width > *left) {
        int32_t extra = width - *left;

        (*pos)++;
        *left = 8 - extra;
        v <<= extra;
        *cur = p[*pos];
        v += *cur >> *left;
    } else if (width == *left) {
        if (prefetch) {
            (*pos)++;
            *cur = p[*pos];
        }
        *left = 8;
    } else {
        *left -= width;
        v >>= *left;
    }
    return (int32_t)v;
}

/* @0x100117e0 */
uint8_t TV_THISCALL Segment_Apply(Engine *self, int32_t vowel)
{
    Node *cur = self->stage_ctx[3].cur;
    Node *ctl = self->stage_ctx[3].ctl;
    Node *scan = self->stage_ctx[3].scan;
    int32_t c_prev = cur->prev->value;
    int32_t c_cur = cur->value;
    int32_t c_nuc = ctl->value;
    int32_t c_scan = scan->value;
    int32_t c_next = scan->next->value;
    const uint8_t *base;
    const uint8_t *hdr;
    const uint8_t *ct[4];
    int32_t count, li, ri, rank, hdr0, hdr9, stride, off;
    int32_t dur, wide[4], f9[9], width[4];
    int32_t pos, bleft, k, track;
    uint32_t bcur;
    uint8_t vid;

    /* seg_len comes off the control node, less whatever the glide takes */
    self->seg_len = (int32_t)ctl->arg;
    if (self->trans_len != 0 && self->trans_len != 0x7f)
        self->seg_len -= self->trans_len;
    dur = self->seg_len - 1;

    /* the same substitutions lang/spa/engine/cluster.c makes, plus 'r' before 'R' */
    if (c_scan == 'r' && c_next == 'R')
        c_scan = 'R';
    if (c_scan == 'N' && c_next == 'C')
        c_scan = 'n';
    if (c_scan == 'Z')
        c_scan = 'S';
    if (c_cur == 'Z')
        c_cur = 'S';

    li = g_100613c0[(int8_t)c_cur];
    if (li == -1)
        return 0;
    ri = g_100613c0[(int8_t)c_scan];
    if (ri == -1)
        return 0;
    rank = BitTable_Rank(ri, li, vowel);
    if (rank == -1)
        return 0;

    switch ((int8_t)c_nuc) {
    case 'A':
    case 'E':
    case 'I':
        base = TV_REF(uint8_t, g_100573a0[vowel]);
        hdr = TV_REF(uint8_t, g_100573f0[vowel]);
        count = g_100573e0[vowel];
        ct[0] = TV_REF(uint8_t, g_10057400[vowel]);
        ct[1] = TV_REF(uint8_t, g_10057410[vowel]);
        ct[2] = TV_REF(uint8_t, g_10057420[vowel]);
        ct[3] = TV_REF(uint8_t, g_10057430[vowel]);
        break;
    case 'O':
    case 'U':
        base = TV_REF(uint8_t, g_1005f524[vowel]);
        hdr = TV_REF(uint8_t, g_1005f54c[vowel]);
        count = g_1005f51c[vowel];
        ct[0] = TV_REF(uint8_t, g_1005f560[vowel - 3]);
        ct[1] = TV_REF(uint8_t, g_1005f568[vowel - 3]);
        ct[2] = TV_REF(uint8_t, g_1005f570[vowel - 3]);
        ct[3] = TV_REF(uint8_t, g_1005f578[vowel - 3]);
        break;
    default:
        /* not a vowel: the original has no table to read and does not
         * survive it */
        return 0;
    }

    hdr0 = hdr[0];
    hdr9 = hdr[9];
    width[0] = nbits((hdr[1] << 8) + hdr[2]);
    width[1] = nbits((hdr[3] << 8) + hdr[4]);
    width[2] = nbits((hdr[5] << 8) + hdr[6]);
    width[3] = nbits(hdr[7]);

    /* ---- the record, and the variant of it that fits the context */
    stride = hdr0 + 15;
    off = rank * stride;
    vid = base[hdr0 + off + 14];
    if (vid != 0) {
        int32_t r = Variant_Find(self, vid, vowel, c_prev, c_next, hdr9);

        if (r == -1) {
            int32_t idx2 = BitTable_Count(vowel, 0x43) + vid - 1;
            uint8_t vid2 = base[hdr0 + idx2 * stride + 14];

            if (vid2 != 0)
                r = Variant_Find(self, vid2, vowel, c_prev, c_next, hdr9);
            else
                r = -1;
        }
        if (r != -1) {
            rank = r;
            off = r * stride;
        }
    }
    if (rank == -1 || rank >= count)
        return 0;

    /* ---- the four wide fields, from the start of the record */
    pos = 0;
    bleft = 8;
    bcur = base[off];
    for (k = 0; k < 4; k++)
        wide[k] = bits_take(base + off, &pos, &bleft, &bcur, width[k], k != 3);

    /* ---- and nine more, eight bytes into the payload */
    pos = 0;
    bleft = 8;
    bcur = base[off + hdr0 + 8];
    for (k = 0; k < 9; k++) {
        int32_t w = k <= 2 ? 6 : 5;

        f9[k] = bits_take(base + off + hdr0 + 8, &pos, &bleft, &bcur, w, 0);
    }
    for (k = 0; k < 3; k++)
        f9[k] = (f9[k] & 0x3f) << 3;
    for (k = 3; k < 9; k++)
        f9[k] = (f9[k] * 12) & 0x1ff;

    /* ---- one pass per track.  The three partner levels carry over from
     * track 11 to track 12, already clamped and halved. */
    {
        int32_t p1 = 0, p2 = 0, p3 = 0;

        for (track = 0; track < 4; track++) {
            const uint8_t *e = ct[track] + wide[track] * 6;
            int32_t b0 = e[0], b1 = e[1], b2 = e[2], b3 = e[3];
            int32_t b4 = e[4], b5 = e[5];
            int32_t t0, t1, t2, t3, l0, l5, p3rd;
            uint8_t *tb;

            if (track < 3) {
                p1 = f9[track];
                p2 = f9[3 + track];
                p3 = f9[6 + track];
            }

            /* the two packed bytes become the four breakpoints, each one a
             * fraction of the segment rather than a sample count */
            t0 = (uint32_t)((((b4 & 0xf0) >> 4) + 0xa) * dur + 0x32) / 100u;
            t1 = (uint32_t)((((b4 & 0xf) * 2) + 0x14) * dur + 0x32) / 100u;
            t2 = (uint32_t)((((b5 & 0xf0) >> 3) + 0x32) * dur + 0x32) / 100u;
            t3 = (uint32_t)((((b5 & 0xf) * 2) + 0x41) * dur + 0x32) / 100u;

            l0 = base[off + hdr0 + track];
            l5 = base[off + hdr0 + track + 4];

            if (track < 3) {
                if ((uint32_t)p1 > 0xc8u)
                    p1 = 0xc8;
                if ((uint32_t)p2 > 0xc8u)
                    p2 = 0xc8;
                if ((uint32_t)p3 > 0xc8u)
                    p3 = 0xc8;
                p1 = (int32_t)((uint32_t)p1 >> 1);
                p2 = (int32_t)((uint32_t)p2 >> 1);
                p3 = (int32_t)((uint32_t)p3 >> 1);
            }

            Track_Contour(self, b0, b1, b2, b3, t0, t1, t2, t3, p2, l5, p3,
                          l0, p1, track + 9, 0);
            Track_Set(self, l5, p3, l0, p1, track + 9, 0);

            /* ---- and the tail of the track's own buffer */
            p3rd = self->trk_param[track + 9][3];
            tb = self->trk_buf[track + 9];
            if (p3rd <= 4) {
                Ramp_Fill(tb, self->trk_wr[track + 9] + 1, p3rd - 1, l0, l5);
            } else if (p3rd <= 7) {
                int32_t avg = (b3 + b2 + b1 + b0) / 4;
                int32_t n1 = (p3rd - 1) / 2;
                int32_t start2, n2;

                Ramp_Fill(tb, self->trk_wr[track + 9] + 1, n1, l0, avg);
                p3rd = self->trk_param[track + 9][3];
                start2 = self->trk_wr[track + 9] + (p3rd - 1) / 2 + 1;
                n2 = p3rd + (1 - p3rd) / 2 - 1;
                Ramp_Fill(tb, start2, n2, avg, l5);
            }
        }
    }
    return 1;
}
