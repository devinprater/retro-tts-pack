/*
 * Stage 4 and the track writers.
 *
 * Stage 4 is the last stage and the one with nowhere to pass work to: it
 * walks its window executing control nodes and counting off the phonemes the
 * synthesiser has already consumed, and whatever it finishes goes back to the
 * node pool.  It is also the only stage the host drives directly --
 * Engine_Step calls it before and after the synthesis loop.
 *
 * The three track helpers write the 22 parameter tracks the synthesiser
 * reads.  Each track is a 256-byte ring, which is why every write is masked
 * with 0xff.  Track_Fill writes a constant; the two blends walk a shape
 * curve, easing the track from where it is towards a target, forwards from a
 * position or backwards from one.  A shape is a run of weights terminated by
 * a zero, so the curve decides its own length and the count is only a cap.
 */
#include "es_engine.h"

/* One curve per shape number: bytes of Q15 weight, scaled by 128 on read and
 * terminated by a zero. */
/* @0x10058368 */
extern const tv_ref g_track_shape[];

/* @0x10004810 */
void TV_THISCALL Stage4_Reset(Engine *self)
{
    self->stage_ctx[4].type_mask = 0x3f; /* every live type */
}

/* @0x10004820 */
int32_t TV_THISCALL Stage4_Run(Engine *self)
{
    StageCtx *st = &self->stage_ctx[4];
    int32_t ret = 2;

    if (!Engine_StageBegin(self, st)) {
        Engine_StageEnd(self);
        return 2;
    }
    /* English stops before the last node of the window; this one processes
     * it and leaves on the null Engine_StageNext returns afterwards. */
    while (st->ctl != NULL) {
        uint32_t type = NODE_TYPE(st->ctl);
        if (type == NODE_CONTROL) {
            if (!Engine_RunControl(self)) {
                ret = 1;
                break;
            }
        } else if (type == 4) {
            if (self->trk_34 == 0) {
                ret = 0;
                break;
            }
            self->trk_34--;
        }
        st->ctl = Engine_StageNext(self, st->ctl);
    }
    st->scan = st->ctl;
    st->cur = st->ctl;
    Engine_StageEnd(self);
    return ret;
}

/* @0x10017d90 */
void TV_STDCALL Track_Fill(uint8_t *buf, int32_t pos, int32_t n, uint8_t value)
{
    int32_t i;

    for (i = n; i != 0; i--) {
        buf[pos & 0xff] = value;
        pos++;
    }
}

/* @0x1000b060 */
void TV_THISCALL Track_BlendFwd(Engine *self, uint8_t *buf, int32_t pos,
                                int32_t shape, int32_t n, uint8_t target)
{
    const uint8_t *s = TV_REF(uint8_t, g_track_shape[shape]);
    int32_t w = (int32_t)*s++ << 7;
    int32_t left = n;
    uint8_t *p;
    (void)self;

    while (w != 0) {
        if (left == 0)
            return;
        left--;
        p = buf + (pos & 0xff);
        pos++;
        *p = (uint8_t)(*p + Synth_MulQ15((int32_t)target - (int32_t)*p, w));
        w = (int32_t)*s++ << 7;
    }
}

/* @0x1000afe0 */
void TV_THISCALL Track_BlendBack(Engine *self, uint8_t *buf, int32_t pos,
                                 int32_t shape, int32_t n, uint8_t target)
{
    const uint8_t *s = TV_REF(uint8_t, g_track_shape[shape]) + 1;
    int32_t w = (int32_t)*s << 7;
    int32_t left = n;
    uint8_t *p;
    (void)self;

    while (w != 0) {
        if (left == 0)
            return;
        left--;
        pos--;
        p = buf + (pos & 0xff);
        s++;
        *p = (uint8_t)(*p + Synth_MulQ15((int32_t)target - (int32_t)*p, w));
        w = (int32_t)*s << 7;
    }
}
