/*
 * Executing a control node.
 *
 * The escape parser turned "ESC [ ... <letter>" into a control node carrying
 * the letter and its arguments; this is where that node takes effect.  It
 * runs once per stage the node passes through, and most commands only do
 * something at one of them -- a pitch change lands in every stage's window
 * but only stage 2 tells the host about it, a voice change only stage 3, an
 * index mark only stage 4.  That is how a parameter change reaches the
 * synthesiser at the point in the audio where it was written rather than
 * when it was parsed.
 *
 * Which stage is running is worked out by subtracting the engine pointer
 * from the window pointer: stage_ctx[0] is at 0x754 and they are 0x44 apart,
 * so 0x754, 0x798, 0x7dc, 0x820 and 0x864 are stages 0 to 4.
 */
#if defined(TV_HOOK_BUILD)
#include <windows.h>
#endif
#include "es_engine.h"

/* Per-voice defaults, three tables of ten int32 laid end to end. */
/* @0x1004c828 */
extern const int32_t g_voice_pitch[10];
/* @0x1004c850 */
extern const int32_t g_voice_rate_index[10];
/* @0x1004c878 */
extern const int32_t g_voice_speed[10];

/* Per-voice mode description handed to the host on a voice change.  Ten of
 * them, which is what the size below is; it is in .bss and filled at load
 * time, so the standalone build only needs the space. */
#define MODE_INFO_SIZE 2800
/* @0x10037dd0 */
extern const uint8_t g_mode_info[28000];   /* 10 * MODE_INFO_SIZE */

/*
 * Every notification is a PostMessageA to the window the host registered.
 * English's decompilation does the same; under the harness the handle is null,
 * so the call is a no-op whichever copy of PostMessageA it reaches.
 *
 * The standalone library has no window and registers none -- it hands a caller
 * its events through the callback instead -- so there the whole path is nothing,
 * as src/engine/sapi.c makes it for English.  Keeping the call and relying on a
 * null handle would drag user32 into a library that otherwise needs only the
 * kernel.
 */
#if defined(TV_HOOK_BUILD)
static void Sapi_Post(SapiCentral *s, uint32_t msg, uint32_t wp, uint32_t lp)
{
    PostMessageA((HWND)s->hwnd, msg, (WPARAM)wp, (LPARAM)lp);
}
#else
static void Sapi_Post(SapiCentral *s, uint32_t msg, uint32_t wp, uint32_t lp)
{
    (void)s;
    (void)msg;
    (void)wp;
    (void)lp;
}
#endif

/* pow(10.0, arg * -0.1) * 65535.0, truncated.
 *
 * The original computes this in x87 floating point every time and then
 * throws away everything below the integer, so the result is a step
 * function with a few dozen steps.  A libm that rounds the last bit
 * differently would move a boundary by one and change the audio, which
 * would look like a decompilation bug; the steps are tabulated instead.
 * The same reasoning and the same 256 values as src/engine/volume.c,
 * which is unsurprising -- it is the same expression -- and unit_es
 * checks every one of them against the engine itself. */
static const uint16_t g_vol_of_atten[256] = {
    65535, 52056, 41349, 32845, 26089, 20723, 16461, 13075,
    10386,  8250,  6553,  5205,  4134,  3284,  2608,  2072,
     1646,  1307,  1038,   825,   655,   520,   413,   328,
      260,   207,   164,   130,   103,    82,    65,    52,
       41,    32,    26,    20,    16,    13,    10,     8,
        6,     5,     4,     3,     2,     2,     1,     1,
        1,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
        0,     0,     0,     0,     0,     0,     0,     0,
};

static uint32_t Volume_FromAtten(int32_t arg)
{
    return g_vol_of_atten[arg & 0xff];
}

/* @0x1000ead0 */
uint8_t TV_THISCALL Engine_RunControl(Engine *self)
{
    StageCtx *st = self->stage;
    Node *n = st->ctl;
    int32_t stage = (int32_t)(st - self->stage_ctx);
    SapiCentral *sapi = self->sapi;
    uint32_t flags;
    int32_t arg, v;
    uint8_t b15;

    if (stage == 2 && NODE_TYPE(n) == 5)
        return 0;
    flags = n->flags;
    if (flags & NODE_TYPE_MASK)
        return 1;

    arg = (int32_t)n->arg;
    if (n->value != 'i')
        arg &= 0xff;
    b15 = n->b15;

    switch (n->value) {
    case 'A':
        st->p_38 = ((int32_t)b15 << 8) | arg;
        break;
    case 'C':
        if (stage == 0)
            Engine_NodeAlloc(self, n, 0, 3, ']');
        break;
    case 'I':
        st->p_1c = arg;
        break;
    case 'N':
        st->p_34 = ((int32_t)b15 << 8) | arg;
        break;
    case 'P':
        st->p_20 = ((int32_t)b15 << 8) + arg;
        break;
    case 'V':
        st->voice = arg;
        self->stage->pitch = es_v_pitch(self->stage->voice);
        self->stage->rate_index = es_v_rate_index(self->stage->voice);
        if (stage != 3)
            break;
        self->cur_voice = (int16_t)arg;
        self->cur_speed = (uint32_t)es_v_speed((int16_t)arg);
        self->cur_pitch = (uint32_t)self->stage->pitch;
        if (sapi == NULL)
            break;
        sapi->voice = (int16_t)arg;
        memcpy(sapi->mode_info, g_mode_info + (int32_t)self->cur_voice * MODE_INFO_SIZE,
               MODE_INFO_SIZE);
        sapi->pitch = (int16_t)self->cur_pitch;
        sapi->speed = (int32_t)self->cur_speed;
        break;
    case 'a':
        st->volume_atten = arg;
        if (stage != 3)
            break;
        if (arg == 0x10) {
            self->cur_volume = 0;
            self->stage->volume_atten = 100;
        } else {
            self->cur_volume = Volume_FromAtten(arg);
            self->mute = 0;
        }
        if (sapi == NULL)
            break;
        sapi->volume = (int32_t)self->cur_volume;
        Sapi_Post(sapi, 0x46a, 3, 0);
        break;
    case 'c':
        if (stage != 2 || sapi == NULL)
            break;
        self->cur_bac = arg;
        sapi->ctx = arg;
        break;
    case 'f':
        if (st->p_30 == 0)
            self->e_1ae4 = st->rate_index;
        st->p_30 = arg;
        self->stage->rate_index = b15;
        if (self->stage->p_30 != 0)
            break;
        self->stage->rate_index = self->e_1ae4;
        break;
    case 'g':
    case 's':
    case 't':
        /* three separate handlers in the original, identical to the byte */
        if (stage != 2)
            break;
        n->flags = (flags & ~2u) | 5;
        break;
    case 'i':
        if (stage != 4)
            break;
        if (sapi != NULL) {
            if (arg == 0 && n->notify != 0)
                Sapi_Post(sapi, 0x4c9, 0, n->notify);
            if (n->notify != 0) {
                uint32_t *rec = (uint32_t *)tv_new(8);
                if (rec != NULL) {
                    /* English queues three words and clears n->notify when
                     * arg is zero; this queues two and leaves it alone */
                    rec[0] = n->notify;
                    rec[1] = (uint32_t)arg;
                    Queue_Push(sapi->audio_queue, &rec, sizeof rec);
                    Sapi_Post(sapi, 0x4c8, 0, 0);
                }
            }
        }
        if (n->flags & 0x20) {
            self->reset_pending = 1;
            self->reset_value = arg;
        }
        break;
    case 'l':
        /* the preformatter has already clamped the index below 22 */
        self->s3_param_raw[arg] = b15;
        break;
    case 'p':
        arg += arg;
        st->pitch = arg;
        if (stage != 2)
            break;
        self->cur_pitch = (uint32_t)self->stage->pitch;
        if (sapi == NULL)
            break;
        sapi->pitch = (int16_t)self->cur_pitch;
        Sapi_Post(sapi, 0x46a, 1, 0);
        break;
    case 'r':
    case 'v':
        st->rate_index = arg;
        if (stage != 2)
            break;
        v = arg * 8 + 50;
        self->cur_speed = (uint32_t)v;
        v = (int32_t)(((uint32_t)v / 10) * 10);
        self->cur_speed = (uint32_t)v;
        if (sapi == NULL)
            break;
        sapi->speed = v;
        Sapi_Post(sapi, 0x46a, 2, 0);
        break;
    case 'x':
        if (stage == 4) {
            self->reset_value = 0;
            self->reset_pending = 1;
        } else if (stage == 0) {
            Engine_NodeAlloc(self, n, 1, 0, 'C');
        }
        break;
    default:
        break;
    }
    return 1;
}
