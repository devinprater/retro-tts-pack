/*
 * The public surface: tvtts.h over however many engines the library carries.
 *
 * TruVoice was five language DLLs, and the 1995 four are a different generation
 * from the 1997 English one -- different object layout, a third the code, their
 * own tables.  So each language OpenTV has decompiled is its own engine with its
 * own layer that owns one, and this file is the only place that knows there is
 * more than one of them.  Everything below the table is a switch on which
 * language a synth speaks.
 *
 * Voices are numbered across the languages -- English 0..9, then Spanish 10..19
 * -- so a caller that does not care about languages can ask for a voice list and
 * get all of them, with tvtts_voice_language to say what each one speaks.  That
 * also means a voice number keeps its meaning when a language is added: a new
 * one goes on the end.
 *
 * Adding a language is a row in the table below, a file like
 * lang/spa/port/tvtts_es.c, and nothing else.
 *
 * Japanese is the third, and the first that is not a decompilation.  No
 * Japanese TruVoice exists, so there was nothing to be byte-exact against and
 * nothing to decompile; its front end is built from the phonetics literature
 * and it drives the 1997 synthesiser through en_speak_frames, which is the
 * layer below stage 3 and knows nothing about any language.  From here it is a
 * row like the other two, which is the point of this file.
 */
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"
#include "tvtts_port.h"

/*
 * What each language calls itself, for a person to read.  The variety comes from
 * the LANGID in each DLL's own version resource -- 0x0409 is English (United
 * States) and 0x040a is Spanish (Spain) -- and not from a guess about the
 * voices; Centigram's own strings say only "English TruVoice" and "Spanish
 * TruVoice", which does not distinguish a variety.
 */
#define EN_LANG                                                               \
    { "en", "American English", en_create, en_destroy,                    \
      en_set_voice, en_set_rate, en_set_pitch, en_set_volume,                 \
      en_get_voice, en_get_rate, en_get_pitch,                                \
      en_get_rate_hz, en_set_rate_hz, en_set_compat, en_set_textin_mode,      \
      en_voice_count, en_voice_name, en_voice_rate, en_voice_pitch,           \
      en_speak_bytes, en_set_extensions, 0 }

#define ES_LANG                                                               \
    { "es", "Castilian Spanish", es_create, es_destroy,                   \
      es_set_voice, es_set_rate, es_set_pitch, es_set_volume,                 \
      es_get_voice, es_get_rate, es_get_pitch,                                \
      es_get_rate_hz, es_set_rate_hz, es_set_compat, es_set_textin_mode,      \
      es_voice_count, es_voice_name, es_voice_rate, es_voice_pitch,           \
      es_speak_bytes, es_set_extensions, 0 }

/*
 * Japanese reads its own script, so it takes UTF-8 where the two cp1252
 * engines take bytes -- the 1 in the last column.  Everything else about the
 * row is the same shape.
 */
#define JA_LANG                                                               \
    { "ja", "Japanese", ja_create, ja_destroy,                                \
      ja_set_voice, ja_set_rate, ja_set_pitch, ja_set_volume,                 \
      ja_get_voice, ja_get_rate, ja_get_pitch,                                \
      ja_get_rate_hz, ja_set_rate_hz, ja_set_compat, ja_set_textin_mode,      \
      ja_voice_count, ja_voice_name, ja_voice_rate, ja_voice_pitch,           \
      ja_speak_bytes, ja_set_extensions, 1 }

/* In voice order: English first, because it was first and because renumbering
 * anybody's saved voice would be unkind.  A new language goes on the END for
 * the same reason -- Japanese is voices 20..29. */
static const tvtts_lang g_langs[] = { EN_LANG, ES_LANG, JA_LANG };

#define NLANGS ((int)(sizeof g_langs / sizeof g_langs[0]))

/*
 * The public synth: which language, and that language's own object.  The two
 * engines' synths have nothing in common but their purpose, so this holds one
 * as a void * and only its own language ever looks inside it.
 */
struct tvtts_synth {
    const tvtts_lang *lang;
    void             *impl;
    /* kept here so a language change can carry them over */
    uint32_t          rate_hz;
    int               preformat, textin, nuls, compat_set;
};

/* Which encoding a synth reads its text as.  tvtts_speak_utf8 and _utf16 live
 * in the English file, which cannot see inside a synth, and the answer belongs
 * to the language rather than to either of them. */
int tv_lang_utf8(const tvtts_synth *s)
{
    return (s != NULL && s->lang != NULL) ? s->lang->utf8_text : 0;
}

/* ---- extensions ---------------------------------------------------------- */

/*
 * Which OpenTV extensions are on, for every engine in the library.  Process-wide
 * rather than per-synth, like tvtts_add_lexicon: the engines keep their stage 2
 * working state in globals, so one synth was never independent of another here.
 *
 * Each language is handed the whole mask and picks out what means anything to
 * it.  A flag can be one language's alone -- TVTTS_EXT_PITCH does nothing to
 * English, which already clamps where it moves Spanish to -- and which is which
 * is the language's business rather than this file's.
 */
static uint32_t g_ext = TVTTS_EXT_DEFAULT;

void TVTTS_CALL tvtts_set_extensions(uint32_t mask)
{
    int i;

    g_ext = mask & TVTTS_EXT_ALL;
    for (i = 0; i < NLANGS; i++)
        g_langs[i].set_extensions(g_ext);
}

void TVTTS_CALL tvtts_set_portamento(int ms)
{
    extern int32_t tv_sing_glide_ms;

    if (ms < 0)
        ms = 0;
    else if (ms > 1000)
        ms = 1000;
    tv_sing_glide_ms = ms;
}

int TVTTS_CALL tvtts_get_portamento(void)
{
    extern int32_t tv_sing_glide_ms;

    return (int)tv_sing_glide_ms;
}

/*
 * The waver a sung note carries.  English is the only language that sings, so
 * this reaches into its engine rather than going through the language table.
 */
void TVTTS_CALL tvtts_set_vibrato(int rate_chz, int depth_chz)
{
    extern int32_t tv_sing_vib_rate;
    extern int32_t tv_sing_vib_depth;

    if (rate_chz < 1)
        rate_chz = 1;
    else if (rate_chz > 5000)
        rate_chz = 5000;
    if (depth_chz < 0)
        depth_chz = 0;
    else if (depth_chz > 5000)
        depth_chz = 5000;
    tv_sing_vib_rate = rate_chz;
    tv_sing_vib_depth = depth_chz;
}

void TVTTS_CALL tvtts_get_vibrato(int *rate_chz, int *depth_chz)
{
    extern int32_t tv_sing_vib_rate;
    extern int32_t tv_sing_vib_depth;

    if (rate_chz != NULL)
        *rate_chz = (int)tv_sing_vib_rate;
    if (depth_chz != NULL)
        *depth_chz = (int)tv_sing_vib_depth;
}

uint32_t TVTTS_CALL tvtts_get_extensions(void)
{
    return g_ext;
}

/* ---- languages and voices ------------------------------------------------ */

int TVTTS_CALL tvtts_language_count(void)
{
    return NLANGS;
}

const char *TVTTS_CALL tvtts_language(int index)
{
    return (index >= 0 && index < NLANGS) ? g_langs[index].code : NULL;
}

/* The language a voice belongs to, and its index within that language. */
static const tvtts_lang *lang_of_voice(int voice, int *local)
{
    int i;

    if (voice < 0)
        return NULL;
    for (i = 0; i < NLANGS; i++) {
        if (voice < g_langs[i].voice_count()) {
            if (local != NULL)
                *local = voice;
            return &g_langs[i];
        }
        voice -= g_langs[i].voice_count();
    }
    return NULL;
}

/* Where a language's voices start in the numbering. */
static int lang_base(const tvtts_lang *l)
{
    int i, base = 0;

    for (i = 0; i < NLANGS; i++) {
        if (&g_langs[i] == l)
            return base;
        base += g_langs[i].voice_count();
    }
    return 0;
}

static const tvtts_lang *lang_by_code(const char *code)
{
    int i;

    if (code == NULL)
        return NULL;
    for (i = 0; i < NLANGS; i++)
        if (strcmp(g_langs[i].code, code) == 0)
            return &g_langs[i];
    return NULL;
}

const char *TVTTS_CALL tvtts_language_name(const char *code)
{
    const tvtts_lang *l = lang_by_code(code);

    return l != NULL ? l->name : NULL;
}

const char *TVTTS_CALL tvtts_voice_language(int voice)
{
    const tvtts_lang *l = lang_of_voice(voice, NULL);

    return l != NULL ? l->code : NULL;
}

int TVTTS_CALL tvtts_voice_count(void)
{
    int i, n = 0;

    for (i = 0; i < NLANGS; i++)
        n += g_langs[i].voice_count();
    return n;
}

const char *TVTTS_CALL tvtts_voice_name(int voice)
{
    int local = 0;
    const tvtts_lang *l = lang_of_voice(voice, &local);

    return l != NULL ? l->voice_name(local) : NULL;
}

int TVTTS_CALL tvtts_voice_rate(int voice)
{
    int local = 0;
    const tvtts_lang *l = lang_of_voice(voice, &local);

    return l != NULL ? l->voice_rate(local) : -1;
}

int TVTTS_CALL tvtts_voice_pitch(int voice)
{
    int local = 0;
    const tvtts_lang *l = lang_of_voice(voice, &local);

    return l != NULL ? l->voice_pitch(local) : -1;
}

/* ---- lifetime ------------------------------------------------------------ */

tvtts_synth *TVTTS_CALL tvtts_create_lang(uint32_t sample_rate, const char *lang)
{
    const tvtts_lang *l = lang_by_code(lang);
    tvtts_synth *s;

    if (l == NULL)
        return NULL;
    s = (tvtts_synth *)calloc(1, sizeof *s);
    if (s == NULL)
        return NULL;
    s->impl = l->create(sample_rate);
    if (s->impl == NULL) {
        free(s);
        return NULL;
    }
    s->lang = l;
    s->rate_hz = sample_rate;
    s->preformat = 1;
    s->textin = 1;
    s->nuls = 2;
    return s;
}

tvtts_synth *TVTTS_CALL tvtts_create(uint32_t sample_rate)
{
    return tvtts_create_lang(sample_rate, g_langs[0].code);
}

void TVTTS_CALL tvtts_destroy(tvtts_synth *s)
{
    if (s == NULL)
        return;
    s->lang->destroy(s->impl);
    free(s);
}

const char *TVTTS_CALL tvtts_get_language(const tvtts_synth *s)
{
    return s != NULL ? s->lang->code : NULL;
}

/*
 * Change language, which means building a new engine and throwing the old one
 * away: the two have nothing in common to carry over.  What does carry over is
 * what belongs to the caller rather than to the engine -- the sample rate, the
 * compatibility switches, and rate, pitch and volume where the new language can
 * take them.  The voice becomes that language's first, because a voice number
 * means nothing outside its own language.
 */
int TVTTS_CALL tvtts_set_language(tvtts_synth *s, const char *lang)
{
    const tvtts_lang *l = lang_by_code(lang);
    void *impl;
    int rate, pitch;

    if (s == NULL || l == NULL)
        return -1;
    if (l == s->lang)
        return 0;
    rate = s->lang->get_rate(s->impl);
    pitch = s->lang->get_pitch(s->impl);
    impl = l->create(s->rate_hz);
    if (impl == NULL) {
        /* the new language may not have this output rate; 11025 always works */
        impl = l->create(11025u);
        if (impl == NULL)
            return -1;
        s->rate_hz = 11025u;
    }
    s->lang->destroy(s->impl);
    s->lang = l;
    s->impl = impl;
    if (s->compat_set)
        l->set_compat(impl, s->preformat, s->textin, s->nuls);
    /* Rate and pitch are the caller's numbers, so they are kept; the engine's
     * own default for the new voice would be a surprise to a caller that had
     * set them.  Volume is the engine's default, which is full. */
    l->set_rate(impl, rate);
    l->set_pitch(impl, pitch);
    return 0;
}

/* ---- settings ------------------------------------------------------------ */

/*
 * Setting a voice that belongs to another language switches to it.  That is the
 * whole of how a caller changes language if it would rather think in voices:
 * ask for voice 12 and the synth speaks Spanish.
 */
void TVTTS_CALL tvtts_set_voice(tvtts_synth *s, int voice)
{
    int local = 0;
    const tvtts_lang *l;

    if (s == NULL)
        return;
    l = lang_of_voice(voice, &local);
    if (l == NULL)
        return;
    if (l != s->lang && tvtts_set_language(s, l->code) != 0)
        return;
    s->lang->set_voice(s->impl, local);
}

void TVTTS_CALL tvtts_set_rate(tvtts_synth *s, int wpm)
{
    if (s != NULL)
        s->lang->set_rate(s->impl, wpm);
}

void TVTTS_CALL tvtts_set_pitch(tvtts_synth *s, int pitch)
{
    if (s != NULL)
        s->lang->set_pitch(s->impl, pitch);
}

void TVTTS_CALL tvtts_set_volume(tvtts_synth *s, uint32_t volume)
{
    if (s != NULL)
        s->lang->set_volume(s->impl, volume);
}

int TVTTS_CALL tvtts_get_voice(const tvtts_synth *s)
{
    if (s == NULL)
        return -1;
    return lang_base(s->lang) + s->lang->get_voice(s->impl);
}

int TVTTS_CALL tvtts_get_rate(const tvtts_synth *s)
{
    return s != NULL ? s->lang->get_rate(s->impl) : -1;
}

int TVTTS_CALL tvtts_get_pitch(const tvtts_synth *s)
{
    return s != NULL ? s->lang->get_pitch(s->impl) : -1;
}

void TVTTS_CALL tvtts_set_compat(tvtts_synth *s, int preformat, int textin,
                                 int terminators)
{
    if (s == NULL)
        return;
    s->preformat = preformat ? 1 : 0;
    s->textin = textin ? 1 : 0;
    s->nuls = terminators < 0 ? 0 : terminators;
    s->compat_set = 1;
    s->lang->set_compat(s->impl, preformat, textin, terminators);
}

void TVTTS_CALL tvtts_set_textin_mode(tvtts_synth *s, int mode)
{
    if (s != NULL)
        s->lang->set_textin_mode(s->impl, mode);
}

/* ---- the output rate ----------------------------------------------------- */

/*
 * Three rates.  The original offered 8 kHz and 11.025; 16 kHz is OpenTV's, and
 * every engine has it, because the tables for it are computed from formulas that
 * reproduce both of the original's sets exactly and those hold for the 1995
 * engines as well as the 1997 one -- see src/syn_hifi.h.  An engine still gets
 * to refuse a rate, and the answer is passed through rather than assumed here:
 * resampling instead would not be that engine's own sound.
 */
static const uint32_t g_sr_hz[3] = { 8000u, 11025u, 16000u };

uint32_t TVTTS_CALL tvtts_sample_rate_hz(int which)
{
    return (which >= 0 && which < 3) ? g_sr_hz[which] : 0u;
}

int TVTTS_CALL tvtts_get_sample_rate(const tvtts_synth *s)
{
    uint32_t hz;
    int i;

    if (s == NULL)
        return -1;
    hz = s->lang->get_rate_hz(s->impl);
    for (i = 0; i < 3; i++)
        if (g_sr_hz[i] == hz)
            return i;
    return -1;
}

int TVTTS_CALL tvtts_set_sample_rate(tvtts_synth *s, int which)
{
    int r;

    if (s == NULL || which < 0 || which >= 3)
        return -1;
    r = s->lang->set_rate_hz(s->impl, g_sr_hz[which]);
    if (r == 0)
        s->rate_hz = g_sr_hz[which];
    return r;
}

/* ---- synthesis ----------------------------------------------------------- */

int TVTTS_CALL tvtts_speak_bytes(tvtts_synth *s, const void *text, uint32_t len,
                                 tvtts_callback cb, void *user)
{
    if (s == NULL)
        return -1;
    return s->lang->speak_bytes(s->impl, text, len, cb, user);
}

/*
 * The phoneme entry points are English's only.  They read the engine's own
 * phoneme alphabet, and the two engines do not share one -- the 1995 set has a
 * lowercase half for the diphthongs that English has no use for -- so a Spanish
 * one would be a separate piece of work rather than a dispatch.
 */
int en_speak_phonemes(void *vs, const char *phonemes, tvtts_callback cb,
                      void *user);
int en_text_to_phonemes(void *vs, const char *text, char *buf, uint32_t cap);

int TVTTS_CALL tvtts_speak_phonemes(tvtts_synth *s, const char *phonemes,
                                    tvtts_callback cb, void *user)
{
    if (s == NULL || s->lang != &g_langs[0])
        return -1;
    return en_speak_phonemes(s->impl, phonemes, cb, user);
}

int en_speak_frames(void *vs, const uint8_t *frames, uint32_t n_frames,
                    tvtts_callback cb, void *user);

int TVTTS_CALL tvtts_speak_frames(tvtts_synth *s, const uint8_t *frames,
                                  uint32_t n_frames,
                                  tvtts_callback cb, void *user)
{
    if (s == NULL || s->lang != &g_langs[0])
        return -1;
    return en_speak_frames(s->impl, frames, n_frames, cb, user);
}

/*
 * Sing a score.  English only, for the same reason the phoneme entry points
 * are: the alphabet is this engine's and the 1995 engines do not share it.
 *
 * The compiler is pure and lives in sing.c, so what it produces can be checked
 * without listening to anything; all this does is give it somewhere to write
 * and hand the result to the ordinary byte path.
 */
int TVTTS_CALL tvtts_sing(tvtts_synth *s, const char *score,
                          tvtts_callback cb, void *user)
{
    char *buf;
    int n, rc;

    if (s == NULL || score == NULL || s->lang != &g_langs[0])
        return -1;
    n = tvtts_sing_compile(score, NULL, 0);
    if (n <= 0)
        return -1;
    buf = (char *)malloc((size_t)n);
    if (buf == NULL)
        return -1;
    tvtts_sing_compile(score, buf, (size_t)n);
    rc = s->lang->speak_bytes(s->impl, buf, (uint32_t)(n - 1), cb, user);
    free(buf);
    return rc;
}

int TVTTS_CALL tvtts_text_to_phonemes(tvtts_synth *s, const char *text,
                                      char *buf, uint32_t cap)
{
    if (s == NULL || s->lang != &g_langs[0])
        return -1;
    return en_text_to_phonemes(s->impl, text, buf, cap);
}
