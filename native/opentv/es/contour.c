/*
 * Lay a whole parameter contour across one phoneme segment, for a track
 * and for its partner four tracks up.
 *
 * The caller hands over six levels and four breakpoints.  The levels are
 * byte-sized, in the units the track's payload bytes are stored in; the
 * breakpoints are sample offsets into the segment, whose total length is
 * `seg_len`.  What comes out is:
 *
 *     a single sample at l0, the level this segment starts from
 *     l0 -> m0   over t0 samples
 *     m0 -> m1   over t1 - t0
 *     m1 -> m2   over t2 - t1
 *     m2 -> m3   over t3 - t2
 *     m3 -> m4   over what is left
 *
 * where m0..m4 are the levels the caller gave, except on short segments,
 * where each one is moved half way toward the next -- see below.
 *
 * Before any of that, `trans_len` samples of glide carry the track from
 * where it was at the end of the previous segment down to l0.  That is
 * Extend's job rather than Ramp_Fill's, so it follows a decay curve.
 *
 * The scalings.  Tracks 9 to 12 store their parameters multiplied by 4, by
 * 8 with 500 added, by 16 and by 16, which is the same set Track_Set and
 * Cluster_SetTracks apply.  This function has to work in both units: the
 * levels arrive as bytes, are scaled up to compare against and average with
 * the track's current value, and are scaled back down to bytes before being
 * handed to Ramp_Fill, which works a byte at a time.
 *
 * The halving.  On a segment shorter than eight samples whose phoneme is
 * not flagged in g_10058618, there is no room for the full contour, so each
 * level is replaced by the midpoint between it and the next.  That is what
 * the five (Ln+1 - Ln) / 2 terms are.  Longer segments use the levels as
 * they came.
 *
 * Unreachable paths.  A `track` outside 9 to 12 skips both scaling switches
 * and reads uninitialised locals in the original; the one caller passes
 * 9 + (0..3).  Argument fifteen is pushed and never read.
 */
#include "es_engine.h"

/*: phoneme class flags, indexed by the value with 0x180 set.  Bit 0 keeps a
 * segment's levels un-halved; bit 6 is tested against the previous phoneme
 * for the 'Y' case on track 9. */
/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the index the original builds -- sign-extend the value to sixteen bits,
 * set 0x180, sign-extend again -- which is only in range for a value below
 * 0x80, and every phoneme letter is. */
static int32_t class_index(uint8_t v)
{
    int16_t w = (int16_t)(int8_t)v;

    w = (int16_t)(w | 0x180);
    return (int32_t)w;
}

/*: a level in the track's own units */
static int32_t scale_up(int32_t track, int32_t v)
{
    switch (track) {
    case 9:  return (v & 0xff) << 2;
    case 10: return (v & 0xff) * 8 + 0x1f4;
    default: return (v & 0xff) << 4;    /* 11 and 12 */
    }
}

/*: and back to a byte */
static int32_t scale_down(int32_t track, int32_t v)
{
    switch (track) {
    case 9:  return v >> 2;
    case 10: return (v - 0x1f4) >> 3;
    default: return v >> 4;             /* 11 and 12 */
    }
}

/* @0x10012390 */
void TV_THISCALL Track_Contour(Engine *self,
                               int32_t l1, int32_t l2, int32_t l3, int32_t l4,
                               int32_t t0, int32_t t1, int32_t t2, int32_t t3,
                               int32_t p2, int32_t l5, int32_t p3, int32_t l0,
                               int32_t p1, int32_t track, int32_t unused15)
{
    uint8_t ph = self->stage_ctx[3].cur->value;
    int32_t prev = 0;           /* the track's level at the end of the last
                                 * segment, back in byte units */
    int32_t partner_start = 0;  /* the partner track's, likewise */
    int32_t L0, L1, L2, L3, L4, L5;
    int32_t m0, m1, m2, m3, m4;
    int32_t trans, tcon, cur, seg, n, ptrack;
    uint8_t *buf;

    (void)unused15;

    /* ---- the six levels in the track's units, and where the track is */
    if (track >= 9 && track <= 12) {
        prev = scale_down(track, self->trk_490[track]);
        if (track != 12)
            partner_start = (int32_t)(uint8_t)(self->trk_490[track + 4] >> 1);
    }
    L0 = scale_up(track, l0);
    L1 = scale_up(track, l1);
    L2 = scale_up(track, l2);
    L3 = scale_up(track, l3);
    L4 = scale_up(track, l4);
    L5 = scale_up(track, l5);

    /* ---- the glide into this segment */
    trans = self->trans_len;
    cur = self->trk_wr[track];
    if (trans > 0 && trans != 0x7f) {
        int32_t from = l0;

        tcon = trans > 0x14 ? 0x14 : trans;
        /* Every track but 9 glides from wherever it was.  Track 9 holds
         * flat instead, unless the phoneme is one of the three voiceless
         * stops and the level really is moving. */
        if (track == 9 &&
            !((ph == 'P' || ph == 'T' || ph == 'K') &&
              (uint8_t)prev != (uint8_t)l0))
            prev = l0;
        Extend(self, self->trk_buf[track], cur, tcon, trans, prev, from);
        cur = self->trk_wr[track] + trans;
        if (self->trans_len == 1) {
            self->trk_wr[track] = cur;
            self->trk_param[track][3] -= self->trans_len;
        }
    }

    /* ---- the levels, halved toward each other on a short segment */
    seg = self->seg_len;
    m0 = l1;
    m1 = l2;
    m2 = l3;
    m3 = l4;
    m4 = l5;
    if (seg < 8 && (g_10058618[class_index(ph)] & 1) == 0 &&
        track >= 9 && track <= 12) {
        int32_t d0 = (L1 - L0) / 2;
        int32_t d1 = (L2 - L1) / 2;
        int32_t d2 = (L3 - L2) / 2;
        int32_t d3 = (L4 - L3) / 2;
        int32_t d4 = (L5 - L4) / 2;

        m0 = scale_down(track, d0 + L0);
        m1 = scale_down(track, d1 + L1);
        m2 = scale_down(track, d2 + L2);
        m3 = scale_down(track, d3 + L3);
        m4 = (int32_t)(uint8_t)scale_down(track, d4 + L4);
    }

    /* ---- the breakpoints, forced apart and out to the end */
    if (t3 < seg - 3)
        t3 = seg - 3;
    if (t3 < t2)
        t2 = t3 - 1;
    if (t2 < t1)
        t1 = t2 - 1;
    if (t1 < t0)
        t0 = t1 - 1;

    /* 'Y' on track 9, after a phoneme flagged 0x40, starts half way between
     * where the track is and the first level rather than at l0 */
    if (track == 9 && ph == 'Y' &&
        (g_10058618[class_index(self->stage_ctx[3].cur->prev->value)] & 0x40)) {
        int32_t half = (self->trk_490[track] - L1) / 2;

        l0 = (int32_t)(uint8_t)scale_down(track, L1 + half);
    }

    /* ---- the contour itself */
    buf = self->trk_buf[track];
    Extend(self, buf, cur, 1, 1, l0, l0);
    cur++;
    if (t0 > 0) {
        n = t0 > 0x14 ? 0x14 : t0;
        if (n == 1)
            Extend(self, buf, cur, 1, 1, m0, m0);
        else
            Ramp_Fill(buf, cur, n, l0, m0);
        cur += n;
    }
    if (t1 - t0 > 0) {
        Ramp_Fill(buf, cur, t1 - t0, m0, m1);
        cur += t1 - t0;
    }
    if (t2 - t1 > 0) {
        Ramp_Fill(buf, cur, t2 - t1, m1, m2);
        cur += t2 - t1;
    }
    if (t3 - t2 > 0) {
        Ramp_Fill(buf, cur, t3 - t2, m2, m3);
        cur += t3 - t2;
    }
    n = self->seg_len - t3 - 1;
    if (n > 0) {
        if (n > 0x14)
            n = 0x14;
        Ramp_Fill(buf, cur, n, m3, m4);
    }

    /* ---- the partner track, which track 12 does not have */
    if (track == 12)
        return;
    ptrack = track + 4;
    trans = self->trans_len;
    if (trans > 0 && trans != 0x7f) {
        int32_t t = trans > 0x14 ? 0x14 : trans;

        seg = self->seg_len;
        if (t < 4 && seg > 4 && seg < 8) {
            Extend(self, self->trk_buf[ptrack], self->trk_wr[ptrack], 3, 3,
                   partner_start, p1);
            cur = self->trk_wr[ptrack] + 3;
        } else if (seg > 7) {
            Extend(self, self->trk_buf[ptrack], self->trk_wr[ptrack],
                   t + 3, trans + 3, partner_start, p1);
            cur = self->trk_wr[ptrack] + self->trans_len + 3;
        } else {
            Extend(self, self->trk_buf[ptrack], self->trk_wr[ptrack],
                   t, trans, partner_start, p1);
            cur = self->trk_wr[ptrack] + self->trans_len;
        }
        self->trk_wr[ptrack] += self->trans_len;
        self->trk_param[ptrack][3] -= self->trans_len;
    } else {
        cur = self->trk_wr[ptrack];
    }

    /* the partner's contour is three runs: half the segment, a quarter, and
     * whatever that leaves */
    {
        int32_t half = (self->seg_len + 1) / 2;
        int32_t quarter = (self->seg_len + 3) / 4;
        int32_t rest = self->seg_len - quarter - half;
        int32_t t = self->trans_len > 0x14 ? 0x14 : self->trans_len;

        seg = self->seg_len;
        if (self->trans_len > 0 && self->trans_len != 0x7f &&
            t < 4 && seg > 4 && seg < 8)
            half = half - self->trans_len + 3;

        buf = self->trk_buf[ptrack];
        if (seg > 7) {
            if (self->trans_len <= 0 || self->trans_len == 0x7f) {
                Extend(self, buf, cur, 3, 3, partner_start, p1);
                cur += 3;
            }
            half -= 3;
        }
        n = half > 0x14 ? 0x14 : half;
        Extend(self, buf, cur, n, half, p1, p2);
        cur += half;
        if (quarter > 0) {
            n = quarter > 0x14 ? 0x14 : quarter;
            Extend(self, buf, cur, n, quarter, p2, p3);
            cur += quarter;
        }
        if (rest > 0) {
            n = rest > 0x14 ? 0x14 : rest;
            Extend(self, buf, cur, n, rest, p3, p3);
        }
        self->trk_wr[ptrack] += self->seg_len;
        self->trk_param[ptrack][3] -= self->seg_len;
    }
}
