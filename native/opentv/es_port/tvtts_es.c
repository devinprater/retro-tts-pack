/*
 * The Spanish engine behind tvtts.h: what SAPI used to be, for CGRM_ES.
 *
 * src/port/tvtts.c is the same layer for English and this is its twin, not a
 * generalisation of it.  The two engines are separate decompilations of
 * separate DLLs -- two years apart, different object layouts, nothing shared
 * but tv_common.h -- so a layer that owns an Engine cannot be written once for
 * both.  What is written once is the part that touches no engine at all: the
 * escape-sequence builders, the code-page conversion and the language table all
 * live in src/port/api.c, which is the public surface both of these sit under.
 *
 * Every function here is renamed to es_* at link time (see harness/build.sh),
 * so the names match English's on purpose: the two files stay comparable, and
 * the dispatcher knows which is which from the prefix rather than from the
 * spelling.
 *
 * The driving sequence -- construct, init, feed, flush, step -- is the one the
 * SAPI engine thread ran for one ITTSCentral::TextData call, and harness/tvh.c
 * has been running it against the original since the first Spanish function was
 * written.  It is copied from there rather than from English's, because that is
 * the version proved byte-exact for this engine.
 */
#include <stdlib.h>
#include <string.h>

#include "es_engine.h"
#include "tvtts.h"
#include "tvtts_port.h"

/* Engine_Construct writes up to 0x8838; the struct wins where a wider pointer
 * has made it larger than the original's. */
#define ENGINE_ALLOC (sizeof(Engine) > 0x8838 ? sizeof(Engine) : (size_t)0x8838)
/* What harness/tvh.c gives both engines.  Engine_Step flushes at 0x2ee0 bytes,
 * so this is more than it can ever hold. */
#define OUTBUF_SIZE  0x34bc

/* Per-voice defaults, and the paired ANSI/UTF-16 names.  Ten voices:
 * Pedro, Jorge, Ricardo, Paco, Luis, Ezequiel, Rogelio, Carlos, Josefa and
 * Isabel, in that order -- which is the order the initialisation code at
 * 0x1000830e registers them in, and not the order they sit in memory.  Voice 5
 * is the one that speaks at 120 wpm rather than 150, the slot English gives
 * Grandpa Amos. */
/* @0x1004c828 */
extern const int32_t g_voice_pitch[10];
/* @0x1004c878 */
extern const int32_t g_voice_speed[10];
/* @0x100497cc */
extern const char g_voice_names[];

/* The DLL's ten, plus any of OpenTV's own; see lang/spa/engine/voices.c. */
#define TV_VOICES (ES_STOCK_VOICES + (int)es_extra_voice_count)

struct es_synth {
    SapiCentral host;
    Engine     *eng;
    uint8_t    *outbuf;
    uint32_t    rate;
    int         started;
    int         preformat, textin, nuls;
    /* valid only for the duration of one speak call */
    tvtts_callback cb;
    void          *user;
    uint32_t       pos;
    int            aborted;
    struct { uint32_t mark, pos; } *pending;
    int            npending, cpending;
};

/* ---- the engine's upward calls ------------------------------------------- */

static int emit(struct es_synth *s, int32_t type, const int16_t *smp,
                uint32_t count, uint32_t mark, uint32_t pos)
{
    tvtts_event ev;

    if (s->cb == NULL || s->aborted)
        return s->aborted;
    ev.type = type;
    ev.count = count;
    ev.samples = smp;
    ev.mark = mark;
    ev.sample_pos = pos;
    if (s->cb(&ev, s->user) != 0)
        s->aborted = 1;
    return s->aborted;
}

/*
 * Where a mark's audio will land.  Stage 3 runs ahead of the synthesiser, so a
 * mark fires before its own samples exist; the earliest of the 22 track write
 * positions is where its frames sit, and a frame is a hundredth of a second of
 * output.  The same reasoning as English's, and the same field.
 */
static uint32_t mark_position(const struct es_synth *s)
{
    const Engine *E = s->eng;
    int32_t lo = E->trk_wr[0];
    int i;

    for (i = 1; i < 22; i++)
        if (E->trk_wr[i] < lo)
            lo = E->trk_wr[i];
    if (lo < 0)
        lo = 0;
    return (uint32_t)lo * (s->rate / 100u);
}

static void queue_mark(struct es_synth *s, uint32_t mark, uint32_t pos)
{
    if (s->npending == s->cpending) {
        int cap = s->cpending ? s->cpending * 2 : 8;
        void *p = realloc(s->pending, (size_t)cap * sizeof *s->pending);

        if (p == NULL)
            return;             /* drop it rather than fail the utterance */
        s->pending = p;
        s->cpending = cap;
    }
    s->pending[s->npending].mark = mark;
    s->pending[s->npending].pos = pos;
    s->npending++;
}

/* Hand over `n` samples, releasing any mark the stream passes on the way. */
static void emit_audio(struct es_synth *s, const int16_t *smp, uint32_t n)
{
    uint32_t done = 0;

    while (done < n && !s->aborted) {
        uint32_t take = n - done;
        int i, first = -1;

        for (i = 0; i < s->npending; i++)
            if (s->pending[i].pos <= s->pos + take &&
                (first < 0 || s->pending[i].pos < s->pending[first].pos))
                first = i;
        if (first >= 0) {
            uint32_t at = s->pending[first].pos;
            take = at > s->pos ? at - s->pos : 0;
        }
        if (take != 0) {
            emit(s, TVTTS_AUDIO, smp + done, take, 0, s->pos);
            s->pos += take;
            done += take;
        }
        if (first < 0)
            break;
        emit(s, TVTTS_MARK, NULL, 0, s->pending[first].mark, s->pos);
        s->pending[first] = s->pending[--s->npending];
    }
}

/*
 * Where a bookmark surfaces.  Stage 3 reaches an index-mark node, builds a
 * three-word record and pushes it at the audio queue; the first word says what
 * it is, 0 a bookmark and 1 a phoneme trace.  English's engine hands the whole
 * SapiCentral to its own version of this and can cast its way back; this one is
 * given only the queue field, so tvtts_create puts the synth there.
 */
void TV_THISCALL Queue_Push(void *queue, void *data, int32_t len)
{
    struct es_synth *s = (struct es_synth *)queue;
    uint32_t *rec;

    if (s == NULL || data == NULL || len != (int32_t)sizeof(void *))
        return;
    rec = *(uint32_t *const *)data;
    if (rec == NULL)
        return;
    /* Mark 0 is the engine's own end-of-item marker rather than one of the
     * caller's, and it also tells the node to stop reporting. */
    if (rec[0] == 0 && rec[2] != 0)
        queue_mark(s, rec[2], mark_position(s));
    tv_free(rec);
}

/*
 * Stage 2 can write the phonemes it decided on to a byte list the layer above
 * allocated.  Nothing in this library asks for that yet -- the engine only
 * builds it when Engine.w_212c is set, which nothing here sets -- so this is
 * the terminator that keeps the call resolved.
 */
uint8_t TV_THISCALL ByteList_Add(void *list, int32_t byte)
{
    (void)list;
    (void)byte;
    return 0;
}

/*
 * Which OpenTV extensions are on.  src/port/api.c hands every language the whole
 * mask; the only one this engine understands is the pitch ceiling.
 */
void es_set_extensions(uint32_t mask)
{
    tv_es_ext_pitch = (mask & TVTTS_EXT_PITCH) != 0;
    tv_es_ext_contour = (mask & TVTTS_EXT_CONTOUR) != 0;
    tv_es_ext_floor = (mask & TVTTS_EXT_FLOOR) != 0;
    tv_es_ext_rate = (mask & TVTTS_EXT_RATE) != 0;
}

/* ---- lifetime ------------------------------------------------------------ */

void *es_create(uint32_t sample_rate)
{
    struct es_synth *s;

    if (sample_rate != 11025 && sample_rate != 8000 &&
        sample_rate != TV_SR_HIFI_ES)
        return NULL;
    s = (struct es_synth *)calloc(1, sizeof *s);
    if (s == NULL)
        return NULL;
    s->eng = (Engine *)calloc(1, ENGINE_ALLOC);
    s->outbuf = (uint8_t *)calloc(1, OUTBUF_SIZE);
    if (s->eng == NULL || s->outbuf == NULL) {
        free(s->eng);
        free(s->outbuf);
        free(s);
        return NULL;
    }
    s->rate = sample_rate;
    s->preformat = 1;
    s->textin = 1;
    s->nuls = 2;

    s->host.voice = 0;
    s->host.pitch = (int16_t)g_voice_pitch[0];
    s->host.speed = g_voice_speed[0];
    s->host.volume = 0xffff;
    s->host.ctx = 0;
    /* Queue_Push is handed this field and nothing else, so it is how a
     * bookmark finds its way back to the caller. */
    s->host.audio_queue = s;

    es_set_extensions(tvtts_get_extensions());
    Engine_Construct(s->eng);
    s->eng->w_212e = 1;
    s->eng->w_212c = 0;
    s->eng->s2_trace = (void *)1;
    s->eng->sapi = &s->host;
    s->eng->sample_rate = (uint16_t)sample_rate;
    s->eng->fmt = 1;
    Engine_Init(s->eng);
    s->eng->preformat = 1;
    s->eng->textin_on = 1;
    s->eng->out_buf = s->outbuf;
    s->eng->out_count = 0;
    Engine_CreateTextIn(s->eng);
    return s;
}

void es_destroy(void *p)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s == NULL)
        return;
    free(s->eng);
    free(s->outbuf);
    free(s->pending);
    free(s);
}

/* ---- settings ------------------------------------------------------------ */

void es_set_voice(void *p, int voice)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s != NULL && voice >= 0 && voice < TV_VOICES)
        s->host.voice = voice;
}

void es_set_rate(void *p, int wpm)
{
    struct es_synth *s = (struct es_synth *)p;

    /* Engine_SetSpeed does (wpm - 46) >> 3 unsigned, so below the floor it
     * reads wildly out of the rate table; the original crashes there too. */
    if (s != NULL)
        s->host.speed = wpm < TVTTS_RATE_MIN ? TVTTS_RATE_MIN : wpm;
}

void es_set_pitch(void *p, int pitch)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s != NULL)
        s->host.pitch = (int16_t)pitch;
}

void es_set_volume(void *p, uint32_t volume)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s != NULL)
        s->host.volume = (int32_t)volume;
}

int es_get_voice(const void *p)
{
    const struct es_synth *s = (const struct es_synth *)p;

    return s != NULL ? s->host.voice : -1;
}

int es_get_rate(const void *p)
{
    const struct es_synth *s = (const struct es_synth *)p;

    return s != NULL ? (int)s->host.speed : -1;
}

int es_get_pitch(const void *p)
{
    const struct es_synth *s = (const struct es_synth *)p;

    return s != NULL ? (int)(uint16_t)s->host.pitch : -1;
}

uint32_t es_get_rate_hz(const void *p)
{
    const struct es_synth *s = (const struct es_synth *)p;

    return s != NULL ? s->rate : 0u;
}

int es_set_rate_hz(void *p, uint32_t hz)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s == NULL || (hz != 8000u && hz != 11025u &&
                      hz != (uint32_t)TV_SR_HIFI_ES))
        return -1;
    if (s->cb != NULL)          /* not part way through an utterance */
        return -1;
    if (hz == s->rate)
        return 0;
    s->rate = hz;
    s->eng->sample_rate = (uint16_t)hz;
    Engine_Init(s->eng);
    /* Engine_Init puts the engine's own defaults back, and the cached copies
     * the speak loop compares against still say the host's are applied, so
     * they have to be re-applied here rather than left to the loop. */
    s->eng->cur_voice = (int16_t)s->host.voice;
    Engine_SetVoice(s->eng, (uint32_t)(int32_t)s->host.voice);
    s->eng->cur_pitch = (uint32_t)(uint16_t)s->host.pitch;
    Engine_SetPitch(s->eng, (int32_t)s->eng->cur_pitch);
    s->eng->cur_speed = (uint32_t)s->host.speed;
    Engine_SetSpeed(s->eng, (int32_t)s->eng->cur_speed);
    s->eng->cur_volume = (uint32_t)s->host.volume;
    Engine_SetVolume(s->eng, s->eng->cur_volume);
    s->eng->cur_bac = (uint32_t)s->host.ctx;
    s->started = 0;
    return 0;
}

void es_set_compat(void *p, int preformat, int textin, int terminators)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s == NULL)
        return;
    s->preformat = preformat ? 1 : 0;
    s->textin = textin ? 1 : 0;
    s->nuls = terminators < 0 ? 0 : terminators;
}

/*
 * The tokenizer mode, which the engine reads when it builds its TextIn.  It has
 * already built one by now, so the object is constructed again in place rather
 * than replaced: Engine_CreateTextIn allocates a new one and does not free the
 * old, and before the first utterance there are no tokens in it to lose.
 */
void es_set_textin_mode(void *p, int mode)
{
    struct es_synth *s = (struct es_synth *)p;

    if (s == NULL)
        return;
    s->host.textin_mode = mode;
    if (s->eng->textin != NULL) {
        TextIn_Construct(s->eng->textin, mode);
        s->eng->textin->engine = s->eng;
    }
}

/* ---- the voices ---------------------------------------------------------- */

int es_voice_count(void)
{
    return TV_VOICES;
}

/*
 * Walk the paired ANSI/UTF-16 name table.  The names do not sit in voice
 * order: the compiler emitted the literals for everything after the first in
 * the reverse of the order the engine registers them, so the block reads
 * Pedro, Isabel, Josefa, ... , Jorge while the voices run Pedro, Jorge,
 * Ricardo, ... , Isabel.  The same shape as English's, and checked the same
 * way -- against the sequence of pushes in the initialisation code.
 */
static const char *voice_entry(int voice)
{
    const char *q = g_voice_names;
    int i, pos;

    /* The blob holds the DLL's ten only, and the walk counts backwards through
     * it, so the count it counts with has to be that ten and not however many
     * voices the library offers.  A voice of ours is named by its own
     * definition and never reaches here. */
    if (voice < 0 || voice >= ES_STOCK_VOICES)
        return NULL;
    pos = voice == 0 ? 0 : ES_STOCK_VOICES - voice;
    for (i = 0; i < pos; i++) {
        size_t n = strlen(q) + 1;          /* the ANSI name */
        q += (n + 3) & ~(size_t)3;
        n = 0;
        while (q[n] != 0 || q[n + 1] != 0) /* the UTF-16 copy */
            n += 2;
        q += (n + 2 + 3) & ~(size_t)3;
    }
    return q;
}

const char *es_voice_name(int voice)
{
    /* A voice of this project's own carries its name in its definition; the
     * blob voice_entry walks holds the DLL's ten and nothing else. */
    const char *extra = es_extra_voice_name((int32_t)voice);

    return extra != NULL ? extra : voice_entry(voice);
}

int es_voice_rate(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)es_v_speed(voice) : -1;
}

int es_voice_pitch(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)es_v_pitch(voice) : -1;
}

/* ---- synthesis ----------------------------------------------------------- */

int es_speak_bytes(void *p, const void *text, uint32_t len,
                   tvtts_callback cb, void *user)
{
    struct es_synth *s = (struct es_synth *)p;
    Engine *E;
    char *buf;
    uint32_t textlen, pos = 0;
    int pending = 0, fed_all = 0, r;
    long steps = 0;

    if (s == NULL || (text == NULL && len != 0))
        return -1;
    E = s->eng;

    /* SAPI enqueued the caller's text with a NUL of its own, and SDK callers
     * conventionally counted their own too.  The count matters: Engine_Feed
     * branches on the total length. */
    buf = (char *)malloc(len + (uint32_t)s->nuls + 1);
    if (buf == NULL)
        return -1;
    if (len != 0)
        memcpy(buf, text, len);
    /* OpenTV: `CamelCase` read as the words it is made of; see tv_camel_split.
     * Spanish has no phoneme-input command, so there is nothing to run first. */
    if ((tvtts_get_extensions() & TVTTS_EXT_CAMEL) && len != 0) {
        uint32_t add = (uint32_t)tv_camel_split(buf, len, NULL);

        if (add != 0) {
            char *wide = (char *)malloc(len + add + (uint32_t)s->nuls + 1);

            if (wide != NULL) {
                tv_camel_split(buf, len, wide);
                free(buf);
                buf = wide;
                len += add;
            }
        }
    }
    memset(buf + len, 0, (size_t)s->nuls + 1);
    textlen = len + (uint32_t)s->nuls;

    if (s->started)
        Engine_Reset(E);
    s->started = 1;
    E->preformat = (uint8_t)s->preformat;
    E->textin_on = (uint8_t)s->textin;
    E->out_count = 0;
    s->cb = cb;
    s->user = user;
    s->pos = 0;
    s->aborted = 0;

    for (;;) {
        int flush = -1;

        if (E->st_idle && E->st_input_empty) {
            if (E->item_done) {
                if (fed_all) {
                    if (!pending)
                        break;
                    goto params;
                }
                pos = 0;
                E->item_done = 0;
                /* SAPI put the caller's TextData context here, and an index
                 * mark only reports itself when it is non-zero. */
                E->item_notify = 1;
                Engine_Feed(E, buf, textlen, &pos);
                if (E->item_done)
                    fed_all = 1;
                flush = 1;
            }
        }
        if (flush < 0) {
            if (!E->item_done && Engine_InFree(E) > 0x800) {
                Engine_Feed(E, buf, textlen, &pos);
                if (E->item_done)
                    fed_all = 1;
            } else if (E->st_idle) {
                flush = 0;
            }
        }
        if (flush >= 0) {
            Engine_Flush(E, flush);
            pending = 1;
        }
    params:
        if (E->cur_pitch != (uint32_t)(uint16_t)s->host.pitch) {
            E->cur_pitch = (uint32_t)(uint16_t)s->host.pitch;
            Engine_SetPitch(E, (int32_t)E->cur_pitch);
        }
        if (E->cur_speed != (uint32_t)s->host.speed) {
            E->cur_speed = (uint32_t)s->host.speed;
            Engine_SetSpeed(E, (int32_t)E->cur_speed);
        }
        if (E->cur_volume != (uint32_t)s->host.volume) {
            E->cur_volume = (uint32_t)s->host.volume;
            Engine_SetVolume(E, E->cur_volume);
        }
        if (E->cur_bac != (uint32_t)s->host.ctx)
            E->cur_bac = (uint32_t)s->host.ctx;
        if ((int32_t)E->cur_voice != s->host.voice) {
            E->cur_voice = (int16_t)s->host.voice;
            Engine_SetVoice(E, (uint32_t)(int32_t)E->cur_voice);
        }

        r = Engine_Step(E);
        if (r & 2) {
            uint32_t n = E->out_count / 2;
            E->out_count = 0;
            if (n != 0)
                emit_audio(s, (const int16_t *)s->outbuf, n);
        }
        if (r & 1)
            pending = 0;
        if (s->aborted)
            break;
        if (++steps > 10000000L)     /* the engine has stopped progressing */
            break;
    }

    /* Anything still queued belongs at the end of what was produced. */
    while (!s->aborted && s->npending > 0) {
        int i, first = 0;

        for (i = 1; i < s->npending; i++)
            if (s->pending[i].pos < s->pending[first].pos)
                first = i;
        emit(s, TVTTS_MARK, NULL, 0, s->pending[first].mark, s->pos);
        s->pending[first] = s->pending[--s->npending];
    }
    s->npending = 0;
    if (!s->aborted)
        emit(s, TVTTS_END, NULL, 0, 0, s->pos);
    r = s->aborted;
    s->cb = NULL;
    s->user = NULL;
    free(buf);
    return r;
}
