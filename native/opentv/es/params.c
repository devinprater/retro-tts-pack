/*
 * The four parameter setters the SAPI layer calls, and the two diagnostics
 * that were compiled out of the shipping build.
 *
 * Engine_SetPitch, Engine_SetSpeed and Engine_SetVoice are the English
 * engine's, instruction for instruction: store the value, then copy it into
 * all five stage contexts, because a stage reads its own context and never
 * the engine.  Engine_SetSpeed's rate index is the same (wpm - 46) >> 3,
 * including the same two faults -- it runs off the end of a 26-row table
 * above row 25 and wraps unsigned below 46 wpm.  TVTTS_EXT_RATE, which
 * clamps those for English, is deliberately not wired up here: the
 * extension is only half a fix without the duration scaling that lives in
 * stage 2, and stage 2's duration rules are not decompiled for Spanish yet.
 *
 * Engine_SetVolume is the one that differs from English.  English scans a
 * table of boundaries; this computes the decibels in x87 and truncates:
 *
 *     fldlg2 ; fild vol ; fmul 1/65535 ; fyl2x ; fmul -10.0 ; _ftol
 *
 * which is log10(vol * (1/65535)) * -10.0, truncated toward zero, then
 * capped at 15.  That is the same step function src/engine/volume.c
 * tabulated for English, reached a different way, so the boundaries are
 * reused here rather than recomputed.  Reusing them is a measurement and
 * not an assumption: unit_es sweeps this against CGRM_ES.DLL over every
 * volume the original accepts, and the two agree throughout.
 *
 * Tabulating rather than calling log10 is the decision src/engine/volume.c
 * explains at length.  What reaches the audio is a step function with a few
 * dozen steps, so a libm that rounds the last bit differently -- another
 * platform, another compiler, or SSE2 in place of the x87 the original used,
 * where fyl2x carries 80 bits of intermediate precision -- would move one
 * boundary by one volume and look exactly like a decompilation bug.
 */
#include "es_engine.h"

/* log10(vol * (1/65535)) * -10.0, truncated, capped at 15.
 *
 * The function is non-increasing in vol -- log10 rises, the negative scale
 * falls, and truncation preserves the order -- so each attenuation owns one
 * contiguous run of volumes and only the ends of the runs need storing.
 * g_atten_bound[k - ES_ATTEN_MIN] is the largest volume whose attenuation is
 * still at least k, and the answer is the largest such k.  Starting the walk
 * at ES_ATTEN_MAX is what caps the result at 15, which is the original's
 * explicit "cmp eax, 0xf".
 *
 * Volumes above 65535 give a negative attenuation, which the original allows:
 * _ftol rounds toward zero rather than down, and nothing clamps the low end.
 */
#define ES_ATTEN_MIN (-48)
#define ES_ATTEN_MAX 15

static const uint32_t g_atten_bound[64] = {
    0xffffffffu, 0xf676c58du, 0xc3c5f567u, 0x9b821c0eu, 0x7b864b48u, 0x621e7b24u,
    0x4df05196u, 0x3de8b0a9u, 0x312d0fe0u, 0x270fd8efu, 0x1f072940u, 0x18a57a27u,
    0x1393cbbdu, 0x0f8d02ceu, 0x0c5a3abau, 0x09cfd91du, 0x07cb3b5bu, 0x0630de77u,
    0x04eae7fcu, 0x03e7fc17u, 0x031a50ecu, 0x0276f29du, 0x01f52df9u, 0x018e19e1u,
    0x013c3912u, 0x00fb2f4fu, 0x00c785efu, 0x009e7ca5u, 0x007de3ffu, 0x0063ff9bu,
    0x004f6e7eu, 0x003f1842u, 0x00321e32u, 0x0027cf63u, 0x001f9f4eu, 0x00191e54u,
    0x0013f3cbu, 0x000fd943u, 0x000c96ccu, 0x0009fff5u, 0x0007f173u, 0x00064f39u,
    0x00050305u, 0x0003fb23u, 0x00032987u, 0x00028308u, 0x0001fec7u, 0x000195b9u,
    0x00014247u, 0x0000cb58u, 0x0000a185u, 0x0000804du, 0x000065e9u, 0x000050f3u,
    0x0000404du, 0x00003313u, 0x00002892u, 0x0000203au, 0x00001999u, 0x00001455u,
    0x00001026u, 0x00000cd4u, 0x00000a30u, 0x00000818u
};

static int32_t Volume_ToAtten(uint32_t vol)
{
    int32_t k = ES_ATTEN_MAX;

    while (k > ES_ATTEN_MIN && vol > g_atten_bound[k - ES_ATTEN_MIN])
        k--;
    return k;
}

/* @0x10008a40 */
void TV_THISCALL Engine_SetPitch(Engine *self, int32_t pitch)
{
    int i;

    self->pitch = pitch;
    for (i = 0; i < 5; i++)
        self->stage_ctx[i].pitch = pitch;
}

/* @0x10008a70 */
void TV_THISCALL Engine_SetSpeed(Engine *self, int32_t wpm)
{
    int32_t idx = (int32_t)((uint32_t)(wpm - 46) >> 3);
    int i;

    /* OpenTV: the original lets this run off the end of every table it feeds,
     * and wraps unsigned below 46 into a wild one.  Held inside the rows that
     * exist with the extension on; left alone with it off, so the corpus still
     * checks both faults.  See es_rate_row in stage2.c. */
    if (tv_es_ext_rate) {
        if (wpm < 46)
            idx = 0;
        else if (idx > ES_RATE_ROW_MAX_PUBLIC)
            idx = ES_RATE_ROW_MAX_PUBLIC;
    }
    self->speed_wpm = wpm;
    self->rate_index = idx;
    for (i = 0; i < 5; i++)
        self->stage_ctx[i].rate_index = idx;
}

/* Below 0x50 the output is muted instead of attenuated, and the attenuation
 * fields are left holding whatever the last audible volume put there. */
/* @0x10008aa0 */
void TV_THISCALL Engine_SetVolume(Engine *self, uint32_t vol)
{
    int32_t att;
    int i;

    if (vol < 0x50) {
        self->mute = 1;
        return;
    }
    self->mute = 0;
    att = Volume_ToAtten(vol);
    self->volume_atten = att;
    for (i = 0; i < 5; i++)
        self->stage_ctx[i].volume_atten = att;
}

/* @0x10008b10 */
void TV_THISCALL Engine_SetVoice(Engine *self, uint32_t voice)
{
    int i;

    /*
     * OpenTV: the stock ten, plus any of our own.  The original bound is 10 and
     * it fails *silently* -- a voice past it leaves the engine on whichever one
     * it was already using, so a custom voice that is not let through here
     * simply sounds like its predecessor and nothing says why.
     */
    if (voice >= (uint32_t)(ES_STOCK_VOICES + es_extra_voice_count))
        return;
    self->voice = (int32_t)voice;
    for (i = 0; i < 5; i++)
        self->stage_ctx[i].voice = (int32_t)voice;
}

/*
 * The engine's two diagnostics, both compiled down to a bare return in the
 * shipping build.  They are worth writing out rather than leaving bound to
 * the DLL, because the port has to link without them and because a stage
 * that calls one must not be left with a hole.
 *
 * Two call sites still carry their format strings, and they are the only
 * place in the image where a name from the original source survives:
 * sub_1000af20 reports "ERROR: Arith.c Extend():   TCon=%d", which gives
 * that function its name and its file, and sub_1000f090 traces
 * "ParL[P_F0]= %d", which says the parameter list is indexed by symbolic
 * names and that P_F0 is one of them.
 *
 * Engine_Trace's first argument is whatever object the caller is tracing --
 * the engine in both sub_1000f090 and Extend -- so it is typed as a bare
 * pointer rather than guessed at.
 */
/* @0x10008b40 */
void TV_CDECL Engine_Trace(void *obj, const char *fmt, ...)
{
    (void)obj;
    (void)fmt;
}

/* @0x10008b50 */
void TV_THISCALL Engine_Error(Engine *self, int32_t code)
{
    (void)self;
    (void)code;
}
