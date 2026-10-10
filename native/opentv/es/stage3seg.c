/*
 * One phoneme segment of stage 3.
 *
 * If the control node holds a vowel, Segment_Apply does the whole job from
 * the tables and this returns.  Everything after that point is the
 * consonant path: a run of corrections keyed on the phoneme class flags,
 * and then -- only when the flags say so -- a pass over tracks 9, 10 and 11
 * that emits each one and puts its parameters back the way it found them.
 *
 * That save-and-restore is the shape worth noticing.  The loop rewrites
 * seven trk_param cells, trk_wr and trk_rd for the track, calls Track_Emit
 * so the writers see those values, and then restores all nine from locals,
 * leaving only trk_param[t][0] changed (to 3).  So the parameters are
 * arguments to Track_Emit rather than state being advanced.
 *
 * g_10058618 is indexed four ways here, with nothing, 0x80, 0x100 or 0x180
 * set in the phoneme value; so the table is four blocks of 128 flags and
 * this is the function that uses all four.
 */
#include "es_engine.h"

/* @0x10058618 */
extern const uint8_t g_10058618[0x200];
/* two byte tables reached through pointer variables, both indexed by the
 * phoneme.  TV_REF(uint8_t, g_10057d50)'s entry is a divisor, and it is zero for several
 * letters, so the original divides by zero if it ever gets one of them. */
/* @0x10057d50 */
extern const tv_ref g_10057d50;
/* @0x10057ce8 */
extern const tv_ref g_10057ce8;
/* indexed by the second table's entry */
/* @0x100580c0 */
extern const uint8_t g_100580c0[256];

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

/* @0x10015720 */
void TV_THISCALL Stage3_Segment(Engine *self)
{
    Node *ctl = self->stage_ctx[3].ctl;
    int32_t c_ctl = ctl->value;
    int32_t vi;
    uint8_t flags;
    int32_t track;

    self->s3_650 = 0;
    vi = Vowel_Index2((uint8_t)c_ctl);
    if (vi != -1) {
        if (g_10058618[cls100(self->stage_ctx[3].cur->value)] & 2)
            self->trans_len = 0x7f;
        self->s3_650 = Segment_Apply(self, vi);
        if (self->s3_650 != 0) {
            self->trans_len = 0x7f;
            return;
        }
    }

    /* ---- the consonant path */
    {
        int32_t c_scan = self->stage_ctx[3].scan->value;
        int32_t c_cur = self->stage_ctx[3].cur->value;

        if (c_scan == 'L') {
            if (g_10058618[cls100((uint8_t)c_ctl)] & 0x20)
                self->trk_param[10][6] -= 0x64;
            if (g_10058618[cls180((uint8_t)c_ctl)] & 0x40)
                self->trk_540[1] -= 0x64;
        }

        if (c_ctl == 'U' && c_cur == 'K')
            self->trk_param[10][6] = 0x3b6;

        if (c_cur == 'L' && (g_10058618[cls100((uint8_t)c_ctl)] & 0x20))
            self->trk_param[10][6] -= 0x64;

        if ((g_10058618[cls0((uint8_t)c_scan)] & 0x20) &&
            (g_10058618[cls80((uint8_t)c_scan)] & 0x10) &&
            (c_ctl == 'k' || c_ctl == 'w'))
            self->trk_540[1] += 0xc8;

        if ((g_10058618[cls180((uint8_t)c_cur)] & 8) &&
            (g_10058618[cls180((uint8_t)c_ctl)] & 0x20))
            self->trk_param[10][2] += 5;

        flags = g_10058618[cls180((uint8_t)c_ctl)];

        if ((flags & 0x40) && (g_10058618[cls100((uint8_t)c_scan)] & 4)) {
            int32_t v = self->trk_540[1] - 0x96;

            self->trk_540[1] = v;
            if (v < self->trk_param[10][6])
                self->trk_540[1] = self->trk_param[10][6];
        }

        if (flags & 8)
            self->trk_param[10][2] -= 2;
        if (!(flags & 0x10))
            return;
    }

    /* ---- the three tracks */
    {
        const uint8_t *div = TV_REF(uint8_t, g_10057d50);
        const uint8_t *sel = TV_REF(uint8_t, g_10057ce8);
        int32_t ph = cls0((uint8_t)c_ctl);
        int32_t v;

        v = (self->trk_param[9][3] * self->s3_468) / div[ph];
        self->s3_468 = (v + self->s3_468) >> 1;
        self->s3_46c = ((int32_t)g_100580c0[sel[ph]] *
                        self->trk_param[9][3]) / div[ph];

        for (track = 9; track <= 11; track++) {
            int32_t k = track - 9;
            int32_t s_rd = self->trk_rd[track];
            int32_t s_wr = self->trk_wr[track];
            int32_t s1 = self->trk_param[track][1];
            int32_t s2 = self->trk_param[track][2];
            int32_t s3 = self->trk_param[track][3];
            int32_t s4 = self->trk_param[track][4];
            int32_t s5 = self->trk_param[track][5];
            int32_t s6 = self->trk_param[track][6];
            int32_t mid;

            self->trk_param[track][0] = 7;
            mid = (self->trk_540[k] + s6) / 2;

            if (c_ctl == 'I' || c_ctl == 'y') {
                if (track == 11) {
                    mid += -200;
                    if (c_ctl == 'y')
                        mid += -200;
                } else if (track == 10) {
                    mid += 0xc8;
                } else if (c_ctl == 'y') {
                    mid += 0x46;
                }
            } else if (c_ctl == 'U') {
                if (track == 11)
                    self->s3_46c -= 7;
            } else if (c_ctl == '4' || c_ctl == 'k') {
                if (track == 11)
                    self->s3_46c -= 3;
            }

            if (self->s3_46c > 0) {
                if (self->trk_param[track][3] <= self->s3_46c) {
                    self->trk_param[track][0] = 3;
                    mid = self->trk_540[k];
                    self->s3_46c = self->trk_param[track][3];
                }
                /* a track outside 9..11 would reach Track_Fill with an
                 * uninitialised value; the loop never leaves that range */
                if (track == 9)
                    v = s6 >> 2;
                else if (track == 10)
                    v = (s6 - 0x1f4) >> 3;
                else
                    v = s6 >> 4;
                Track_Fill(self->trk_buf[track], self->trk_wr[track],
                           self->s3_46c, (uint8_t)v);
                self->trk_param[track][3] -= self->s3_46c;
                self->trk_wr[track] += self->s3_46c;
            } else {
                mid = s6;
            }

            self->trk_rd[track] = s_wr;
            self->trk_param[track][6] = self->trk_540[k];
            self->trk_param[track][4] = mid;
            self->trk_param[track][5] = mid;
            v = self->s3_468;
            if (v >= self->s3_1fe8)
                v = self->s3_1fe8;
            self->trk_param[track][2] = v;
            self->trk_param[track][1] = v;

            Track_Emit(self, track);

            /* put everything back; only column 0 is left changed */
            self->trk_param[track][6] = s6;
            self->trk_param[track][3] = s3;
            self->trk_wr[track] = s_wr;
            self->trk_rd[track] = s_rd;
            self->trk_param[track][0] = 3;
            self->trk_param[track][2] = s2;
            self->trk_param[track][1] = s1;
            self->trk_param[track][5] = s5;
            self->trk_param[track][4] = s4;
        }
    }
}
