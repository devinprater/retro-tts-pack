/*
 * Stage 3 helpers.
 *
 * Stage 3 turns phonemes and their durations into the 22 parameter tracks
 * the synthesiser reads, so it is where a phoneme stops being a symbol and
 * becomes a set of formant targets moving over time.
 *
 * Stage3_Build is the driver: one call per phoneme, and every correction
 * pass in lang/spa/engine/adjust.c, lang/spa/engine/phone.c and lang/spa/engine/stage3seg.c hangs off it.
 *
 * Stage3_Insert puts a pause into the stream: two silence nodes, the first
 * carrying the length and the second a fixed tail, with stress bits set so
 * that nothing downstream mistakes them for speech.
 */
#include "es_engine.h"

/* @0x100613b0 */
extern const uint8_t g_param_init[22];
/* @0x10049910 */
extern const uint32_t g_bit_mask[8];

/* Ten rows of 22, one byte per track, chosen by four bits of the pause
 * node's flags.  The flags can in principle make an index of up to 15, which
 * is past the tenth row; both implementations then read the same following
 * bytes of .rdata, so the array is declared wide enough to say so rather
 * than to claim sixteen rows exist. */
/* @0x10048a98 */
extern const uint8_t g_pause_param[16 * 22];
/* Sixteen levels, indexed by s3_628 >> 4. */
/* @0x10048b78 */
extern const uint8_t g_pause_level[16];

/*
 * A pause, written straight into the tracks.
 *
 * A type 4 node does not go through the phoneme machinery at all.  It names
 * one 22-byte parameter frame -- `mode` says where the frame comes from --
 * and that same frame is then written into all 22 track buffers once per
 * sample, for `count` samples or until the `room` the buffers have left runs
 * out, whichever comes first.  What is still owed comes back as the result,
 * and the caller asks again next time round.
 *
 * Only 't' builds the frame from anything; every type 4 node this engine
 * makes has the value ' ', which is none of the three, so the frame is then
 * whatever the last 't' left -- zeroes, for a pause before any speech.  The
 * 'g' and 's' arms are reachable only from outside.
 *
 * 't' takes a row of g_pause_param picked by four bits of the node's flags.
 * An 0xff in a cell means "not in the table", and each of those is filled in
 * from the engine instead: tracks 9 to 11 from s3_628, track 17 from the
 * node's b15, tracks 0 and 1 from a level less the host's attenuation, and
 * tracks 13 to 15 from a fixed 0x2d once s3_628 has gone over 0xb4.
 */
/* @0x1001b250 */
int32_t TV_THISCALL Stage3_FillFrames(Engine *self, uint8_t mode, int32_t count,
                                      int32_t room, uint8_t rebuild)
{
    int32_t cursor, i;

    if (rebuild) {
        self->s3_628 = 0;
        if (mode == 'g') {
            for (i = 0; i < 22; i++)
                self->s3_frame[i] = self->s3_param_raw[i];
        } else if (mode == 's') {
            for (i = 0; i < 22; i++)
                self->s3_frame[i] = g_param_init[i];
        } else if (mode == 't') {
            Node *ctl = self->stage_ctx[3].ctl;
            uint32_t f = ctl->flags;
            int32_t row = (int32_t)((f & 0x18) >> 3);
            /* set when track 9's cell is not in the table; the original
             * reads it out of an uninitialised stack byte before the loop,
             * which is dead -- the same condition makes track 9 assign it
             * on the first pass, and nothing reads it before track 13 */
            int over = 0;

            if (f & 0x20)
                row += 4;
            if (f & 0x40)
                row += 8;
            row *= 22;
            if (g_pause_param[row + 9] == 0xff)
                self->s3_628 = ctl->b15;

            for (i = 0; i < 22; i++) {
                int32_t v = g_pause_param[row + i];

                if (g_pause_param[row + 9] == 0xff && i >= 13 && i <= 15 &&
                    over)
                    v = 0x2d;
                if (v == 0xff) {
                    if (i >= 9 && i <= 11) {
                        v = self->s3_628;
                        over = self->s3_628 > 0xb4;
                    } else if (i == 17) {
                        v = self->stage_ctx[3].ctl->b15;
                    } else if (i == 0 || i == 1) {
                        v = 0x3c - self->stage_ctx[3].volume_atten;
                        if (self->s3_628 != 0)
                            v -= g_pause_level[self->s3_628 >> 4];
                    }
                }
                self->s3_frame[i] = (uint8_t)v;
            }
        }
    }

    cursor = self->trk_0c;
    while (count > 0 && room > 0) {
        int32_t at = cursor & 0xff;

        for (i = 0; i < 22; i++)
            self->trk_buf[i][at] = self->s3_frame[i];
        if (self->s3_628 != 0) {
            uint32_t p = (uint32_t)(cursor - 1);

            self->s3_1fbd[(p & 0xf8u) >> 3] |= (uint8_t)g_bit_mask[p & 7u];
        }
        count--;
        cursor++;
        room--;
    }

    for (i = 0; i < 22; i++) {
        self->trk_rd[i] = cursor - self->s3_1fe0;
        self->trk_wr[i] = cursor;
    }
    self->trk_0c = cursor;
    self->trk_10 = cursor;
    return count;
}

/* @0x1001b810 */
void TV_THISCALL Stage3_Reset(Engine *self)
{
    int i;

    /* English clears two more fields first, s3_2034 and s3_2035, which this
     * engine does not have. */
    self->s3_1fe8 = 4;
    self->s3_1fb0 = 0;
    self->s3_1fbc = 0;
    self->s3_1fdd = 0;
    self->s3_1fae = 1;
    self->s3_1fb4 = 1;
    self->s3_1fe4 = 3;
    self->s3_1fe0 = 2;
    self->stage_ctx[3].type_mask = 0x28; /* types 3 and 5 */
    self->s3_1fb8 = 0;
    for (i = 0; i < 22; i++)
        self->s3_param_raw[i] = self->cfg_bytes[i];
    Stage3_ResetParams(self);
}

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/* A per-voice Q15 gain on the three amplitude tracks.  Ten int32 at
 * 0x1004c7c0, and in this build every one of them is zero, so the three
 * scalings below come to nothing and only the clamp on track 12 survives.
 * Nothing else in the image reads or writes it. */
/* @0x1004c7c0 */
extern const int32_t g_1004c7c0[10];

/*: the two ways this file indexes the flag table; see lang/spa/engine/adjust.c for what
 * the four blocks are. */
static int32_t cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t cls100(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

static int32_t cls180(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x180);
}

/*
 * One phoneme's worth of stage 3.
 *
 * Stage3_Run walks the node list and calls this once per phoneme; every
 * other function in lang/spa/engine/adjust.c, lang/spa/engine/phone.c and lang/spa/engine/stage3seg.c is called
 * from here.  The shape is a fixed sequence of correction passes, each
 * gated on the class flags of one of stage 3's three nodes, then the host's
 * voice and volume, then all 22 tracks emitted into their buffers.
 *
 * Two bits of g_10058618's unshifted block do most of the gating, and the
 * table says which phonemes they cover:
 *
 *   bit 2 (0x04)  every letter but C F K P S T X, and not space
 *   bit 1 (0x02)  A E I L O R U and the whole lower case set but c f n r s
 *
 * so the first pass takes Track_AdjustTrill for anything voiced and
 * Track_AdjustPause for a voiceless stop or a space, and the segment pass
 * below runs for a voiced phoneme only.  Bit 0x20 is the stops B C D G K P
 * T Y and 0x10 the nasals M N n ~.  In the 0x100 block bit 1 is the stops
 * plus the nasals plus r, and bit 2 is the vowels.
 *
 * Two of the corrections at the end are keyed on the phonemes as they stood
 * before Stage3_LoadPhone ran, which is why the prologue copies the two
 * characters into locals; everything else rereads the nodes.
 */
/* @0x1001b880 */
void TV_THISCALL Stage3_Build(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    uint8_t ctl0 = st->ctl->value;
    uint8_t cur0 = 0;
    int32_t atten, voice, n, count, d, i;

    if (st->cur != NULL)
        cur0 = st->cur->value;

    Stage3_LoadPhone(self);
    Track_AdjustWeights(self);
    if (g_10058618[cls0(st->ctl->value)] & 4)
        Track_AdjustTrill(self);
    else
        Track_AdjustPause(self);

    /* a stop burst and its shape, for a closure that the current node ends
     * rather than the control node */
    if (!(g_10058618[cls100(st->ctl->value)] & 1) ||
        st->cur->value == 'P' || st->cur->value == 'D' ||
        st->cur->value == 'T') {
        if (g_10058618[cls100(st->cur->value)] & 1) {
            if (g_10058618[cls0(st->cur->value)] & 0x20)
                Track_StopBurst(self);
            Track_SetShapes(self);
        }
    }

    if (g_10058618[cls0(st->ctl->value)] & 2) {
        Track_AdjustBoundary(self);
        if (g_10058618[cls100(st->ctl->value)] & 2)
            Stage3_Segment(self);
        else
            Stage3_BlendTriples(self);
    }

    {
        uint8_t f = g_10058618[cls0(st->ctl->value)];

        /* voiced and not nasal is the one case that skips the whole block */
        if (!(f & 2) || (f & 0x10)) {
            if (f & 0x60) {
                Track_AdjustBeforeR(self);
                if (g_10058618[cls0(st->ctl->value)] & 0x40)
                    Track_AdjustGap(self);
            }
            if (g_10058618[cls100(st->ctl->value)] & 1) {
                Track_SetModes(self);
                if (g_10058618[cls0(st->ctl->value)] & 0x20) {
                    Node *ctl = st->ctl;
                    uint8_t c_scan = st->scan->value;

                    if (!(g_10058618[cls100(c_scan)] & 1) ||
                        ctl->value == 'P' || ctl->value == 'D' ||
                        ctl->value == 'T') {
                        if (!(ctl->flags & 0x40) && c_scan != ' ')
                            Stage3_StopClosure(self);
                    }
                }
                if (g_10058618[cls0(st->ctl->value)] & 0x10)
                    Stage3_LoadTriples(self);
            }
        }
    }

    Track_AdjustTransition(self);
    Track_AdjustVelar(self);
    Track_Adjust(self);
    Track_Average(self);

    /* track 17 carries the voicing level, and an unvoiced control phoneme
     * parks it in s3_1fb8 instead of emitting it */
    if (!(g_10058618[cls0(st->ctl->value)] & 4)) {
        self->trk_param[17][0] &= ~2;
        self->s3_1fb8 = self->trk_param[17][6];
        self->trk_param[17][6] = 0;
    }
    if (st->ctl->b15 == 0) {
        self->trk_param[17][0] &= ~2;
        self->trk_param[2][6] = self->trk_param[0][6] + 5;
        self->trk_param[0][6] = 0;
    }
    if (!(g_10058618[cls0(st->cur->value)] & 4) || st->cur->b15 == 0)
        self->trk_param[17][0] &= ~1;

    /* the host's attenuation comes off columns 6 and 4 of tracks 0, 1 and
     * 2, in that order, each floored at zero */
    atten = st->volume_atten;
    for (i = 0; i < 3; i++) {
        int32_t k;

        for (k = 6; k >= 4; k -= 2) {
            int32_t v = self->trk_param[i][k] - atten;

            self->trk_param[i][k] = v < 0 ? 0 : v;
        }
    }

    voice = st->voice;
    if (voice != 0) {
        int32_t g = es_v_c7c0(voice);
        int32_t v;

        v = self->trk_param[9][6];
        self->trk_param[9][6] = v + Synth_MulQ15(v, g);
        v = self->trk_param[10][6];
        self->trk_param[10][6] = v + Synth_MulQ15(v, g);
        v = self->trk_param[11][6] + Synth_MulQ15(self->trk_param[11][6], g);
        self->trk_param[11][6] = v;
        /* track 12's share is scaled from track 11's new value, not its own */
        self->trk_param[12][6] += Synth_MulQ15(v, g);
        if (self->trk_param[12][6] > 0xfff)
            self->trk_param[12][6] = 0xfff;
    }

    /* tracks 20 and 21 are how the voice number and the attenuation reach
     * the synthesiser: shifted up four and or-ed in, not assigned */
    self->trk_param[21][6] |= st->voice << 4;
    self->trk_param[20][6] |= st->volume_atten << 4;

    if (self->s3_1fdd != 0) {
        for (i = 0; i < 22; i++)
            self->trk_rd[i] = self->trk_wr[i];
        self->s3_1fdd = 0;
    }

    if (ctl0 == 'B') {
        self->trk_param[0][4] = 0;
        self->trk_param[0][5] = 0;
        self->trk_param[0][0] = 7;
        self->trk_param[0][2] = 5;
    }
    if (ctl0 == 'K' && cur0 == ' ') {
        self->trk_param[1][5] = 0x44;
        self->trk_param[1][6] = 0x44;
        self->trk_param[3][6] = 0x41;
        self->trk_param[6][6] = 0x3c;
        self->trk_param[10][6] = 0x76c;
        self->trk_param[11][6] = 0x898;
        self->trk_param[7][6] = 0;
        self->trk_param[1][3] = 2;
        self->trk_param[1][0] = 4;
        self->trk_param[6][0] = 4;
        self->trk_param[4][6] = 0x14;
        self->trk_param[5][6] = 0x14;
    }

    for (i = 0; i < 22; i++)
        Track_Emit(self, i);

    /* flag 0x40 on the control node asks for track 17 again, on its own and
     * with a fixed shape */
    if (st->ctl->flags & 0x40) {
        self->trk_param[17][4] = 0x28;
        self->trk_param[17][5] = 0x28;
        self->trk_param[17][2] = 2;
        self->trk_param[17][1] = 5;
        self->trk_param[17][0] = 3;
        Track_Emit(self, 17);
    }

    /*
     * And last, the two formant tracks are pulled toward each other -- but
     * only across a big enough move in the same direction.  s3_450 and
     * s3_454 are the two frequencies Stage3_LoadPhone leaves one segment
     * apart; both have to be at least 0x19a0 in magnitude, and the smaller
     * of the two halves has to be negative, which is to say they must not
     * both be rising.  s3_650 with a vowel either side cancels it outright.
     */
    if (self->s3_650 != 0) {
        if (g_10058618[cls100(st->ctl->value)] & 2)
            return;
        if (g_10058618[cls100(st->cur->value)] & 2)
            return;
    }

    d = self->s3_450 < 0 ? -self->s3_450 : self->s3_450;
    if (d < 0x19a0)
        return;
    d = self->s3_454 < 0 ? -self->s3_454 : self->s3_454;
    if (d < 0x19a0)
        return;
    d = self->s3_454 / 2 - self->s3_450 / 2;
    if (d >= 0) {
        if (self->s3_450 > 0)
            return;
    } else if (self->s3_454 > 0) {
        return;
    }

    /* how far back the run starts, from the two class indices */
    if (self->s3_44c < self->s3_448)
        n = 3;
    else if (self->s3_44c > self->s3_448)
        n = 9;
    else
        n = 6;
    count = self->trk_param[10][3] + n;
    if (count > 12)
        count = 12;
    Track_Couple(self, self->trk_wr[10] - n, count, d < 0 ? -d : d);
}

/* @0x1001be30 */
Node *TV_THISCALL Stage3_Insert(Engine *self, Node *ref, int32_t mode)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *a, *b;
    uint32_t f;

    a = Engine_NodeAlloc(self, ref, 1, 4, ' ');
    /* English uses 4 here; this engine uses 15 */
    if (mode == 1)
        a->arg = 0xf;
    else
        a->arg = (uint32_t)(self->s3_1fe0 + self->s3_1fe4 + 0xa);
    a->b15 = (st->ctl != NULL) ? st->ctl->b15 : 0x32;
    f = a->flags & ~0x20u;
    a->flags = f;
    f &= ~0x40u;
    a->flags = f;
    f = (f & ~8u) | 0x10u;
    a->flags = f;

    b = Engine_NodeAlloc(self, a, 1, 4, ' ');
    b->arg = 4;
    b->b15 = (st->ctl != NULL) ? st->ctl->b15 : 0x32;
    f = b->flags & ~0x20u;
    b->flags = f;
    f &= ~0x40u;
    b->flags = f;
    f |= 0x18u;
    b->flags = f;

    if (st->ctl == NULL) {
        st->ctl = a;
        a = Engine_StageNext(self, a);
    }
    self->trk_08 = -1;
    self->synth_busy = 0;
    return a;
}

/*
 * Back to the start of every track.
 *
 * Every cursor goes to trk_0c -- the read ones s3_1fe0 samples behind it --
 * and every trk_490 to the scaled default for its track.  Called when
 * stage 3 picks up a new phoneme after a pause, and by Stage3_Reset.
 */
/* @0x1001bf10 */
void TV_THISCALL Stage3_ResetParams(Engine *self)
{
    int32_t i;

    self->s3_454 = -0x3340;
    self->s3_450 = -0x3340;
    self->s3_458 = 0;
    self->s3_448 = 0;
    self->s3_44c = 0;
    self->s3_464 = 0;
    self->s3_460 = 0;
    self->s3_46c = 0;
    self->s3_468 = 0;
    self->s3_474 = 0;
    self->trans_len = 0;
    self->s3_470 = 0;
    self->s3_480 = 0;
    self->s3_47c = 0;
    self->s3_478 = 0;
    for (i = 0; i < 22; i++) {
        self->trk_rd[i] = self->trk_0c - self->s3_1fe0;
        self->trk_wr[i] = self->trk_0c;
        self->trk_490[i] = Synth_ScaleParam(i, g_param_init[i]);
    }
    self->trk_10 = self->trk_0c;
}

/*
 * Commit what stage 3 has laid down.
 *
 * Every track's write cursor moves on by the duration it was given, its last
 * written sample becomes its new trk_490 -- scaled the way that track wants
 * it, so the next segment starts where this one ended -- and its read cursor
 * is pulled forward if it has fallen more than s3_1fe0 behind.  trk_0c and
 * trk_10 come out as the earliest and the latest cursor, so the synthesiser
 * knows how much of the 22 tracks is complete.
 */
/* @0x1001c040 */
void TV_THISCALL Track_Commit(Engine *self)
{
    int32_t lo = 0x1000, hi = -1;
    int32_t i;

    for (i = 0; i < 22; i++) {
        int32_t at = self->trk_wr[i] + self->trk_param[i][3];
        int32_t back;

        self->trk_wr[i] = at;
        self->trk_490[i] = Synth_ScaleParam(i,
                               self->trk_buf[i][(at - 1) & 0xff]);
        if (lo > at)
            lo = at;
        if (hi < at)
            hi = at;
        back = at - self->s3_1fe0;
        if (self->trk_rd[i] < back)
            self->trk_rd[i] = back;
    }
    {
        uint32_t p = (uint32_t)(lo - 1);

        self->trk_14[(p & 0xf8u) >> 3] |= (uint8_t)g_bit_mask[p & 7u];
    }
    self->trk_10 = hi;
    self->trk_0c = lo;
}

/*
 * The pause, one call at a time.
 *
 * When stage 3 is on a pause rather than a phoneme it comes here instead of
 * Stage3_Build.  The first call consumes the node stage 3 has already done,
 * takes the length off the next one, and puts every track cursor back to the
 * start; after that each call hands as much of the length as the buffers have
 * room for to Stage3_FillFrames and keeps what is still owed in s3_60c.  Once
 * that reaches zero s3_1fae goes back up and the next call starts again on
 * whatever follows.
 */
/* @0x1001b170 */
int32_t TV_THISCALL Stage3_Fill(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    int32_t owed, room;

    if (self->s3_1fae != 0) {
        Node *ctl = st->ctl;

        if ((ctl->flags & 7) == 4)
            st->ctl = Engine_NodeFree(self, ctl, 1);
        self->s3_610 = st->ctl->value;
        self->s3_60c = (int32_t)(st->ctl->arg & 0xff);
        Stage3_ResetParams(self);
        self->s3_1fb0 = 3;
        self->s3_1fae = 0;
        self->s3_611 = 1;
    }

    /* how many more samples the track buffers can take */
    room = self->trk_04 - self->trk_10 - self->s3_1fe4 + 0x100;
    owed = self->s3_60c;
    if (owed != 0 && room != 0) {
        self->s3_60c = Stage3_FillFrames(self, self->s3_610, owed, room,
                                         self->s3_611);
        /* the first block of a pause restarts the synthesiser on it */
        if (self->s3_611 != 0) {
            self->trk_08 = -1;
            self->synth_busy = 0;
        }
        self->s3_611 = 0;
    }
    if (self->s3_60c == 0)
        self->s3_1fae = 1;
    /* what is still owed: Stage3_Run stops calling once it is zero */
    return self->s3_60c;
}

/*
 * What ends the current run, and the pause that goes with it.
 *
 * Stage 3 has finished the phonemes it can see; this looks ahead through the
 * window for what stops it and turns that into one of five kinds in s3_1fb0:
 *
 *   1  a 'C' node          4  ten nodes went by without any of the others
 *   2  an 'x' node, or an 'i' once the host has asked to stop
 *   3  a type 5 node       0  a pause is already there, so none is needed
 *
 * and then hands the kind and the node it stopped on to Stage3_Insert.  A
 * type 3 node whose value is one of , . ? does not stop the scan, but it does
 * restart the synthesiser and move s3_1fb4 on from 4 to 5.
 *
 * Before any of that, a pad node goes in ahead of the control node unless
 * the current node is already a pause -- four samples of type 4, carrying the
 * control node's b15, with the three per-stage flag bits rewritten.
 */
/* @0x1001c110 */
Node *TV_THISCALL Stage3_Pause(Engine *self)
{
    StageCtx *st = &self->stage_ctx[3];
    Node *at = st->ctl;
    int32_t kind, seen = 0;

    if (at != NULL && (st->cur == at || (st->cur->flags & 7) != 4)) {
        Node *pad = Engine_NodeAlloc(self, at, 0, 4, ' ');
        uint32_t f;

        st->cur = pad;
        pad->arg = 4;
        pad->b15 = st->ctl->b15;
        f = pad->flags & ~0x20u;
        pad->flags = f;
        f &= ~0x40u;
        pad->flags = f;
        f = (f & ~0x10u) | 8u;
        pad->flags = f;
    }

    at = st->ctl;
    while (at != NULL) {
        uint32_t type;

        at = Engine_StageNext(self, at);
        if (at == NULL)
            break;
        type = at->flags & 7;
        if (type == 4) {
            /* a pause of stage 3's own making, already in place */
            if (self->s3_1fb0 == 3 && at->value == ' ' &&
                (at->flags & 0x18) == 0x18)
                return at;
            self->s3_1fb0 = 0;
            break;
        }
        seen++;
        if (type == 0) {
            uint8_t c = at->value;

            if (c == 'C') {
                self->s3_1fb0 = 1;
                break;
            }
            if (c == 'x' || (self->stop_mark != 0 && c == 'i')) {
                at->flags |= 0x20;
                self->s3_1fb0 = 2;
                break;
            }
            continue;
        }
        if (type == 5) {
            self->s3_1fb0 = 3;
            break;
        }
        if (type != 3)
            continue;
        if (!(g_10058618[cls180(at->value)] & 0x80))
            continue;
        self->trk_08 = -1;
        self->synth_busy = 0;
        if (self->s3_1fb4 == 4)
            self->s3_1fb4 = 5;
    }

    if (seen >= 10)
        self->s3_1fb0 = 4;

    kind = self->s3_1fb0;
    if (kind != 0 && (at != NULL || kind == 4)) {
        if (kind == 1)
            return Stage3_Insert(self, at, 1);
        if (kind == 3)
            return Stage3_Insert(self, st->first == at ? NULL : at->prev, 3);
        if (kind == 4)
            return Stage3_Insert(self, st->last, 4);
        if (kind == 2)
            return Stage3_Insert(self, at, 2);
        return at;
    }
    /* nothing to insert, or nowhere to put it */
    self->trk_08 = -1;
    return at;
}

/*
 * Stage 3, one step at a time.
 *
 * Engine_Step calls this in turn with the other four stages and reads the
 * result: 0 means the window had nothing in it, 1 that a pause was handed a
 * block and the step is over, 2 that a phoneme was built.  Everything below
 * is a decision about which of three things this step is -- fill a pause,
 * hold on a node the synthesiser is not ready for, or build a phoneme -- and
 * then the bookkeeping that moves the window on.
 *
 * s3_1fb4 is a small state machine over the whole utterance, running 0 -> 1
 * -> 2 -> 3 and 0 -> 4 -> 5, and what moves it is how far ahead of the
 * synthesiser the tracks have got: trk_0c - trk_04 against s3_1fe0.  Those
 * three margins are set at the top, and they are three times wider for a
 * slow voice than a fast one.
 */
/* @0x1001ad30 */
int32_t TV_THISCALL Stage3_Run(Engine *self)
{
    enum { FILL, HOLD, PAUSE };
    StageCtx *st = &self->stage_ctx[3];
    Node *ctl, *nx;
    int32_t kind, owed, what;

    Engine_StageBegin(self, st);
    if (st->cur == NULL && st->ctl == NULL && st->scan == NULL)
        return Engine_StageEnd(self) == 0 ? 0 : 2;

    if (!(st->p_34 & 0x2000) && st->rate_index < 0x13) {
        self->s3_1fe8 = 0x14;
        self->s3_1fe4 = 0xf;
        self->s3_1fe0 = 0xc;
    } else {
        self->s3_1fe8 = 4;
        self->s3_1fe4 = 3;
        self->s3_1fe0 = 2;
    }

    if (self->trk_08 != -1 &&
        self->trk_0c - self->trk_04 - self->s3_1fe0 <= 8 &&
        self->s3_1fb4 == 0)
        self->s3_1fb4 = 1;

    ctl = st->ctl;
    kind = self->s3_1fb0;
    if (kind == 2 && (ctl->flags & 7) == 4 && ctl->value == ' ' &&
        (ctl->flags & 0x18) == 0x18) {
        self->s3_1fbc = 1;
        return Engine_StageEnd(self) == 0 ? 0 : 2;
    }

    /* which of the three this step is.  The order matters: a step that
     * goes straight to the pause below leaves s3_1fb0 alone, while one that
     * gets as far as choosing between filling and holding sets it to 3
     * first. */
    if (kind != 3 && ctl == NULL) {
        what = PAUSE;
    } else if (kind != 3 && (ctl->flags & 7) == 4) {
        what = HOLD;
    } else {
        self->s3_1fb0 = 3;
        if (ctl == NULL)
            what = PAUSE;
        else if ((ctl->flags & 7) == 4 &&
                 !(ctl->value == ' ' && (ctl->flags & 0x18) == 0x18))
            what = HOLD;
        else
            what = FILL;
    }

    if (what == FILL) {
        /* a pause node: hand it as much as the tracks will take */
        owed = Stage3_Fill(self);
        Tracks_Op(self, 2, -1);
        self->s3_1fdd = 1;
        self->trk_38 = owed < 1 ? owed : 1;
        if (owed == 0) {
            Node *c = st->ctl;

            st->cur = c;
            c = Engine_StageNext(self, c);
            st->scan = c;
            st->ctl = c;
            self->s3_1fb0 = 0;
        }
        self->s3_1fb4 = 5;
        return Engine_StageEnd(self) == 0 ? 1 : 2;
    }

    if (what == HOLD) {
        /* the node carries a length the synthesiser has to take first */
        self->trk_38 = (int32_t)ctl->arg;
        if (Tracks_Op(self, 1, (int32_t)ctl->arg) == 0) {
            self->trk_08 = -1;
            self->synth_busy = 0;
            if (self->s3_1fb4 == 4)
                self->s3_1fb4 = 5;
            return Engine_StageEnd(self) == 0 ? 1 : 2;
        }
        if (st->p_20 == 0) {
            self->trk_08 = -1;
            self->synth_busy = 0;
            if (self->s3_1fb4 == 4)
                self->s3_1fb4 = 5;
        }
    }

    nx = Stage3_Pause(self);
    if (nx == NULL)
        return Engine_StageEnd(self) == 0 ? 0 : 2;
    st->scan = nx;

    if (st->ctl->value != ' ') {
        if (self->s3_1fb4 == 0)
            self->s3_1fb4 = 4;
        else if (self->s3_1fb4 == 1)
            self->s3_1fb4 = 2;
    }

    Stage3_Build(self);
    Track_Commit(self);

    ctl = st->ctl;
    if (!(g_10058618[cls0(ctl->value)] & 4))
        self->trk_490[17] = self->s3_1fb8;
    if (ctl->n_12 == 1) {
        self->s3_1fb8 = 100;
        self->trk_490[17] = 100;
    }

    /* the pad Stage3_Pause put in front of the node is done with */
    {
        Node *cur = st->cur;
        uint32_t f = cur->flags;

        if ((f & 7) == 4 && cur->value == ' ' && (f & 0x18) == 8)
            Engine_NodeFree(self, cur, 1);
    }

    if (self->s3_1fb4 == 2 &&
        self->trk_0c - self->trk_04 - self->s3_1fe0 > 8) {
        self->trk_08 = -1;
        self->synth_busy = 0;
        self->s3_1fb4 = 3;
    }

    {
        Node *c = st->ctl;

        st->cur = c;
        st->ctl = Engine_StageNext(self, c);
        st->scan = Engine_StageNext(self, st->scan);
    }

    ctl = st->ctl;
    if ((ctl->flags & 7) == 4 && ctl->value == ' ' &&
        (ctl->flags & 0x18) == 0x18) {
        Node *at = ctl;

        self->synth_busy = 0;
        self->trk_08 = self->trk_0c - self->s3_1fe0;
        for (;;) {
            uint32_t t;

            at = Engine_StageNext(self, at);
            if (at == NULL)
                break;
            t = at->flags & 7;
            if (t == 4 || t == 5)
                break;
        }
        /* nothing else in the window, so the whole utterance is over */
        if (at == NULL) {
            uint32_t p;

            Engine_NodeFree(self, st->ctl, 0);
            st->cur = NULL;
            st->ctl = NULL;
            st->scan = NULL;
            p = (uint32_t)(self->trk_0c - 1);
            self->trk_14[(p & 0xf8u) >> 3] &=
                (uint8_t)~(uint32_t)(uint8_t)g_bit_mask[p & 7u];
            p = (uint32_t)(self->trk_0c - self->s3_1fe0 - 1);
            self->trk_14[(p & 0xf8u) >> 3] |= (uint8_t)g_bit_mask[p & 7u];
        }
    }

    Tracks_Op(self, 2, -1);
    Engine_StageEnd(self);
    return 2;
}
