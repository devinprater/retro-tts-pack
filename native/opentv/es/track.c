/*
 * Track parameter buffers.
 *
 * The four `trk_` arrays in the engine are 22 tracks of 256 bytes each, and
 * the stages that fill them do so in runs: "take this track from value A to
 * value B over the next N samples".  Ramp_Fill is that operation.
 */
#include "es_engine.h"

/*
 * Write a straight line from `from` to `to` into `n` bytes of `buf`,
 * beginning at `start` and wrapping at 256.
 *
 * The k'th byte, counting from one, is `from + (k * (to - from)) / n`, with
 * the division truncating toward zero the way idiv does and the sum taken
 * in eight bits.  So the first byte is already one step along and the last
 * is exactly `to`; the caller advances its own cursor by `n` and carries
 * on from there.
 *
 * The original accumulates the numerator rather than multiplying, which is
 * why it is written that way here.  `n` of zero returns without touching
 * anything; a negative `n` would divide by a negative and then run the loop
 * about four billion times, and every caller tests `n > 0` first.
 */
/* @0x10012ed0 */
void TV_STDCALL Ramp_Fill(uint8_t *buf, int32_t start, int32_t n,
                          int32_t from, int32_t to)
{
    int32_t delta = (int32_t)(uint8_t)to - (int32_t)(uint8_t)from;
    int32_t acc = delta;
    int32_t i;

    if (n == 0)
        return;
    for (i = n; i != 0; i--) {
        int32_t q = acc / n;

        acc += delta;
        buf[start & 0xff] = (uint8_t)((uint8_t)from + (uint8_t)q);
        start++;
    }
}

/*
 * Set one track's parameters, and its partner four tracks up.
 *
 * `track` is 9 to 12, and the scaling applied to the two byte values is the
 * one that track uses -- x4, x8 + 500, x16, x16 -- the same four that
 * Cluster_SetTracks applies when it writes rows 9 to 12 directly.  The
 * partner track gets `b` doubled, and track 12 has no partner.
 *
 * Column 0 is a flag taken from the phoneme stage 3 is sitting on: zero for
 * M, N, n, ~, R and r, one otherwise.  Those six are the nasals and the two
 * rhotics -- the phonemes that stay voiced with the tract open -- and the
 * one exception is `M` on tracks 9 and 12, which is forced back to one.
 *
 * Arguments four and six are pushed by the caller and never read.  A
 * `track` outside 9..12 makes the original write an uninitialised local
 * into all three parameters; its one caller passes 9 + (0..3), so that is
 * unreachable, and zero stands in for it here.
 */
/* @0x10012cb0 */
void TV_THISCALL Track_Set(Engine *self, int32_t a, int32_t b, int32_t c,
                           int32_t unused4, int32_t track, int32_t unused6)
{
    uint8_t ph = self->stage_ctx[3].cur->value;
    int32_t open, va, vb, vc;

    (void)unused4;
    (void)unused6;
    open = !(ph == 'M' || ph == 'N' || ph == 'n' || ph == '~' ||
             ph == 'R' || ph == 'r');
    if (ph == 'M' && (track == 12 || track == 9))
        open = 1;

    vb = (int32_t)(uint8_t)b * 2;
    switch (track) {
    case 9:
        vc = (int32_t)(uint8_t)c << 2;
        va = (int32_t)(uint8_t)a << 2;
        break;
    case 10:
        vc = (int32_t)(uint8_t)c * 8 + 0x1f4;
        va = (int32_t)(uint8_t)a * 8 + 0x1f4;
        break;
    case 11:
        vc = (int32_t)(uint8_t)c << 4;
        va = (int32_t)(uint8_t)a << 4;
        break;
    case 12:
        vc = (int32_t)(uint8_t)c << 4;
        va = (int32_t)(uint8_t)a << 4;
        vb = 0;                 /* never read: track 12 has no partner */
        break;
    default:
        va = vb = vc = 0;       /* the original's uninitialised local */
        break;
    }

    self->trk_param[track][0] = open;
    self->trk_param[track][5] = vc;
    self->trk_param[track][4] = vc;
    self->trk_param[track][6] = va;
    self->trk_490[track] = va;
    if (track != 12) {
        self->trk_param[track + 4][5] = vb;
        self->trk_param[track + 4][4] = vb;
        self->trk_490[track + 4] = vb;
        self->trk_param[track + 4][6] = vb;
        self->trk_param[track + 4][0] = 0;
    }
}

/*: the transition contours, indexed by TCon.  Contour n is n bytes long
 * and decays 255, ..., 1 before a terminating zero, so TCon is both the
 * shape and the number of samples the glide takes. */
/* @0x10058368 */
extern const tv_ref g_10058368[21];

/*
 * Glide a track from `to` back to `from` over `tcon` samples, then hold at
 * `from` for the rest of the `n` bytes.
 *
 * The original's error string names its own source: "Arith.c Extend()".
 * The contour byte is taken to Q15 by shifting it seven places -- 255
 * becomes 32640 against a divisor of 32767, so the first sample is `to`
 * bar a rounding step -- and the product is added to `from` in eight bits.
 *
 * Whichever runs out first ends the glide: a zero in the contour, or the
 * `n` bytes.  Either way exactly `n` bytes are written, wrapping at 256.
 * A TCon above 20 is traced and clamped, and since the tracer is stubbed
 * out in the shipping build that is silent.
 */
/* @0x1000af20 */
void TV_THISCALL Extend(Engine *self, uint8_t *buf, int32_t start,
                        int32_t tcon, int32_t n, int32_t to, int32_t from)
{
    int32_t base = (int32_t)(uint8_t)from;
    int32_t delta = (int32_t)(uint8_t)to - base;
    const uint8_t *shape;
    int32_t left = n;
    int32_t v, k;

    if (tcon > 0x14) {
        Engine_Trace(self, "ERROR: Arith.c Extend():   TCon=%d \n", tcon);
        tcon = 0x14;
    }
    shape = TV_REF(uint8_t, g_10058368[tcon]);

    for (v = (int32_t)shape[0] << 7; v != 0; v = (int32_t)shape[0] << 7) {
        k = left;
        left--;
        if (k == 0)
            break;
        buf[start & 0xff] = (uint8_t)(Synth_MulQ15(delta, v) + base);
        start++;
        shape++;
    }
    for (k = 0; k < left; k++) {
        buf[start & 0xff] = (uint8_t)base;
        start++;
    }
}

/*
 * Emit one track's pending shape into its buffer.
 *
 * trk_param[track][0] is a mode, and it decides which of three writers run:
 * bit 0 a backward blend, bit 2 an Extend, and bit 1 either a forward blend
 * (on its own) or the contour arguments for that Extend (with bit 2).  Mode
 * 0, and anything above 7, writes nothing.
 *
 * Bit 0 is dropped first if the track has nothing buffered -- trk_wr is not
 * ahead of trk_rd -- because the backward blend works over exactly that gap.
 *
 * The three levels are scaled out of the track's own units on the way in,
 * which extends the family Track_Set and Track_Contour use from four tracks
 * to nine: 9 is x4, 10 is x8 + 500, 11 and 12 are x16, 13 to 15 are x2, 16
 * is x4 + 192 and 17 is x2.  A track outside 9..17 is not scaled at all.
 * Tracks 13 to 15 additionally report an error when their level comes out
 * at or below zero.
 */
/* @0x10017aa0 */
void TV_THISCALL Track_Emit(Engine *self, int32_t track)
{
    int32_t mode = self->trk_param[track][0];
    int32_t count = self->trk_param[track][1];
    int32_t p2 = self->trk_param[track][2];
    int32_t lo = self->trk_param[track][4];
    int32_t hi = self->trk_param[track][5];
    int32_t top;
    uint8_t *buf = self->trk_buf[track];
    int32_t wr = self->trk_wr[track];
    int32_t gap = wr - self->trk_rd[track];

    if (lo < 0)
        lo = 0;
    if (hi < 0)
        hi = 0;
    top = self->trk_param[track][6];

    switch (track - 9) {
    case 0:
        lo >>= 2; hi >>= 2; top >>= 2;
        break;
    case 1:
        lo -= 0x1f4; hi -= 0x1f4; top -= 0x1f4;
        lo >>= 3; hi >>= 3; top >>= 3;
        break;
    case 2:
    case 3:
        lo >>= 4; hi >>= 4; top >>= 4;
        break;
    case 4:
    case 5:
    case 6:
        lo >>= 1; hi >>= 1; top >>= 1;
        if (top <= 0)
            Engine_Error(self, 0x30);
        break;
    case 7:
        lo -= 0xc0; hi -= 0xc0; top -= 0xc0;
        lo >>= 2; hi >>= 2; top >>= 2;
        break;
    case 8:
        lo >>= 1; hi >>= 1; top >>= 1;
        break;
    default:
        break;
    }

    /* the backward blend has nothing to work over if the track is empty */
    if ((mode & 1) && gap <= 0)
        mode &= ~1;

    switch (mode) {
    case 1:
        if (count > 0x14)
            Engine_Error(self, 0x3a6);
        Track_BlendBack(self, buf, wr, count, gap, (uint8_t)lo);
        break;
    case 2:
        Track_BlendFwd(self, buf, wr, p2, self->trk_param[track][3],
                       (uint8_t)hi);
        break;
    case 3:
        Track_BlendBack(self, buf, wr, count, gap, (uint8_t)lo);
        Track_BlendFwd(self, buf, wr, p2, self->trk_param[track][3],
                       (uint8_t)hi);
        break;
    case 4:
        Extend(self, buf, wr, 0, self->trk_param[track][3], 0, top);
        break;
    case 5:
        Track_BlendBack(self, buf, wr, count, gap, (uint8_t)lo);
        Extend(self, buf, wr, 0, self->trk_param[track][3], 0, top);
        break;
    case 6:
        Extend(self, buf, wr, p2, self->trk_param[track][3], hi, top);
        break;
    case 7:
        Track_BlendBack(self, buf, wr, count, gap, (uint8_t)lo);
        Extend(self, buf, wr, p2, self->trk_param[track][3], hi, top);
        break;
    default:
        break;
    }
}

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the two flag-table index modes this file needs; see lang/spa/engine/adjust.c on
 * why g_10058618 is indexed four different ways across the engine. */
static int32_t cls0_trk(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t cls100_trk(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

/*: a per-step gain, walked one int32 at a time for as many steps as the
 * caller asks for. */
/* @0x10048940 */
extern const int32_t g_10048940[];

/*
 * Pull tracks 10 and 11 toward each other over a run of samples.
 *
 * At each position the distance between track 10 and twice track 11 becomes
 * a weight -- |8d + 500| / 2 subtracted from 100 -- which scales the
 * caller's gain and then a per-step gain out of g_10048940.  An eighth of
 * the result comes off track 10 and a sixteenth goes on to track 11, both
 * in eight bits, so the two move together.
 *
 * The count is decremented once before the loop, so a count of 1 does
 * nothing.  Positions wrap at 256 like every other track write.
 */
/* @0x100179d0 */
void TV_THISCALL Track_Couple(Engine *self, int32_t pos, int32_t count,
                              int32_t gain)
{
    const int32_t *g = g_10048940;
    int32_t at = pos & 0xff;

    count--;
    while (count > 0) {
        uint8_t *b10 = self->trk_buf[10];
        uint8_t *b11 = self->trk_buf[11];
        int32_t d = (int32_t)b10[at] - (int32_t)b11[at] * 2;
        int32_t v;

        d = d * 8 + 0x1f4;
        if (d < 0)
            d = -d;
        d /= 2;
        v = Synth_MulQ15(100 - d, gain);
        v = Synth_MulQ15(v, *g++) >> 3;
        b10 = self->trk_buf[10];
        if ((int32_t)b10[at] > v + 0xff)
            b10[at] = 0xff;
        else
            b10[at] = (uint8_t)(b10[at] - (uint8_t)v);
        v >>= 1;
        self->trk_buf[11][at] = (uint8_t)(self->trk_buf[11][at] + (uint8_t)v);
        at = (at + 1) & 0xff;
        count--;
    }
}

/*
 * Blend a track backward toward a level it has already been told, rather
 * than toward one recomputed each step.
 *
 * Track_BlendBack takes a target and works out `target - *p` fresh at every
 * position, so it converges.  This one is handed a single `delta` and scales
 * that by the contour, so the whole run moves by the same amount shaped the
 * same way.  The caller computes the delta once, and this clamps it at the
 * start so the first sample cannot go below zero.
 *
 * `mode` only gates the floor: a result of zero or less is pinned to zero
 * unless the mode is 3 or more, in which case the eight-bit truncation is
 * left to stand.  It runs backward from `pos`, wrapping at 256, and stops
 * on the contour's terminator or when the count runs out.
 */
/* @0x10017dc0 */
void TV_THISCALL Track_BlendDelta(Engine *self, uint8_t *buf, int32_t mode,
                                  int32_t pos, int32_t tcon, int32_t n,
                                  int32_t delta)
{
    const uint8_t *s = TV_REF(uint8_t, g_10058368[tcon]);
    int32_t p = pos - 1;
    int32_t first = buf[p & 0xff];
    int32_t w;

    (void)self;
    if (delta + first < 0)
        delta = -first;
    s++;
    for (w = (int32_t)*s << 7; w != 0; w = (int32_t)*s << 7) {
        uint8_t *q;
        int32_t r, k = n;

        n--;
        if (k == 0)
            return;
        q = buf + (p & 0xff);
        r = Synth_MulQ15(delta, w) + (int32_t)*q;
        if (r <= 0 && mode < 3)
            r = 0;
        p--;
        s++;
        *q = (uint8_t)r;
    }
}

/*
 * Wind tracks 0 and 2 back and redraw the tail.
 *
 * s3_458 samples come off track 2's write cursor, the vacated run is blended
 * back toward s3_47c, and the same number is added to the track's duration
 * so the time is accounted for rather than lost.  Two more tracks get the
 * same treatment conditionally -- 13 when the current phoneme is flagged,
 * 0 when the control node is a space -- and track 0's cursor is wound back
 * at the end.
 */
/* @0x10017e60 */
void TV_THISCALL Track_Shorten(Engine *self)
{
    int32_t n = self->s3_458;
    int32_t wr = self->trk_wr[2];
    uint8_t *buf;
    int32_t delta;

    if (wr < n)
        return;
    wr -= n;
    buf = self->trk_buf[2];
    self->trk_wr[2] = wr;
    delta = self->s3_47c - (int32_t)buf[wr & 0xff];
    Track_BlendDelta(self, buf, 2, wr, 3, wr - self->trk_rd[2], delta);

    self->trk_param[2][2] = 3;
    self->trk_490[2] = self->s3_47c;
    self->trk_param[2][3] += self->s3_458;

    if (g_10058618[cls0_trk(self->stage_ctx[3].cur->value)] & 4) {
        int32_t w = self->trk_wr[2];

        Track_BlendDelta(self, self->trk_buf[13], 0xd, w, 6,
                         w - self->trk_rd[13], 0x32);
    }
    if (self->stage_ctx[3].ctl->value == ' ') {
        int32_t w = self->trk_wr[2];

        Track_BlendDelta(self, self->trk_buf[0], 0, w, 0xa,
                         w - self->trk_rd[0], -4);
    }
    if (g_10058618[cls100_trk(self->stage_ctx[3].ctl->value)] & 1)
        self->trk_param[2][0] = 5;
    if (self->trk_wr[0] >= self->s3_458) {
        self->trk_param[0][3] += self->s3_458;
        self->trk_wr[0] -= self->s3_458;
    }
}

/* the same two tables Track_SetModes reads, and a third entry point into
 * the second one -- the original addresses it eight bytes along. */
/* @0x10057e20 */
extern const tv_ref g_10057e20t;
/* @0x100581e8 */
extern const uint8_t g_100581e8[256];

/*
 * Emit a run of fixed values into tracks 0, 2 and 13, and account for it.
 *
 * trans_len is the length, worked out in four steps: 4 to begin with, 2 when
 * the current node is not flagged 0x20, then 5 or 7 or 1 depending on the
 * class flags, and finally a duration lookup added on.  It is then clamped
 * against what tracks 2 and 0 have left, and if nothing is left the whole
 * thing is skipped.
 *
 * The fills are 0 into track 0, s3_47c into track 2 and 0x4b into track 13.
 * Each one costs the track that many samples of duration and advances its
 * cursor by the same amount, so the time is moved rather than created.
 *
 * The original looks the duration up seven separate times, always on the
 * current phoneme and always through the same two tables; it is one value.
 * It also sets trans_len to 5 twice on nested conditions, the inner one
 * changing nothing.
 */
/* @0x10017f90 */
void TV_THISCALL Track_EmitPause(Engine *self)
{
    Node *cur = self->stage_ctx[3].cur;
    Node *ctl = self->stage_ctx[3].ctl;
    int32_t c_cur = cur->value;
    int32_t c_ctl = ctl->value;
    int32_t v = self->s3_478;
    int32_t n;

    self->s3_47c = v;
    self->trans_len = 4;
    if (!(cur->flags & 0x20)) {
        self->trans_len = 2;
        self->s3_47c = v - 3;
    }
    if (!(g_10058618[cls0_trk((uint8_t)c_ctl)] & 1))
        self->trans_len = 5;
    if (c_ctl == 'p')
        self->trans_len = 7;

    if (g_10058618[cls0_trk((uint8_t)c_cur)] & 0x20) {
        int32_t d = g_100581e8[TV_REF(uint8_t, g_10057e20t)[cls0_trk((uint8_t)c_cur)]];
        int32_t wr2 = self->trk_wr[2];

        self->trans_len += d;
        self->trk_param[2][3] += d;
        self->trk_param[0][3] += d;
        self->trk_param[13][3] += d;
        wr2 -= d;
        self->trk_wr[2] = wr2;
        self->trk_wr[0] -= d;
        self->trk_wr[13] -= d;
        self->trk_rd[2] = wr2;
    }

    if (g_10058618[cls0_trk((uint8_t)c_cur)] & 0x40) {
        self->trans_len = 1;
        if (!(g_10058618[cls0_trk((uint8_t)c_ctl)] & 1))
            self->trans_len = self->trk_param[2][3] / 2;
    }
    if (self->trans_len > self->trk_param[2][3])
        self->trans_len = self->trk_param[2][3];
    if (self->trans_len > self->trk_param[0][3])
        self->trans_len = self->trk_param[0][3];
    if (self->trans_len <= 0)
        return;

    if (g_10058618[cls0_trk((uint8_t)c_cur)] & 0x40) {
        int32_t wr2 = self->trk_wr[2];

        Track_BlendDelta(self, self->trk_buf[2], 2, wr2, 3,
                         wr2 - self->trk_rd[2],
                         self->s3_47c - self->trk_490[2]);
    }
    n = self->trans_len;
    Track_Fill(self->trk_buf[0], self->trk_wr[0], n, 0);
    Track_Fill(self->trk_buf[2], self->trk_wr[2], self->trans_len,
               (uint8_t)self->s3_47c);
    Track_Fill(self->trk_buf[13], self->trk_wr[13], self->trans_len, 0x4b);

    n = self->trans_len;
    self->trk_490[2] = self->s3_47c;
    self->trk_param[2][3] -= n;
    self->trk_wr[2] += n;
    self->trk_param[0][3] -= n;
    self->trk_wr[0] += n;
    self->trk_param[13][3] -= n;
    self->trk_490[13] = 0x96;
    self->trk_param[0][0] = 6;
    self->trk_wr[13] += n;
}
