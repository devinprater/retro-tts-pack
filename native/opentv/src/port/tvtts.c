/*
 * The flat C library: tvtts.h over the decompiled engine.
 *
 * This file is the layer the engine calls "the central object" -- what SAPI
 * used to be.  It owns the engine, holds the settings block the engine reads
 * back, and terminates the two paths the engine uses to talk upwards: the
 * bookmark queue (Sapi_QueuePush, below) and the window notifications (no-ops
 * in src/engine/sapi.c).
 *
 * The driving loop is the one src/port/main.c used to carry, which is the
 * sequence the SAPI engine thread ran for one ITTSCentral::TextData call.
 * It is byte-exact against the original across the whole corpus, so it is
 * copied here rather than rewritten.
 *
 * It owns the English engine, and only that.  The library carries one of these
 * per language -- lang/spa/port/tvtts_es.c is the Spanish one -- and the public names
 * belong to src/port/api.c, which picks between them; what each language has to
 * provide is the en_ and es_ contract in include/tvtts_port.h.  What is left
 * public here is everything that touches no engine and so serves both: the
 * escape sequences a caller can embed in text, the code page conversion, the
 * extension flags and the user lexicon.
 */
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "crt.h"
#include "tvtts.h"
#include "tvtts_port.h"
#include "bytelist.h"

/* Which OpenTV extensions are on; see tvtts_set_extensions.
 * Process-wide, which is why it is not in tvtts_synth. */
/* Which extensions are on is src/port/api.c's, because every language has to
 * hear about a change; this file only applies the ones English understands. */
void en_set_extensions(uint32_t mask)
{
    tv_ext_rate = (mask & TVTTS_EXT_RATE) != 0;
    tv_ext_clarity = (mask & TVTTS_EXT_CLARITY) != 0;
    tv_ext_sing = (mask & TVTTS_EXT_SING) != 0;
    tv_ext_pitch = (mask & TVTTS_EXT_PITCH) != 0;
}

/* The engine object has no allocator of its own -- in the original it lives
 * on a thread stack -- so the caller provides the block.  0x9200 is what the
 * original reserved; where pointers are wider than the original's four bytes
 * the struct outgrows that, and the struct wins. */
#define ENGINE_ALLOC (sizeof(Engine) > 0x9200 ? sizeof(Engine) : (size_t)0x9200)
#define OUTBUF_SIZE  0x34bc

/* @0x100b5350 */ extern const uint32_t g_voice_pitch[10];
/* @0x100b53a0 */ extern const uint32_t g_voice_speed[10];
/* The speaker names, each an ANSI string followed by the same name in
 * UTF-16, both padded to four bytes -- the table the SAPI mode-info block
 * was filled from.  See docs/VOICES.md. */
/* @0x100bf638 */ extern const char g_voice_names[];

extern uint8_t UserLex_Add(const char *word, const char *phonemes);

/* The ten the DLL carries, plus whatever src/engine/voices.c adds. */
#define TV_VOICES (TV_STOCK_VOICES + (int)tv_extra_voice_count)

struct en_synth {
    /* First, so Sapi_QueuePush can get back here from Engine.sapi. */
    SapiCentral host;
    /*
     * OpenTV: what the *caller* asked for, which is not what `host` holds.
     * `host` is the block the engine reports into -- Engine.sapi points at it
     * -- and an `ESC[<n>p` in the text writes its new pitch straight back
     * there (see control.c, `case 'p'`).  That is right for a host being told
     * what is playing and wrong for a record of what was configured: a score
     * sets the pitch for every note, so the setting the caller made is gone by
     * the end of the song.  These four are only ever written from the setters.
     */
    struct {
        int      voice;
        int16_t  pitch;
        int32_t  speed;
        int32_t  volume;
        uint32_t ctx;
    } cfg;
    Engine     *eng;
    uint8_t    *outbuf;
    uint32_t    rate;
    int         started;         /* an utterance has run on this engine */
    int         preformat, textin, nuls;
    /* valid only for the duration of one tvtts_speak call */
    tvtts_callback cb;
    void          *user;
    uint32_t       pos;          /* samples emitted so far this utterance */
    int            aborted;
    /* Marks waiting for the audio to reach them; see queue_mark. */
    struct { uint32_t mark, pos; } *pending;
    int            npending, cpending;
};

/* Sapi_QueuePush recovers the synth by casting; make that true. */
typedef char tvtts_host_first[offsetof(struct en_synth, host) == 0 ? 1 : -1];

/* ---- the engine's upward calls ------------------------------------------ */

static int emit(struct en_synth *s, int32_t type, const int16_t *smp,
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
 * Where a mark's audio will land.
 *
 * A mark fires when stage 3 reaches it, and stage 3 runs ahead of the
 * synthesizer: it has already written this mark's frames into the parameter
 * tracks, but the synthesizer has not turned them into samples yet.  So the
 * event arrives before its own audio, by however much is queued -- twelve
 * frames in practice, about 120 ms.  Reporting the raw output position would
 * make a screen reader move its caret that far early.
 *
 * The earliest of the 22 track write positions is where this mark's frames
 * sit, and a frame is a hundredth of a second of output, so that position
 * scaled by the frame size is where the mark belongs in the stream.  Checked
 * against a mark placed before the first word: it lands within one frame of
 * where speech actually starts.
 */
static uint32_t mark_position(const struct en_synth *s)
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

/*
 * A mark fires ahead of its own audio, so it waits here until the stream
 * reaches it.  Delivering it early would make every caller keep this list
 * itself and split its audio buffer; delivering it in order means a caller
 * can feed what it is given and act on a mark when it sees one.
 */
static void queue_mark(struct en_synth *s, uint32_t mark, uint32_t pos)
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
static void emit_audio(struct en_synth *s, const int16_t *smp, uint32_t n)
{
    uint32_t done = 0;

    while (done < n && !s->aborted) {
        uint32_t take = n - done;
        int i, first = -1;

        /* the earliest mark that falls inside what is left */
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
 * three-word record and hands it to the layer above; the record's first word
 * says what it is (0 a bookmark, 1 a phoneme trace) and the queue then owns
 * it.  The original put it on the same queue as the audio so the SAPI layer
 * could raise it in step with playback; here it goes straight to the caller,
 * which has already been handed every sample before this point.
 */
int32_t Sapi_QueuePush(SapiCentral *ctl, const void *data, uint32_t size)
{
    struct en_synth *s = (struct en_synth *)ctl;
    uint32_t *rec;

    if (size != sizeof(void *) || data == NULL)
        return 0;
    rec = *(uint32_t *const *)data;
    if (rec == NULL)
        return 0;
    /* rec[0] is 0 for a bookmark and 1 for a phoneme trace.  Mark 0 is the
     * engine's own end-of-item marker rather than one of the caller's, and
     * it also tells the node to stop reporting, so it is not passed on. */
    if (rec[0] == 0 && rec[2] != 0)
        queue_mark(s, rec[2], mark_position(s));
    tv_delete(rec);
    return 0;
}

/* ---- text ---------------------------------------------------------------- */

/* The 27 places cp1252 differs from Latin-1.  Anything with no cp1252 byte
 * becomes '?', which is what WideCharToMultiByte substituted. */
static int cp1252_from_unicode(uint32_t u)
{
    static const uint16_t high[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    int i;

    if (u < 0x80 || (u >= 0xa0 && u <= 0xff))
        return (int)u;
    for (i = 0; i < 32; i++)
        if (high[i] == u)
            return 0x80 + i;
    return '?';
}

static char *to_cp1252_utf16(const uint16_t *w, uint32_t *out_len)
{
    uint32_t len = 0, n = 0, i;
    char *b;

    while (w[len] != 0)
        len++;
    b = (char *)malloc(len + 1);
    if (b == NULL)
        return NULL;
    for (i = 0; i < len; i++) {
        uint32_t u = w[i];

        /* A surrogate pair is a character beyond U+FFFF, which cp1252 has
         * no byte for; consume both halves and substitute once. */
        if (u >= 0xd800 && u <= 0xdbff && i + 1 < len &&
            w[i + 1] >= 0xdc00 && w[i + 1] <= 0xdfff) {
            i++;
            b[n++] = '?';
            continue;
        }
        b[n++] = (char)cp1252_from_unicode(u);
    }
    b[n] = 0;
    *out_len = n;
    return b;
}

static char *to_cp1252_utf8(const char *u8, uint32_t *out_len)
{
    size_t cap = strlen(u8) + 1;
    char *b = (char *)malloc(cap);
    const unsigned char *p = (const unsigned char *)u8;
    uint32_t n = 0;

    if (b == NULL)
        return NULL;
    while (*p) {
        uint32_t u = *p;
        int extra = 0;

        if (u >= 0xf0)      { u &= 0x07; extra = 3; }
        else if (u >= 0xe0) { u &= 0x0f; extra = 2; }
        else if (u >= 0xc0) { u &= 0x1f; extra = 1; }
        else if (u >= 0x80) { u = '?';   extra = 0; }  /* stray continuation */
        p++;
        while (extra-- > 0 && (*p & 0xc0) == 0x80)
            u = (u << 6) | (*p++ & 0x3f);
        b[n++] = (char)cp1252_from_unicode(u);
    }
    b[n] = 0;
    *out_len = n;
    return b;
}

static int esc_seq(char *buf, size_t cap, uint32_t n, char letter);

int TVTTS_CALL tvtts_mark_sequence(char *buf, size_t cap, uint32_t mark)
{
    return esc_seq(buf, cap, mark, 'i');
}

static int esc_seq(char *buf, size_t cap, uint32_t n, char letter)
{
    char digits[12];
    int nd = 0, i = 0;

    if (buf == NULL || cap < 4)
        return 0;
    do {
        digits[nd++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n != 0);
    if (cap < (size_t)nd + 4)
        return 0;
    buf[i++] = 0x1b;
    buf[i++] = '[';
    while (nd > 0)
        buf[i++] = digits[--nd];
    buf[i++] = letter;
    buf[i] = 0;
    return i;
}

int TVTTS_CALL tvtts_break_sequence(char *buf, size_t cap, uint32_t ms)
{
    /* Measured: the engine inserts (argument - 49) frames of a hundredth of
     * a second each, so 49 is silence and 255 is its longest pause. */
    uint32_t n = 49u + ms / 10u;

    if (n > 255u)
        n = 255u;
    return esc_seq(buf, cap, n, 's');
}

int TVTTS_CALL tvtts_punctuation_sequence(char *buf, size_t cap, int on)
{
    /* Flag 2 of the ESC[..N/F set.  Measured from the phoneme stream: with
     * it on, "Hi, there." gains the words "comma" and "period"; no word is
     * ever spelled.  Turning it off again gives byte-identical audio to
     * never having set it, but nothing else does -- it outlives the
     * utterance that set it. */
    return esc_seq(buf, cap, 2, on ? 'N' : 'F');
}

int TVTTS_CALL tvtts_pitch_sequence(char *buf, size_t cap, int pitch)
{
    /* ESC[<n>p sets the pitch to 2n, checked against tvtts_set_pitch for
     * several values; the command itself takes 25..200. */
    int n = pitch / 2;

    if (n < 25)
        n = 25;
    if (n > 200)
        n = 200;
    return esc_seq(buf, cap, (uint32_t)n, 'p');
}

int TVTTS_CALL tvtts_rate_sequence(char *buf, size_t cap, int wpm)
{
    /* Both ends clamped here: unlike the setter this builds text for a
     * caller to speak, so there is no fidelity case for letting it ask
     * the engine for a rate that is not speech.  The ceiling follows
     * whether the added rate rows are on, since without them anything
     * past 253 is the original's off-the-end reading. */
    int hi = (tvtts_get_extensions() & TVTTS_EXT_RATE) ? TVTTS_RATE_MAX_EXT
                                                       : TVTTS_RATE_MAX;

    if (wpm < TVTTS_RATE_MIN)
        wpm = TVTTS_RATE_MIN;
    if (wpm > hi)
        wpm = hi;
    return esc_seq(buf, cap, (uint32_t)wpm, 'r');
}

/* ---- lifetime ------------------------------------------------------------ */

void *en_create(uint32_t sample_rate)
{
    struct en_synth *s;

    if (sample_rate != 11025 && sample_rate != 8000 && sample_rate != TV_SR_HIFI)
        return NULL;
    s = (struct en_synth *)calloc(1, sizeof *s);
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
    s->host.pitch = (int16_t)(uint16_t)g_voice_pitch[0];
    s->host.speed = (int32_t)g_voice_speed[0];
    s->host.volume = 0xffff;
    s->host.ctx = 0;
    s->cfg.voice = s->host.voice;
    s->cfg.pitch = s->host.pitch;
    s->cfg.speed = s->host.speed;
    s->cfg.volume = s->host.volume;
    s->cfg.ctx = s->host.ctx;

    en_set_extensions(tvtts_get_extensions());
    Engine_Construct(s->eng);
    s->eng->w_212e = 1;
    s->eng->w_212c = 0;
    s->eng->w_2130 = 1;
    s->eng->sapi = &s->host;
    s->eng->sample_rate = (uint16_t)sample_rate;
    s->eng->fmt_2104 = 1;
    Engine_Init(s->eng);
    s->eng->preformat = 1;
    s->eng->textin_on = 1;
    s->eng->out_buf = s->outbuf;
    s->eng->out_count = 0;
    Engine_CreateTextIn(s->eng);
    return s;
}

void en_destroy(void *vs)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s == NULL)
        return;
    free(s->eng);
    free(s->outbuf);
    free(s->pending);
    free(s);
}

/* ---- settings ------------------------------------------------------------ */

void en_set_voice(void *vs, int voice)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s != NULL && voice >= 0 && voice < TV_VOICES) {
        s->host.voice = voice;
        s->cfg.voice = voice;
    }
}

void en_set_rate(void *vs, int wpm)
{
    struct en_synth *s = (struct en_synth *)vs;

    /* Engine_SetSpeed does (wpm - 46) >> 3 unsigned, so anything below
     * TVTTS_RATE_MIN wraps to a vast index and reads wildly out of the
     * rate table.  The original crashes there too, so there is no
     * behaviour to be faithful to and the floor costs nothing.  The
     * ceiling is deliberately not enforced: past 253 the engine reads
     * off the end of the table, which is nonsense but is the original's
     * nonsense, and the corpus checks it at 260 and 400. */
    if (s != NULL) {
        s->host.speed = wpm < TVTTS_RATE_MIN ? TVTTS_RATE_MIN : wpm;
        s->cfg.speed = s->host.speed;
    }
}

void en_set_pitch(void *vs, int pitch)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s != NULL) {
        s->host.pitch = (int16_t)pitch;
        s->cfg.pitch = s->host.pitch;
    }
}

void en_set_volume(void *vs, uint32_t volume)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s != NULL) {
        s->host.volume = (int32_t)volume;
        s->cfg.volume = s->host.volume;
    }
}

int en_get_voice(const void *vs)
{
    const struct en_synth *s = (const struct en_synth *)vs;

    return s != NULL ? s->cfg.voice : -1;
}

int en_get_rate(const void *vs)
{
    const struct en_synth *s = (const struct en_synth *)vs;

    return s != NULL ? (int)s->cfg.speed : -1;
}

int en_get_pitch(const void *vs)
{
    const struct en_synth *s = (const struct en_synth *)vs;

    return s != NULL ? (int)(uint16_t)s->cfg.pitch : -1;
}

/* ---- output rate ---------------------------------------------------------- */

/* Which rates exist is api.c's business; which of them this engine can be built
 * for is this one's.  All three, the 16 kHz being OpenTV's own addition. */
uint32_t en_get_rate_hz(const void *vs)
{
    const struct en_synth *s = (const struct en_synth *)vs;

    return s != NULL ? s->rate : 0u;
}

int en_set_rate_hz(void *vs, uint32_t hz)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s == NULL || (hz != 8000u && hz != 11025u &&
                      hz != (uint32_t)TV_SR_HIFI))
        return -1;
    /* The filters and the output stage are rebuilt for the new rate, which
     * cannot be done to an utterance already part way through. */
    if (s->cb != NULL)
        return -1;
    if (hz == s->rate)
        return 0;
    s->rate = hz;
    s->eng->sample_rate = (uint16_t)hz;
    Engine_Init(s->eng);

    /* Engine_Init puts the engine's own defaults back -- voice 0, pitch 85,
     * 150 wpm, full volume -- but the cached copies the speak loop compares
     * against still say the host's settings are applied, so it would leave
     * them reset.  Wanda came back as Peter.  Re-apply them here and make
     * the cache agree, rather than clearing the cache and hoping: a host
     * volume that happens to equal the cleared value would be missed.
     *
     * textin, preformat, textin_on and out_buf all survive Engine_Init, so
     * they are deliberately not redone -- Engine_CreateTextIn would leak the
     * text-in object this one still owns. */
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

void en_set_compat(void *vs, int preformat, int textin, int terminators)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s == NULL)
        return;
    s->preformat = preformat ? 1 : 0;
    s->textin = textin ? 1 : 0;
    s->nuls = terminators < 0 ? 0 : terminators;
}

/*
 * The tokenizer mode.  English's Engine_CreateTextIn builds its TextIn for mode
 * 0 and never asks the host block, so this reaches the object and constructs it
 * again in place: allocating another would leak the one already there, and
 * before the first utterance it holds no tokens to lose.
 */
void en_set_textin_mode(void *vs, int mode)
{
    struct en_synth *s = (struct en_synth *)vs;

    if (s == NULL || s->eng->textin == NULL)
        return;
    TextIn_Construct(s->eng->textin, mode);
    s->eng->textin->engine = s->eng;
}

int en_voice_count(void)
{
    return TV_VOICES;
}

/*
 * Walk the paired ANSI/UTF-16 name table; see docs/VOICES.md.
 *
 * The names do not sit in voice order.  The engine registers them with the
 * layer above in the order Peter, Sidney, Eager Eddie, ... , Julia, and the
 * compiler emitted the literals for everything after the first in the
 * reverse of that, so the block reads Peter, Julia, Wanda, ... , Sidney.
 * Checked against the pointer sequence in the initialisation code, where
 * each voice's two names are pushed in turn, and it holds in all five of
 * the language DLLs.
 */
static const char *voice_entry(int voice)
{
    const char *p = g_voice_names;
    int i, pos;

    /* The blob holds the DLL's ten only; a voice of ours is named by
     * its own definition and never reaches here. */
    if (voice < 0 || voice >= TV_STOCK_VOICES)
        return NULL;
    pos = voice == 0 ? 0 : TV_STOCK_VOICES - voice;
    for (i = 0; i < pos; i++) {
        size_t n = strlen(p) + 1;          /* the ANSI name */
        p += (n + 3) & ~(size_t)3;
        n = 0;
        while (p[n] != 0 || p[n + 1] != 0) /* the UTF-16 copy */
            n += 2;
        p += (n + 2 + 3) & ~(size_t)3;
    }
    return p;
}

const char *en_voice_name(int voice)
{
    /* A voice of this project's own carries its name in its definition; the
     * blob voice_entry walks holds the DLL's ten and nothing else. */
    const char *extra = tv_extra_voice_name((int32_t)voice);

    return extra != NULL ? extra : voice_entry(voice);
}

int en_voice_rate(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)tv_v_speed(voice) : -1;
}

int en_voice_pitch(int voice)
{
    return (voice >= 0 && voice < TV_VOICES) ? (int)tv_v_pitch(voice) : -1;
}

int TVTTS_CALL tvtts_add_lexicon(const char *word, const char *phonemes)
{
    if (word == NULL || phonemes == NULL)
        return -1;
    return UserLex_Add(word, phonemes) ? 0 : -1;
}

/* ---- synthesis ----------------------------------------------------------- */

int en_speak_bytes(void *vs, const void *text, uint32_t len,
                   tvtts_callback cb, void *user)
{
    struct en_synth *s = (struct en_synth *)vs;
    Engine *E;
    char *buf;
    uint32_t textlen, pos = 0;
    int pending = 0, fed_all = 0, r;
    long steps = 0;

    if (s == NULL || (text == NULL && len != 0))
        return -1;
    E = s->eng;

    /* SAPI enqueued the caller's text with a NUL of its own, and SDK callers
     * conventionally counted their own too.  The count matters: the feed
     * routine branches on the total length. */
    buf = (char *)malloc(len + (uint32_t)s->nuls + 1);
    if (buf == NULL)
        return -1;
    if (len != 0)
        memcpy(buf, text, len);
    /*
     * `[:phone TruVoice on|off]` is a command in the text rather than an
     * escape, so it is turned into one here, before the engine sees any of
     * it.  Gated, and the rewrite only shortens, so the buffer above still
     * holds it.
     */
    if (tv_ext_sing && len != 0)
        len = (uint32_t)tv_phone_commands(buf, len);
    /*
     * And `CamelCase` read as the words it is made of, which the engine has no
     * notion of.  After the command rewrite, never before: before, it splits
     * the command's own name and then the phoneme text the command introduces.
     */
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

    if (s->started) {
        Engine_Reset(E);
        /*
         * OpenTV: and give the caller's settings back.
         *
         * `ESC[<n>p` moves the engine's pitch and reports itself into `host`,
         * so by the end of an utterance both the engine and this layer's idea
         * of the pitch are the song's rather than the caller's.  Engine_Reset
         * does not touch either.  Measured: "hello there" after one 300 ms C4
         * came out at 355 Hz against 105 on a synth that had not sung, and
         * stayed there for every utterance after it.
         *
         * So `host` is put back from `cfg` -- which only the setters write --
         * and the pitch is pushed into the engine, the two places it lives.
         * The block further down then finds speed, volume and voice changed
         * and re-sends those for the same reason.
         */
        s->host.voice = s->cfg.voice;
        s->host.pitch = s->cfg.pitch;
        s->host.speed = s->cfg.speed;
        s->host.volume = s->cfg.volume;
        s->host.ctx = s->cfg.ctx;
        E->cur_pitch = (uint32_t)(uint16_t)s->cfg.pitch;
        Engine_SetPitch(E, (int32_t)E->cur_pitch);
    }
    /*
     * OpenTV: start every utterance with the singing state clear.
     *
     * tv_sing_dur and tv_sing_f0q are globals -- they have to be, since they
     * carry a value from one stage of the pipeline to the next -- so
     * Engine_Reset cannot reach them.  A score ends with `ESC[0;0q` to hand the
     * pitch back, and that is not enough on its own: the pitch is applied at
     * stage 4, which only advances as the synthesiser consumes frames, so an
     * escape sitting after the last phoneme of a score has nothing left to
     * carry it and never runs.  The forced period then survives into whatever
     * the synth says next.
     *
     * The utterance boundary is this layer's to know, so this is where they are
     * cleared.
     */
    {
        int k;

        for (k = 0; k < 5; k++) {
            tv_sing_dur[k] = 0;
            tv_sing_f0q[k] = 0;
        }
        tv_sing_per = 0;
        tv_sing_f0_fx = 0;
        tv_sing_f0_tgt = 0;
        tv_sing_f0_step = 0;
        tv_sing_vib_ph = 0;
    }

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
                 * mark only reports itself when it is non-zero; the library
                 * has no such context, so a constant stands in for it. */
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
        if (++steps > 10000000L)     /* the engine has stopped making progress */
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

/* ---- parameter frames ---------------------------------------------------- */

/*
 * Drive the synthesiser directly: 22 parameter tracks per 10 ms frame, with
 * stages 0 to 3 bypassed entirely.
 *
 * docs/VOICES.md documents the escape pair `ESC[<track>;<value>l` and
 * `ESC[<n>g` as a way to place the resonators by hand, and it is fine for a
 * handful of frames.  It is not a way to drive a voice: the escapes travel as
 * control nodes through a pipeline sized for speech, and past about ten of them
 * the queue stops delivering what was asked -- a run of bare holds after a
 * change sequence *replays* that sequence rather than holding its last frame.
 * Rendering a word as one utterance per segment and splicing the audio works,
 * but every splice is an utterance boundary: the glottal phase restarts, and
 * measured across one 0.8 s word the joins stepped up to 46% of local peak.
 *
 * So this writes the frames where stage 3 would have written them and lets the
 * synthesiser run straight through, which is what makes a continuous signal
 * possible at all.  The track layout is the one in docs/VOICES.md; the ring is
 * 256 frames and the filling here is `Stage3_Hold`'s loop with the caller's
 * frames in place of a held one.
 *
 * English only, as the phoneme entry points are: the Spanish engine is a
 * separate object with its own tracks.
 */
int en_speak_frames(void *vs, const uint8_t *frames, uint32_t n_frames,
                    tvtts_callback cb, void *user)
{
    struct en_synth *s = (struct en_synth *)vs;
    Engine *E;
    uint32_t f = 0;
    long steps = 0;
    int i;

    if (s == NULL || (frames == NULL && n_frames != 0))
        return -1;
    E = s->eng;

    if (s->started)
        Engine_Reset(E);
    s->started = 1;
    E->out_count = 0;
    s->cb = cb;
    s->user = user;
    s->pos = 0;
    s->aborted = 0;

    /*
     * No stop marker while frames are still arriving: `Synth_Step` sets
     * `synth_busy` the moment the reader reaches `trk_08`, and -1 is the value
     * the engine itself uses for "not set" (see `Tracks_Op`'s rebase, which
     * skips it).  `s3_1fe0` is the lookahead `Synth_Frame` insists on having
     * ahead of the reader; 2 is what stage 3 uses for a short transition.
     */
    E->trk_08 = -1;
    E->s3_1fe0 = 2;
    E->synth_busy = 0;
    E->synth_hold = 0;

    while (!s->aborted) {
        /* Fill while the 256-frame ring has room. */
        while (f < n_frames && Tracks_Op(E, 1, 1)) {
            int32_t pos = E->trk_0c;

            for (i = 0; i < 22; i++)
                E->trk_buf[i][pos & 0xff] = frames[f * 22 + (uint32_t)i];
            pos++;
            for (i = 0; i < 22; i++) {
                E->trk_rd[i] = pos - E->s3_1fe0;
                E->trk_wr[i] = pos;
            }
            E->trk_0c = pos;
            E->trk_10 = pos;
            f++;
        }
        if (f >= n_frames) {
            /* Everything is in; let the reader run out and stop cleanly. */
            if (E->trk_08 == -1)
                E->trk_08 = E->trk_0c - 2;
            if (E->trk_04 >= E->trk_08)
                break;
        }
        Synth_Step(E);
        if (E->out_count != 0) {
            uint32_t n = E->out_count / 2;
            E->out_count = 0;
            if (n != 0)
                emit_audio(s, (const int16_t *)s->outbuf, n);
        }
        if (++steps > 10000000L)         /* no progress; do not spin forever */
            break;
    }
    if (E->out_count != 0) {
        uint32_t n = E->out_count / 2;
        E->out_count = 0;
        if (n != 0)
            emit_audio(s, (const int16_t *)s->outbuf, n);
    }
    if (!s->aborted)
        emit(s, TVTTS_END, NULL, 0, 0, s->pos);
    s->cb = NULL;
    s->user = NULL;
    return s->aborted;
}

/* ---- phonemes ------------------------------------------------------------ */

/* The audio a phoneme conversion produces is not wanted, only the trace. */
static int TVTTS_CALL discard_audio(const tvtts_event *ev, void *user)
{
    (void)ev;
    (void)user;
    return 0;
}

int en_speak_phonemes(void *vs, const char *phonemes,
                      tvtts_callback cb, void *user)
{
    struct en_synth *s = (struct en_synth *)vs;
    /* tts_SpeakPhoneme in the 5.1 builds does exactly this: bracket the
     * caller's string with the two escapes and hand it to tts_Speak. */
    char open[8], close[8], *buf;
    size_t n, no, nc;
    int r;

    if (s == NULL || phonemes == NULL)
        return -1;
    no = (size_t)esc_seq(open, sizeof open, 1, 'I');
    nc = (size_t)esc_seq(close, sizeof close, 0, 'I');
    n = strlen(phonemes);
    buf = (char *)malloc(no + n + nc + 1);
    if (buf == NULL)
        return -1;
    memcpy(buf, open, no);
    memcpy(buf + no, phonemes, n);
    memcpy(buf + no + n, close, nc);
    buf[no + n + nc] = 0;
    r = en_speak_bytes(s, buf, (uint32_t)(no + n + nc), cb, user);
    free(buf);
    return r;
}

int en_text_to_phonemes(void *vs, const char *text,
                        char *buf, uint32_t cap)
{
    struct en_synth *s = (struct en_synth *)vs;
    tv_bytelist list;
    uint32_t need;
    int r;

    if (s == NULL || text == NULL || (buf == NULL && cap != 0))
        return -1;

    /* Stage 2 reports each phoneme through ByteList_Append while w_212c is
     * set; it guards nothing else, so the audio is unaffected. */
    memset(&list, 0, sizeof list);
    s->eng->s2_bytes = &list;
    s->eng->w_212c = 1;
    r = en_speak_bytes(s, text, (uint32_t)strlen(text), discard_audio, NULL);
    s->eng->w_212c = 0;
    s->eng->s2_bytes = NULL;

    if (r < 0 || list.failed) {
        tv_bytelist_free(&list);
        return -1;
    }
    need = list.len + 1;
    if (buf != NULL && cap != 0) {
        uint32_t n = need > cap ? cap - 1 : list.len;

        if (n != 0)
            memcpy(buf, list.buf, n);
        buf[n] = 0;
    }
    tv_bytelist_free(&list);
    return (int)need;
}

/*
 * UTF-16 to UTF-8, for a language that reads its own script.
 *
 * Narrowing to cp1252 is right for the two engines whose phoneme tables are
 * built on it, and it is destruction for any other alphabet -- every kana
 * becomes a question mark, so a Japanese synth handed text through the UTF-16
 * entry point would have spoken a row of question marks.  Which conversion to
 * do is the language's business, and the table in api.c says.
 */
static char *to_utf8_utf16(const uint16_t *w, uint32_t *out_len)
{
    uint32_t len = 0, n = 0, i;
    char *b;

    while (w[len] != 0)
        len++;
    b = (char *)malloc((size_t)len * 4 + 1);  /* 4 bytes is the most one takes */
    if (b == NULL)
        return NULL;
    for (i = 0; i < len; i++) {
        uint32_t u = w[i];

        if (u >= 0xd800 && u <= 0xdbff && i + 1 < len &&
            w[i + 1] >= 0xdc00 && w[i + 1] <= 0xdfff) {
            u = 0x10000u + ((u - 0xd800u) << 10) + (w[i + 1] - 0xdc00u);
            i++;
        }
        if (u < 0x80) {
            b[n++] = (char)u;
        } else if (u < 0x800) {
            b[n++] = (char)(0xc0 | (u >> 6));
            b[n++] = (char)(0x80 | (u & 0x3f));
        } else if (u < 0x10000) {
            b[n++] = (char)(0xe0 | (u >> 12));
            b[n++] = (char)(0x80 | ((u >> 6) & 0x3f));
            b[n++] = (char)(0x80 | (u & 0x3f));
        } else {
            b[n++] = (char)(0xf0 | (u >> 18));
            b[n++] = (char)(0x80 | ((u >> 12) & 0x3f));
            b[n++] = (char)(0x80 | ((u >> 6) & 0x3f));
            b[n++] = (char)(0x80 | (u & 0x3f));
        }
    }
    b[n] = 0;
    *out_len = n;
    return b;
}

int TVTTS_CALL tvtts_speak_utf8(tvtts_synth *s, const char *text,
                                tvtts_callback cb, void *user)
{
    uint32_t len;
    char *b;
    int r;

    if (s == NULL || text == NULL)
        return -1;
    /* Already UTF-8, and this language wants it that way: nothing to do. */
    if (tv_lang_utf8(s))
        return tvtts_speak_bytes(s, text, (uint32_t)strlen(text), cb, user);
    b = to_cp1252_utf8(text, &len);
    if (b == NULL)
        return -1;
    r = tvtts_speak_bytes(s, b, len, cb, user);
    free(b);
    return r;
}

int TVTTS_CALL tvtts_speak_utf16(tvtts_synth *s, const uint16_t *text,
                                 tvtts_callback cb, void *user)
{
    uint32_t len;
    char *b;
    int r;

    if (s == NULL || text == NULL)
        return -1;
    b = tv_lang_utf8(s) ? to_utf8_utf16(text, &len)
                        : to_cp1252_utf16(text, &len);
    if (b == NULL)
        return -1;
    r = tvtts_speak_bytes(s, b, len, cb, user);
    free(b);
    return r;
}
