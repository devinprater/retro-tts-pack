/*
 * The per-phoneme corrections to the track parameters.
 *
 * Everything else in this subtree works from tables.  This function is the
 * exceptions: two dozen tests on the three phonemes stage 3 has in hand,
 * each one nudging particular cells of trk_param.  It reads like a list of
 * fixes accumulated against real speech, and most of the constants are bare
 * numbers with nothing to derive them from, so they are transcribed as they
 * are rather than explained.
 *
 * The three phonemes.  `cur`, `ctl` and `scan` are stage 3's three node
 * pointers; which of them holds a vowel and which a consonant is not fixed
 * here the way it is in lang/spa/engine/cluster.c -- `ctl`'s value is tested against
 * consonants in one place and against 'O' and 'A' in another -- so they are
 * named after the pointers rather than after phoneme roles.
 *
 * g_10058618 is indexed three different ways, with 0x80, 0x100 or 0x180 set
 * in the phoneme value.  So it is four blocks of 128 flags and each test
 * picks its block; every phoneme letter is below 0x80, which is what makes
 * the sign-extension the original does harmless.
 *
 * Two loops rather than straight-line code: one over all 22 tracks that
 * clamps three cells and mixes trk_490 with trk_param[i][6] by trk_4e8[i]
 * as a Q15 weight, and one over tracks 9 to 12.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];

/*: the three ways the original indexes the flag table.  Each sign-extends
 * the value to sixteen bits, sets a block bit and sign-extends again; for a
 * value below 0x80 that is just an or. */
static int32_t cls0(uint8_t v)
{
    return (int32_t)(int16_t)(int8_t)v;
}

static int32_t cls80(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x80);
}

static int32_t cls100(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x100);
}

static int32_t cls180(uint8_t v)
{
    return (int32_t)(int16_t)((int16_t)(int8_t)v | 0x180);
}

/*: 'A' 'E' 'I' 'L' 'r' take one value for trk_param[10][6], 'O' and 'U'
 * another, everything else none.  A 50-byte table at 0x10011590 through an
 * eight-arm jump. */
static int adj_scan_arm(int32_t c)
{
    switch (c) {
    case 'A': case 'E': case 'I': case 'L': case 'r': return 0;
    case 'O': case 'U':                               return 1;
    default:                                          return 2;
    }
}

/*: 'A' alone, then the other four vowels, then nothing.  0x100115dc. */
static int adj_y_arm(int32_t c)
{
    switch (c) {
    case 'A':                               return 0;
    case 'E': case 'I': case 'O': case 'U': return 1;
    default:                                return 2;
    }
}

/*: the eleven values of `ctl` that let the 'b'/'g' correction through.  A
 * 55-byte table at 0x10011624 through a twelve-arm jump, of which five arms
 * do nothing. */
static int adj_bg_ok(int32_t c)
{
    switch (c) {
    case 'A': case 'E': case 'I':
    case 'a': case 'e':
    case 'h': case 'i': case 'j': case 'k':
    case 'v': case 'w':
        return 1;
    default:
        return 0;
    }
}

/* @0x10010ba0 */
void TV_THISCALL Track_Adjust(Engine *self)
{
    Node *cur = self->stage_ctx[3].cur;
    Node *ctl = self->stage_ctx[3].ctl;
    Node *scan = self->stage_ctx[3].scan;
    int32_t c_cur = cur->value;
    int32_t c_ctl = ctl->value;
    int32_t c_scan = scan->value;
    int32_t i;

    /* 'L' 'E' 'g' before a back vowel */
    if (c_ctl == 'L' && c_cur == 'E' && c_scan == 'g') {
        int32_t v = scan->next->value;

        if (v == 'O' || v == 'U') {
            self->trk_param[10][6] = 0x774;
            self->trk_param[11][6] = 0xb10;
            self->trk_param[12][6] = 0xdb0;
        }
    }

    /* a tap that is not next to a trill: pull three tracks toward the
     * average of where they are and where they were, at 0x2a48 in Q15 */
    if (c_ctl == 'r' && c_cur != 'R' && c_scan != 'R') {
        self->trk_param[9][6] = Synth_MulQ15(
            (self->trk_55c[9] >> 1) + (self->trk_490[9] >> 1) +
            self->trk_param[9][6], 0x2a48);
        self->trk_param[10][6] = Synth_MulQ15(
            self->trk_55c[10] + self->trk_490[10] +
            self->trk_param[10][6], 0x2a48);
        self->trk_param[11][6] = Synth_MulQ15(
            self->trk_55c[11] + self->trk_490[11] +
            self->trk_param[11][6], 0x2a48);
    }

    if (g_10058618[cls180((uint8_t)c_cur)] & 8)
        self->trk_param[10][2] -= 2;

    /* keep tracks 10, 11 and 12 at least 200 above the one below, and
     * cascading, so raising 10 can raise 11 in the same pass */
    if (self->trk_param[0][6] > 0 || self->trk_param[2][6] > 0)
        for (i = 0; i < 3; i++) {
            int32_t c = self->trk_param[9 + i][6] + 0xc8;

            if (self->trk_param[10 + i][6] < c)
                self->trk_param[10 + i][6] = c;
        }

    if (c_ctl == 'r' && c_cur == 'D') {
        self->trk_4e8[13] = 0x12c;
        self->trk_4e8[14] = 0xfa;
        self->trk_4e8[15] = 0x190;
    }

    if (Is_Vowel((uint8_t)c_cur) && c_ctl == 'M') {
        self->trk_4e8[10] = 0;
        self->trk_4e8[11] = 0;
    }

    if ((c_ctl == 'M' || c_ctl == 'N' || c_ctl == 'n') &&
        Is_Vowel((uint8_t)c_scan) &&
        !(g_10058618[cls100(ctl->next->next->value)] & 2))
        self->trk_param[9][6] = 0x10e;

    /* every track: clamp, then mix where it was with where it is going */
    for (i = 0; i < 22; i++) {
        int32_t v, x, s;

        if (i >= 9 && i <= 15 && self->s3_650 != 0 &&
            (g_10058618[cls100((uint8_t)c_ctl)] & 2))
            continue;
        v = self->s3_1fe8;
        if (v >= self->trk_param[i][2])
            v = self->trk_param[i][2];
        self->trk_param[i][2] = v;
        self->trk_param[i][1] = v;
        if (self->trk_param[i][3] >= 0x3c)
            self->trk_param[i][3] = 0x3c;
        x = self->trk_4e8[i];
        s = Synth_MulQ15(self->trk_490[i], 0x7ffe - x);
        s += Synth_MulQ15(self->trk_param[i][6], x);
        self->trk_param[i][5] = s;
        self->trk_param[i][4] = s;
    }

    /* a vowel after a consonant that is none of these gets the whole
     * consonant-pair lookup run over it again */
    if ((g_10058618[cls100((uint8_t)c_scan)] & 2) &&
        c_ctl != 'M' && c_ctl != 'N' && c_ctl != 'n' && c_ctl != 'L' &&
        c_ctl != '~' && c_ctl != 'R' && c_ctl != 'r' && c_ctl != 'Y' &&
        c_ctl != 'b' && c_ctl != 'P' && c_ctl != 'T' && c_ctl != 'K' &&
        c_ctl != 'C' && c_ctl != 'B') {
        int32_t vi = Vowel_Index((uint8_t)c_scan);

        if (vi != -1)
            Cluster_SetTracks(self, vi);
    }

    if (Is_Vowel((uint8_t)c_cur) &&
        !(g_10058618[cls100((uint8_t)c_ctl)] & 2) &&
        c_ctl != 'M' && c_ctl != 'N' && c_ctl != 'n' && c_ctl != '~')
        for (i = 0; i < 4; i++) {
            int32_t v = self->trk_490[9 + i];

            self->trk_param[9 + i][5] = v;
            self->trk_param[9 + i][4] = v;
            self->trk_param[9 + i][0] = 6;
        }

    if (Is_Vowel((uint8_t)c_cur) &&
        (g_10058618[cls100((uint8_t)c_ctl)] & 1))
        for (i = 0; i < 4; i++)
            self->trk_param[9 + i][0] = 6;

    if (c_cur == 'I') {
        if (c_ctl == 'M' || c_ctl == 'X')
            self->trk_param[10][5] = self->trk_490[10] - 0xc8;
        if (c_ctl == 'N' || c_ctl == 'n') {
            self->trk_param[10][2] = 4;
            self->trk_param[10][5] = self->trk_490[10];
        }
    }

    /* an 'L' next to a vowel, either way round */
    if ((Is_Vowel((uint8_t)c_ctl) && c_cur == 'L') ||
        (c_ctl == 'L' && Is_Vowel((uint8_t)c_cur))) {
        self->trk_param[9][2] = 4;
        self->trk_param[9][1] = 4;
        self->trk_param[10][2] = 3;
        self->trk_param[10][1] = 3;
        self->trk_param[11][2] = 3;
        self->trk_param[11][1] = 3;
        self->trk_param[12][2] = 3;
        self->trk_param[12][1] = 3;
    }

    /* and a stop next to a vowel, either way round */
    if ((Is_Vowel((uint8_t)c_ctl) &&
         (c_cur == 'P' || c_cur == 'T' || c_cur == 'K' || c_cur == 'C' ||
          c_cur == 'B')) ||
        ((c_ctl == 'P' || c_ctl == 'T' || c_ctl == 'K' || c_ctl == 'C' ||
          c_ctl == 'B') && Is_Vowel((uint8_t)c_cur))) {
        self->trk_param[9][2] = 2;
        self->trk_param[9][1] = 2;
        self->trk_param[10][2] = 3;
        self->trk_param[10][1] = 3;
        self->trk_param[11][2] = 3;
        self->trk_param[11][1] = 3;
        self->trk_param[12][2] = 3;
        self->trk_param[12][1] = 3;
    }

    if (c_ctl == 'X' && c_scan == 'I')
        self->trk_param[10][6] = 0x7f3;

    if (c_ctl == 'b' &&
        (c_cur == 'A' || c_cur == 'E' || c_cur == 'I' || c_cur == 'O' ||
         c_cur == 'U' || c_cur == 'L' || c_cur == 'r')) {
        self->trk_param[9][6] = 0x1a4;
        self->trk_param[11][6] = 0x920;
        self->trk_param[12][6] = 0xce0;
        self->trk_param[8][6] = 0x3c;
        if ((uint32_t)((int8_t)c_scan - 0x41) <= 0x31u) {
            int arm = adj_scan_arm((int8_t)c_scan);

            if (arm == 0)
                self->trk_param[10][6] = 0x38c;
            else if (arm == 1)
                self->trk_param[10][6] = 0x30c;
        }
    }

    if (c_ctl == 'Y' && (uint32_t)((int8_t)c_scan - 0x41) <= 0x14u) {
        int arm = adj_y_arm((int8_t)c_scan);

        if (arm == 0) {
            self->trk_param[1][6] = 0x2e;
            self->trk_param[2][6] = 0x34;
        } else if (arm == 1) {
            self->trk_param[1][6] = 0x34;
            self->trk_param[2][6] = 0x37;
        }
    }

    if (self->s3_650 != 0 && (c_ctl == 'O' || c_ctl == 'A') && c_cur == 'N')
        self->trk_param[9][0] = 1;

    /* a vowel four nodes after the sequence r R r, itself after a labial:
     * redraw the last ten samples of track 10 */
    if (Is_Vowel((uint8_t)c_ctl)) {
        Node *p = ctl->prev;

        if (p->value == 'r') {
            p = p->prev;
            if (p->value == 'R') {
                p = p->prev;
                if (p->value == 'r') {
                    int32_t v = p->prev->value;

                    if (v == 'P' || v == 'b' || v == 'B') {
                        int32_t start = self->trk_wr[10] - 0xa;
                        uint8_t *buf = self->trk_buf[10];
                        int32_t to = (self->trk_param[10][4] - 0x1f4) >> 3;

                        Ramp_Fill(buf, start, 0xa, buf[start], to);
                    }
                }
            }
        }
    }

    if (g_10058618[cls100((uint8_t)c_cur)] & 0x40) {
        self->trk_param[9][4] -= 0x32;
        self->trk_param[9][5] += 0x32;
        self->trk_param[10][4] -= 0x32;
        self->trk_param[10][5] += 0x32;
    }

    if (c_ctl == 'b' && c_cur == 'L' && (c_scan == 'A' || c_scan == 'O')) {
        self->trk_param[9][5] = self->trk_param[9][4];
        self->trk_param[10][5] = self->trk_param[10][4];
    }

    if (c_ctl == 'd' && c_cur == 'L' && Is_Vowel((uint8_t)c_scan))
        self->trk_param[9][4] = self->trk_param[9][5];

    if (c_ctl == 'S' && (c_scan == 'O' || c_scan == 'U')) {
        self->trk_param[10][6] = 0x77c;
        self->trk_param[11][6] = 0xa00;
        self->trk_param[12][6] = 0xc90;
    }

    if (c_ctl == 'g' && c_cur == 'L' && c_scan == 'O') {
        self->trk_param[10][5] = 0x618;
        self->trk_param[10][4] = 0x618;
        self->trk_param[11][5] = 0x9dd;
        self->trk_param[11][4] = 0x9dd;
    }

    if ((g_10058618[cls80((uint8_t)c_ctl)] & 4) &&
        (c_cur == 'K' || c_cur == 'G')) {
        self->trk_param[10][1] = 1;
        self->trk_param[11][1] = 1;
    }

    if ((c_cur == 'b' || c_cur == 'g') && adj_bg_ok((int8_t)c_ctl) &&
        (uint32_t)((int8_t)c_ctl - 0x41) <= 0x36u) {
        int32_t v = self->trk_param[10][6];

        if (c_cur == 'b') {
            v -= 0xc8;
            if (v <= 0x1f4)
                v = 0x1f4;
            self->trk_param[10][5] = v;
            self->trk_param[11][5] = self->trk_param[11][6] - 0x64;
        } else {
            v += 0xc8;
            self->trk_param[10][5] = v;
            self->trk_param[11][5] = self->trk_param[11][6] - 0xc8;
        }
    }

    if (c_ctl == 'b' && (g_10058618[cls100((uint8_t)c_cur)] & 2)) {
        int32_t v = self->trk_490[10] - 0xc8;

        if (v <= 0x1f4)
            v = 0x1f4;
        self->trk_param[10][4] = v;
        self->trk_param[11][4] = self->trk_490[11] - 0x64;
    }

    if (c_ctl == 'g' && (g_10058618[cls100((uint8_t)c_cur)] & 2)) {
        self->trk_param[10][4] = self->trk_490[10] + 0xc8;
        self->trk_param[11][4] = self->trk_490[11] - 0xc8;
    }

    if (c_ctl == 'r' && (g_10058618[cls100((uint8_t)c_cur)] & 0x80)) {
        self->trk_param[10][0] = 4;
        self->trk_param[11][0] = 4;
        self->trk_param[10][4] = self->trk_490[10] + 0xc8;
        self->trk_param[11][4] = self->trk_490[11] - 0xc8;
    }

    if ((g_10058618[cls180((uint8_t)c_cur)] & 4) && c_ctl == 'O')
        self->trk_param[10][2] = 3;

    if (c_ctl == 'b' && c_cur == 'E' && c_scan == 'O')
        self->trk_param[10][4] = 0x54c;

    /* track 17 last: at least 7, at most s3_1fe8 */
    self->trk_param[17][2] = self->trk_param[17][3];
    if (self->trk_param[17][3] < 7)
        self->trk_param[17][2] = 7;
    if (self->trk_param[17][2] > self->s3_1fe8)
        self->trk_param[17][2] = self->s3_1fe8;
    self->trk_param[17][1] = 0;
}

/*
 * Three more correction passes.
 *
 * sub_1001b880 runs these in sequence with Track_Adjust and the rest, and
 * the grouping is the original's rather than anything derived: each is a
 * handful of unrelated tests that happened to be written together.  The
 * names describe the most distinctive test in each, not an established
 * role.
 */

/*
 * Blend weights, and two tracks flushed.
 *
 * trk_4e8 is the Q15 weight Track_Adjust mixes trk_490 against
 * trk_param[i][6] with, so 0x7ffe on tracks 9 to 11 means "take the new
 * value whole".  Setting trk_rd to trk_wr on a track discards whatever is
 * still buffered in it.
 */
/* @0x10014f20 */
void TV_THISCALL Track_AdjustWeights(Engine *self)
{
    int32_t c = self->stage_ctx[3].cur->value;

    self->trk_4e8[17] = 0;
    self->trk_param[17][0] = 6;
    if (g_10058618[cls180((uint8_t)c)] & 2) {
        self->trk_4e8[11] = 0x7ffe;
        self->trk_4e8[10] = 0x7ffe;
        self->trk_4e8[9] = 0x7ffe;
    }
    if (g_10058618[cls0((uint8_t)c)] & 0x20)
        self->trk_rd[1] = self->trk_wr[1];
    if (!(g_10058618[cls0((uint8_t)c)] & 4)) {
        self->trk_param[0][0] = 6;
        self->trk_rd[17] = self->trk_wr[17];
    }
}

/*: the three stops that are followed by a tap each get their own fix. */
/* @0x10016220 */
void TV_THISCALL Track_AdjustBeforeR(Engine *self)
{
    int32_t c = self->stage_ctx[3].ctl->value;
    int32_t scan = self->stage_ctx[3].scan->value;

    if ((g_10058618[cls0((uint8_t)c)] & 0x40) || c == 'C')
        self->trk_param[1][1] = 2;
    if (c == 'K' && scan == 'r')
        self->trk_param[10][6] = 0x4b0;
    if (c == 'B' && scan == 'r')
        self->trk_param[0][6] = 0x2d;
    if (c == 'D' && scan == 'r') {
        self->trk_param[1][6] = 0x38;
        self->trk_param[6][6] = 0x41;
        self->trk_param[7][6] = 0x39;
    }
}

/*: a minimum of 8 on trk_param[1][3], and a space resets five tracks. */
/* @0x100162b0 */
void TV_THISCALL Track_AdjustGap(Engine *self)
{
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_scan = self->stage_ctx[3].scan->value;
    int32_t c_cur;
    int32_t i;

    if (c_ctl == 'Z' && c_scan == 'A')
        self->trk_param[10][6] += 0xc8;
    else
        self->trk_param[9][6] += 0x64;

    c_cur = self->stage_ctx[3].cur->value;
    if (!(g_10058618[cls0((uint8_t)c_cur)] & 0x40) &&
        !(g_10058618[cls100((uint8_t)c_cur)] & 1) &&
        !(g_10058618[cls0((uint8_t)c_scan)] & 0x40) &&
        self->trk_param[1][3] < 8)
        self->trk_param[1][6] += 8 - self->trk_param[1][3];

    if (c_cur == ' ')
        for (i = 0; i < 5; i++)
            self->trk_param[3 + i][0] = 4;
}

/*: two more byte tables reached through a pointer variable, the first
 * indexed by the phoneme and the second by its entry. */
/* @0x10057e20 */
extern const tv_ref g_10057e20;
/* @0x100581e0 */
extern const uint8_t g_100581e0[256];

/*
 * Set the emit mode on every track, and the blend shape on tracks 9 to 16.
 *
 * Column 0 is the mode Track_Emit switches on and column 2 the shape index
 * its blends take, so this is what decides how the whole set gets written
 * out.  The shape comes from a two-level table lookup on the control
 * phoneme unless the current one is flagged, in which case it is 5; track 9
 * then takes half of it plus one, or 1 outright after a tap.
 */
/* @0x10016350 */
void TV_THISCALL Track_SetModes(Engine *self)
{
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_cur = self->stage_ctx[3].cur->value;
    int32_t c_scan = self->stage_ctx[3].scan->value;
    int32_t i, v;

    if (!(g_10058618[cls100((uint8_t)c_cur)] & 1)) {
        for (i = 0; i < 10; i++)
            self->trk_param[i][0] = 5;
        for (i = 0; i < 7; i++)
            self->trk_param[10 + i][0] = 7;
    }

    if (c_ctl == 'r' && (c_cur == 'R' || c_scan == 'R')) {
        self->trk_param[9][1] = 1;
        self->trk_param[9][2] = 1;
        for (i = 0; i < 10; i++)
            self->trk_param[i][0] = 4;
    }

    v = g_100581e0[TV_REF(uint8_t, g_10057e20)[cls0((uint8_t)c_ctl)]];
    if (g_10058618[cls80((uint8_t)c_cur)] & 2)
        v = 5;
    for (i = 0; i < 8; i++)
        self->trk_param[9 + i][2] = v;
    self->trk_param[9][2] = self->trk_param[9][2] / 2 + 1;
    if (c_ctl == 'r')
        self->trk_param[9][2] = 1;
}

/*
 * The trill and the tap next to each other.
 *
 * 'R' is the trill and 'r' the tap.  Which one is on the control node, and
 * what stage 3 has on either side of it, picks one of three settings for
 * track 0 -- and when a trill is followed by a tap, six more tracks with it.
 * Engine_StageNext and Engine_StagePrev are used rather than the raw links
 * because the neighbour wanted is the next one this stage will look at.
 */
/* @0x10014fb0 */
void TV_THISCALL Track_AdjustTrill(Engine *self)
{
    Node *ctl = self->stage_ctx[3].ctl;
    int32_t c = ctl->value;

    if (c == 'R') {
        Node *nx = Engine_StageNext(self, ctl);

        if ((g_10058618[cls0(nx->value)] & 2) ||
            (g_10058618[cls0(Engine_StageNext(self,
                        self->stage_ctx[3].ctl)->value)] & 0x10))
            self->trk_param[0][6] = 0x3b;
        nx = Engine_StageNext(self, self->stage_ctx[3].ctl);
        if (nx->value == 'r') {
            self->trk_param[0][6] = 0x37;
            self->trk_param[3][6] = 0;
            self->trk_param[4][6] = 0x2f;
            self->trk_param[6][6] = 0x3e;
            self->trk_param[13][6] = 0x3c;
            self->trk_param[14][6] = 0x64;
            self->trk_param[15][6] = 0x78;
        }
        return;
    }
    if (c != 'r')
        return;
    if (Engine_StageNext(self, ctl)->value == 'R' ||
        Engine_StagePrev(self, self->stage_ctx[3].ctl)->value == 'R')
        self->trk_param[0][6] = 0x19;
}

/*
 * The same two-level shape lookup Track_SetModes does, but keyed on the
 * current phoneme rather than the control one, and with its own mode values:
 * 6 on tracks 0 to 8 and 7 on 9 to 16.  A trill and a tap adjacent, either
 * way round, drops the first ten to 4.
 */
/* @0x10015410 */
void TV_THISCALL Track_SetShapes(Engine *self)
{
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_cur = self->stage_ctx[3].cur->value;
    int32_t c_scan = self->stage_ctx[3].scan->value;
    int32_t i;

    for (i = 0; i < 8; i++)
        self->trk_param[9 + i][2] =
            g_100581e0[TV_REF(uint8_t, g_10057e20)[cls0((uint8_t)self->stage_ctx[3].cur->value)]];
    self->trk_param[9][2] = self->trk_param[9][2] / 2 + 1;

    for (i = 0; i < 9; i++)
        self->trk_param[i][0] = 6;
    for (i = 0; i < 8; i++)
        self->trk_param[9 + i][0] = 7;

    if (c_ctl == 'r' && (c_cur == 'R' || c_scan == 'R')) {
        self->trk_param[9][2] = 1;
        self->trk_param[9][1] = 1;
        for (i = 0; i < 10; i++)
            self->trk_param[i][0] = 4;
    }
    if (c_ctl == 'R' && (c_cur == 'r' || c_scan == 'r'))
        for (i = 0; i < 10; i++)
            self->trk_param[i][0] = 4;

    if ((g_10058618[cls0((uint8_t)c_cur)] & 0x10) &&
        (g_10058618[cls0((uint8_t)c_ctl)] & 4))
        self->trk_param[0][0] = 4;
}

/*: the space before a phoneme that is neither a space nor a tap makes the
 * pause six shorter and winds the tracks back to match. */
/* @0x10015090 */
void TV_THISCALL Track_AdjustPause(Engine *self)
{
    int32_t v = self->s3_478;

    self->s3_47c = v;
    if (self->stage_ctx[3].ctl->value == ' ') {
        int32_t c = self->stage_ctx[3].cur->value;

        if (c != ' ' && c != 'r') {
            self->s3_458 = 1;
            self->s3_47c = v - 6;
            Track_Shorten(self);
        }
    }
    self->trk_param[17][0] = 4;
    self->trk_param[0][0] = 5;
}

/*
 * Four settings keyed on the velars and the nasal.
 *
 * 'K' and 'G' are tested twice, '~' once, and the rest goes through the
 * class flags.  The three arms are exclusive -- the first two jump past the
 * third -- and the last block runs whichever of them fired.
 */
/* @0x10010a30 */
void TV_THISCALL Track_AdjustVelar(Engine *self)
{
    Node *cur = self->stage_ctx[3].cur;
    int32_t c_cur = cur->value;
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int done = 0;

    if ((g_10058618[cls180((uint8_t)c_cur)] & 0x40) &&
        (g_10058618[cls100((uint8_t)c_ctl)] & 4))
        self->trk_param[10][6] = 0x76c;

    if (c_cur == 'I' && c_ctl == '~') {
        int32_t s = self->stage_ctx[3].scan->value;

        if (s == 'K' || s == 'G') {
            self->trk_param[10][6] = 0x8ca;
            done = 1;
        }
    }
    if (!done && (g_10058618[cls100((uint8_t)c_ctl)] & 0x80)) {
        int ok = (g_10058618[cls80((uint8_t)c_cur)] & 4) != 0;

        if (!ok) {
            int32_t s = self->stage_ctx[3].scan->value;

            ok = (s == 'p' || s == 'm' || s == 't' || s == 'U');
        }
        if (ok) {
            self->trk_param[10][6] = 0x384;
            self->trk_param[11][6] = 0x640;
            if (g_10058618[cls180((uint8_t)cur->value)] & 0x40) {
                self->trk_4e8[10] = 0x4010;
                self->trk_4e8[11] = 0x4010;
            }
            done = 1;
        }
    }
    if (!done && (g_10058618[cls180((uint8_t)c_ctl)] & 8)) {
        uint8_t f = g_10058618[cls100((uint8_t)c_cur)];

        if ((f & 4) && (f & 1))
            self->trk_490[10] = 0x73a;
    }

    if ((g_10058618[cls80((uint8_t)self->stage_ctx[3].ctl->value)] & 4) &&
        (c_cur == 'K' || c_cur == 'G')) {
        self->trk_4e8[10] = 0;
        self->trk_4e8[11] = 0;
    }
}

/*
 * Average each track's old level with its new one, and hold the result
 * within a limit of both.
 *
 * Tracks 9 to 17 are left alone -- they are the ones Track_Contour and
 * Stage3_Segment drive from the tables -- so this covers 0 to 8 and 18 to
 * 21.  trk_param[t][5] and [4] both end up at the midpoint of trk_490[t] and
 * trk_param[t][6], except that on tracks 0 to 9 it is also kept within
 * `lim` of each of them.
 *
 * `lim` is worked out by a chain of overrides rather than a table, and the
 * order matters: 9 to begin with, 15 after a space, 0 on track 2, 10 when
 * the current phoneme is not flagged, and 0 on every track above 2.  So the
 * only tracks where it is not 0 or 10 are 0 and 1.
 */
/* @0x10011660 */
void TV_THISCALL Track_Average(Engine *self)
{
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_cur = self->stage_ctx[3].cur->value;
    int32_t i;
    uint8_t f_ctl, f_cur;

    if (c_ctl == 'D') {
        self->trk_param[13][6] = 0x12c;
        self->trk_param[14][6] = 0xfa;
        self->trk_param[15][6] = 0x190;
    }

    for (i = 0; i < 22; i++) {
        int32_t v, lim;

        if (i >= 9 && i <= 0x11)
            continue;
        v = (self->trk_490[i] + self->trk_param[i][6]) / 2;
        self->trk_param[i][5] = v;

        lim = 9;
        if (self->stage_ctx[3].ctl->value == ' ')
            lim = 0xf;
        if (i == 2)
            lim = 0;
        if (!(g_10058618[cls0((uint8_t)self->stage_ctx[3].cur->value)] & 4))
            lim = 0xa;
        if (i > 2)
            lim = 0;

        if (i <= 9) {
            int32_t t = self->trk_param[i][6] - lim;

            if (t > v)
                self->trk_param[i][5] = t;
            t = self->trk_490[i] - lim;
            if (self->trk_param[i][5] < t)
                self->trk_param[i][5] = t;
        }
        self->trk_param[i][4] = self->trk_param[i][5];
    }

    if (!(g_10058618[cls0((uint8_t)c_cur)] & 4))
        goto tail;

    f_ctl = g_10058618[cls0((uint8_t)c_ctl)];
    if (!((f_ctl & 4) && (f_ctl & 2)))
        self->trk_param[0][4] -= 6;

    if (f_ctl & 2) {
        if ((g_10058618[cls100((uint8_t)c_cur)] & 1) ||
            (g_10058618[cls0((uint8_t)c_cur)] & 0x40))
            self->trk_param[0][4] -= 4;
    }

    f_cur = g_10058618[cls0((uint8_t)c_cur)];
    if ((f_ctl & 4) && (f_ctl & 2) && (f_cur & 4) && !(f_cur & 2))
        self->trk_param[0][5] -= 9;

tail:
    if ((g_10058618[cls0((uint8_t)self->stage_ctx[3].ctl->value)] & 4) &&
        c_cur == 'S')
        self->trk_param[0][0] = 4;
}

/*
 * The phoneme boundary: whether to emit a pause, and what to set after it.
 *
 * Three parts.  The first is a gauntlet of tests that has to be passed
 * before Track_EmitPause runs at all -- the control phoneme must not be
 * flagged either of two ways nor be 'I', the current one must not be a
 * voiceless stop, and 'S' takes its own exit that zeroes two levels instead.
 * Note that an 'S' whose control phoneme is not flagged falls through into
 * the second test rather than skipping, so it is checked twice.
 *
 * The second sets the blend shape on tracks 9 to 16 to 7, or 11 for a 'Y'
 * after a flagged control phoneme, and full blend weight on tracks 9 to 11.
 *
 * The third is a block of eleven levels for 'y', the palatal glide, and a
 * final bump to track 10's shape.
 */
/* @0x10015540 */
void TV_THISCALL Track_AdjustBoundary(Engine *self)
{
    Node *ctl = self->stage_ctx[3].ctl;
    int32_t c_ctl = ctl->value;
    Node *cur;
    int32_t c_cur;
    int32_t i;

    if (!(g_10058618[cls180((uint8_t)c_ctl)] & 0x20) &&
        !(g_10058618[cls100((uint8_t)c_ctl)] & 0x40) &&
        c_ctl != 'I') {
        cur = self->stage_ctx[3].cur;
        c_cur = cur->value;
        if (c_cur != 'P' && c_cur != 'T' && c_cur != 'K' && c_cur != 'C') {
            int done = 0;

            if (c_cur == 'S' && (g_10058618[cls0((uint8_t)c_ctl)] & 4)) {
                self->trk_param[1][6] = 0;
                self->trk_param[2][6] = 0;
                done = 1;
            }
            if (!done && !(g_10058618[cls0((uint8_t)c_cur)] & 4) &&
                c_cur != ' ' && !(cur->flags & 0x40))
                Track_EmitPause(self);
        }
    }

    c_cur = self->stage_ctx[3].cur->value;
    if (c_cur == 'C')
        self->trk_param[1][0] = 4;

    if (g_10058618[cls180((uint8_t)c_cur)] & 1) {
        int32_t v = 7;

        if ((g_10058618[cls180((uint8_t)self->stage_ctx[3].ctl->value)] & 0x20)
            && c_cur == 'Y')
            v = 0xb;
        for (i = 0; i < 8; i++)
            self->trk_param[9 + i][2] = v;
    }

    if (g_10058618[cls180((uint8_t)c_cur)] & 0x40) {
        self->trk_4e8[9] = 0x2cd8;
        self->trk_4e8[10] = 0x2cd8;
        self->trk_4e8[11] = 0x2cd8;
    }

    c_ctl = self->stage_ctx[3].ctl->value;
    if (c_ctl == 'y') {
        self->trk_param[3][6] = 0x37;
        self->trk_param[5][6] = 0x2e;
        self->trk_param[1][6] = 0x19;
        self->trk_param[6][6] = 0x2e;
        self->trk_param[8][6] = 0x37;
        self->trk_param[2][6] = 0x1e;
        self->trk_param[4][6] = 0x34;
        self->trk_param[7][6] = 0x3c;
        self->trk_param[13][6] = 0xb4;
        self->trk_param[14][6] = 0x12c;
        self->trk_param[15][6] = 0x15e;
    }

    if ((g_10058618[cls180((uint8_t)c_ctl)] & 0x20) &&
        (g_10058618[cls180((uint8_t)c_cur)] & 4))
        self->trk_param[10][2] += 5;
}

/*
 * The stop burst.
 *
 * The seven phonemes this dispatches on -- B C D G K P T -- are exactly the
 * stops, and what it does is write a fixed value into a track s3_460 samples
 * back from where that track's cursor now is.  So it is patching the release
 * into audio the earlier stages have already laid down.
 *
 * Which track and what value depend on the pair: 'D' takes its value from
 * the following vowel (A and O give 0x32, I gives 0x35, a tap gives 0x41,
 * anything else 0x37), 'C' writes seven bytes rather than one, 'K' writes
 * nothing at all, and the rest are fixed.  Track 1 and track 8 are the two
 * that get written, sometimes both.
 */
/* @0x100150f0 */
void TV_THISCALL Track_StopBurst(Engine *self)
{
    int32_t c_ctl = self->stage_ctx[3].ctl->value;
    int32_t c_cur = self->stage_ctx[3].cur->value;
    int32_t back = self->s3_460;

    switch ((int8_t)c_cur) {
    case 'B':
        Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x37);
        return;
    case 'C':
        Track_Fill(self->trk_buf[8], self->trk_wr[8] - back - 1, 7, 0x42);
        return;
    case 'D': {
        int32_t v;

        switch ((int8_t)c_ctl) {
        case 'A': case 'O': v = 0x32; break;
        case 'I':           v = 0x35; break;
        case 'r':           v = 0x41; break;
        default:            v = 0x37; break;
        }
        Track_Fill(self->trk_buf[1], self->trk_wr[1] - self->s3_460, 1,
                   (uint8_t)v);
        if (c_ctl == 'U')
            Track_Fill(self->trk_buf[0], self->trk_wr[0] - self->s3_460, 1,
                       0x2e);
        return;
    }
    case 'G':
        if (c_ctl == 'O' || c_ctl == 'U') {
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x32);
            Track_Fill(self->trk_buf[8], self->trk_wr[8] - back, 1, 0x37);
        } else {
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x37);
        }
        return;
    case 'K':
        return;
    case 'P':
        if (c_ctl == 'A') {
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x32);
        } else if (c_ctl == 'r') {
            Track_Fill(self->trk_buf[8], self->trk_wr[8] - back, 1, 0x32);
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x2d);
        } else {
            Track_Fill(self->trk_buf[8], self->trk_wr[8] - back, 1, 0x3c);
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x37);
        }
        return;
    case 'T':
        if (c_ctl == 'r') {
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x3f);
        } else {
            Track_Fill(self->trk_buf[8], self->trk_wr[8] - back, 1, 0x3c);
            Track_Fill(self->trk_buf[1], self->trk_wr[1] - back, 1, 0x37);
        }
        return;
    default:
        Track_Fill(self->trk_buf[8], self->trk_wr[8] - back, 1, 0x37);
        return;
    }
}

/*: swap the three level pairs and complement the three weights.  This is
 * its own inverse, which is why sub_10010630 calls it twice. */
static void adj_flip(Engine *self)
{
    int32_t i;

    for (i = 9; i <= 11; i++) {
        int32_t t = self->trk_490[i];

        self->trk_490[i] = self->trk_param[i][6];
        self->trk_param[i][6] = t;
    }
    for (i = 9; i <= 11; i++)
        self->trk_4e8[i] = 0x7ffe - self->trk_4e8[i];
}

/*
 * Corrections for the transition between two phonemes, in whichever
 * direction it runs.
 *
 * The two phonemes are stage 3's current and control nodes, and which of
 * them counts as the start of the transition depends on s3_44c against
 * s3_448 -- the class of this segment against the class of the last one.
 * When the new class is the higher, the roles are exchanged: the three level
 * pairs are swapped, the three blend weights are complemented, and the
 * control node takes the near role.  The same flip runs again at the end,
 * which puts everything back, so the flip is a way of writing the body once
 * for both directions rather than a lasting change.
 *
 * A space in the current node skips the whole thing, flip included.
 */
/* @0x10010630 */
void TV_THISCALL Track_AdjustTransition(Engine *self)
{
    Node *cur = self->stage_ctx[3].cur;
    Node *pa, *pb;
    int flipped;
    int32_t i;

    if (cur->value == ' ')
        return;

    flipped = self->s3_44c > self->s3_448;
    if (flipped) {
        pa = self->stage_ctx[3].ctl;
        pb = cur;
        adj_flip(self);
    } else {
        pa = cur;
        pb = self->stage_ctx[3].ctl;
    }

    if ((g_10058618[cls180(pa->value)] & 4) &&
        !(g_10058618[cls0(pa->value)] & 2)) {
        self->trk_4e8[10] = 0x19a0;
        self->trk_param[10][2] += 3;
    }

    if ((g_10058618[cls180(pb->value)] & 4) &&
        (g_10058618[cls80(pa->value)] & 8))
        self->trk_param[11][6] += 0x12c;

    if ((pa->value == 'd' || pa->value == 'b' || pa->value == 'g') &&
        (g_10058618[cls100(pb->value)] & 2))
        for (i = 0; i < 5; i++)
            self->trk_param[3 + i][0] = 4;

    if ((pb->value == 'd' || pb->value == 'b' || pb->value == 'g') &&
        pa->value == 'r')
        for (i = 0; i < 5; i++)
            self->trk_param[3 + i][0] = 4;

    if (!(g_10058618[cls100(pb->value)] & 1) &&
        (g_10058618[cls100(pa->value)] & 1)) {
        self->trk_4e8[9] = 0x4010;
        self->trk_4e8[11] = 0;
        self->trk_4e8[10] = 0;

        if (g_10058618[cls0(pa->value)] & 0x10) {
            self->trk_4e8[9] = 0;
            self->trk_param[16][0] = 6;
        }
        if (g_10058618[cls100(pa->value)] & 0x10) {
            self->trk_4e8[10] = 0x19a0;
            self->trk_4e8[11] = 0x59b0;
            if (g_10058618[cls100(pb->value)] & 0x20) {
                self->trk_4e8[10] = 0x5348;
                self->trk_4e8[11] = 0x19a0;
            }
        }
        if (g_10058618[cls100(pa->value)] & 4) {
            self->trk_490[10] = 0x640;
            self->trk_490[11] = 0xa3c;
            if (pa->value == 'N')
                self->trk_490[10] = 0x578;
            if (g_10058618[cls100(pb->value)] & 0x40)
                self->trk_490[10] = 0x41a;
        }
        if (g_10058618[cls100(pa->value)] & 0x80) {
            int32_t f2 = self->trk_param[10][6];
            int32_t f1 = self->trk_param[9][6];
            int32_t lo = f2 + f1 * 2 - 0x258;
            int32_t hi = f2 + f1 * 2 - 0xc8;

            self->trk_490[10] = lo;
            self->trk_490[11] = hi;
            if (g_10058618[cls100(pb->value)] & 0x10) {
                hi += 0x190;
                self->trk_490[11] = hi;
            }
            if (pa->value == '~') {
                self->trk_490[10] = (lo + f2) / 2;
                self->trk_490[11] =
                    (self->trk_param[11][6] + self->trk_490[11]) / 2;
            }
            if (pb->value == 'I' || pb->value == 'a') {
                self->trk_490[10] += 0xfa;
                self->trk_490[11] = self->trk_param[11][6] + 0x32;
            }
        }
    }

    if (!(g_10058618[cls0(pb->value)] & 0x10) &&
        (g_10058618[cls0(pa->value)] & 0x10))
        self->trk_4e8[16] = 0;

    if (self->s3_44c > self->s3_448)
        adj_flip(self);
}
