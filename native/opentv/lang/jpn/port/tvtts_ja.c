/*
 * Japanese, as the dispatcher sees it: the eighteen entry points src/port/api.c
 * calls, over the front end in ja_frame.c and the 1997 synthesiser.
 *
 * This language is built differently from the other two and it is worth being
 * clear why.  English and Spanish are decompilations: a Centigram DLL exists,
 * the C reproduces it byte for byte, and the port layer's job is to drive an
 * engine that already knows how to turn text into speech.  There is no
 * Japanese TruVoice.  So Japanese has no engine of its own -- it builds the
 * 22-track parameter frames the 1997 synthesiser already reads and hands them
 * to en_speak_frames, which is the layer below stage 3 and knows nothing about
 * any language.  Nothing in src/engine or es/ changes, and nothing has to.
 *
 * WHAT THAT GETS FOR FREE, and it is more than it sounds.  Synth_Frame reads
 * the voice from the HIGH NIBBLE OF TRACK 21 -- it travels with the frames --
 * and applies that voice's whole parameter block: seven formant and bandwidth
 * percentages, the F1 and nasal resonator table offsets, jitter, shimmer,
 * breathiness, and the track 18/19 source settings.  So a Japanese voice is
 * made by writing a voice number into track 21, and the ten it can choose
 * from are Centigram's own ten, including the two whose formants are scaled up
 * for a shorter vocal tract.  See docs/VOICES.md.
 *
 * The public Japanese roster maps explicitly onto these engine indices.
 * Ojiisan stays at public index 5 for SAPI age metadata; indices 8 and 9
 * are the female voices, matching the original stock roster.
 *
 * WHAT IT DOES NOT GET.  Stage 3 is where volume attenuation, the rate table
 * and the base pitch are applied, and the frame path goes under it.  So those
 * three are this file's job -- which is the right place for them anyway, since
 * Japanese timing is computed from durations in milliseconds rather than
 * indexed out of a 26-row table, and its F0 is a Fujisaki baseline rather than
 * a stage-3 pitch node.
 *
 * WHAT IT CANNOT DO AT ALL, stated here because it is the first thing anyone
 * will notice: this reads KANA AND ROMAJI.  It does not read kanji, which is
 * most of real Japanese text.  Kanji needs a morphological analyser over a
 * dictionary -- the surface form to its reading, with the ambiguity resolved
 * by context -- and that is a piece of work the size of this one.  Text whose
 * kanji this cannot read comes out as the kana around them, not as an error.
 */
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"
#include "tvtts_port.h"
#include "ja.h"
#include "ja_njd.h"

/* The engine's own per-voice tables, from src/engine/voices.c. */
extern int32_t tv_v_pitch(int32_t v);
extern int32_t tv_v_speed(int32_t v);
/* log10(vol / 65535.0) * -10.0, truncated and capped: src/engine/volume.c. */
extern int32_t Volume_ToAtten(uint32_t vol);

#define JA_VOICES 10

/*
 * The original ten-voice order is restored: public Japanese indices now
 * match the stock English engine indices. This deliberately undoes the
 * temporary seven-voice roster, as requested by the user. SAPI's female
 * slots (8/9) and elderly slot (5) again have their original meaning.
 *
 * Sidney, Eager Eddie, Wanda and Julia use source-specific Japanese
 * calibration in ja_voice.c, with their stock formant and voicing blocks.
 * Noise matching uses Peter's Japanese baseline; it is an engineering
 * reference rather than perceptual validation. Existing trims for the other
 * voices stay associated with the same named voice, irrespective of index.
 */
static const struct {
    const char *name;
    int         engine;         /* the Centigram voice whose block it uses */
    int         atten[3];       /* dB off tracks 0/1/2, by rate: 8k, 11k, 16k */
} g_voices[JA_VOICES] = {
    /*
     * A blanket attenuation on the voiced and noise SOURCES, which is the
     * crude instrument and is now needed by four voices at one rate only.
     * There were two real causes of overflow and neither of them was this:
     *
     *   1. g_synhifi_8 held 700 entries, reaching 5592 Hz.  At 16 kHz an F5
     *      lookup can go higher, and three stock voices set a ceiling above
     *      it -- Sidney and Melvin at 5970, Wanda at 6000 -- so those read
     *      off the end of the table.  Fixed in src/syn_hifi.c, which now has
     *      1001 entries through the 8 kHz Nyquist endpoint.  That repair is
     *      not Japanese's: it applies to those voices in English at 16 kHz
     *      too.
     *
     *   2. The noise levels were fitted on Peter's formant block, and a voice
     *      with different formants resonates the same noise differently.
     *      ja_voice.c matches the aspiration and parallel sources per voice,
     *      rate and context for Sidney, Eager Eddie, Wanda and Julia instead
     *      of turning everything down.
     *
     * With both in place the whole 16 kHz column is zero -- measured, not
     * assumed: with this table emptied, every voice renders 35 texts at
     * 16 kHz with no clipped sample and no adjacent jump over 32000.  Osamu's
     * entry there used to be 8 dB, and all 8 of those decibels were measuring
     * the table overrun.
     *
     * The 11 kHz column survives because 11 kHz never had the overrun to
     * begin with: its Nyquist is 5512 Hz and the old table reached 5592, so
     * nothing read past the end.  These four are the voices ja_voice.c does
     * not cover, and the figures are each the smallest that is clean.
     *
     * 8 kHz is refused outright (see ja_set_rate_hz), so that column is never
     * read; it carries the 11 kHz figure rather than a number nobody measured.
     */
    { "Taro",     0, { 2, 2, 0 } },
    { "Tsuyoshi", 1, { 0, 0, 0 } },
    { "Kenta",    2, { 0, 0, 0 } },
    { "Daichi",   3, { 0, 0, 0 } },
    { "Takeshi",  4, { 0, 0, 0 } },
    { "Ojiisan",  5, { 1, 1, 0 } },
    { "Osamu",    6, { 4, 4, 0 } },
    { "Akira",    7, { 8, 8, 0 } },
    { "Hanako",   8, { 0, 0, 0 } },
    { "Keiko",    9, { 0, 0, 0 } }
};

/*
 * The Fujisaki baseline the Japanese prosody was built at, and the voice whose
 * base pitch it belongs to.  jp_speak.py uses Fb = 72.0 Hz throughout, and
 * voice 0's base pitch is 85, so the API's pitch number scales the baseline
 * from there: at 85 the contour is exactly the Python's, and asking for a
 * voice's own default pitch gives that voice's baseline.
 *
 * Fb is the floor the phrase and accent components rise FROM, not a mean, so
 * 72 Hz is an ordinary adult male and 208 -> 176 Hz is an ordinary adult
 * female once the components are on top.
 */
#define JA_FB_REF    72.0
#define JA_PITCH_REF 85
/* The rate the duration targets were measured at, in the API's words-per-
 * minute: ~7 morae/s, which is the rate Kawai's symbol values assume. */
#define JA_WPM_REF   150

struct ja_synth {
    void    *en;                /* the synthesiser, driven frame by frame */
    /*
     * A SECOND English synthesiser, for asking what a Latin word sounds like
     * and nothing else.  It has to be separate: the phoneme trace is got by
     * synthesising and discarding the audio, so asking `en` would advance the
     * synthesiser mid-utterance -- and phoneme mode is known to colour the
     * next utterance (lang/jpn/research/open_questions.md).  Created on the first Latin
     * word that needs it, so an utterance of pure Japanese never makes one.
     */
    void    *en_ask;
    int      voice, wpm, pitch;
    uint32_t volume;
    uint32_t rate_hz;
};

/* The one used for asking, created on demand.  NULL on failure, which just
 * means the rules are skipped and romaji and spelling answer instead. */
static void *ask_synth(struct ja_synth *s)
{
    if (s->en_ask == NULL)
        s->en_ask = en_create(s->rate_hz);
    return s->en_ask;
}

/* The callback ja_read_latin_g2p takes; see ja_njd.h for why it is one. */
static int ask_phonemes(void *ctx, const char *word, char *buf, size_t cap)
{
    return en_text_to_phonemes(ctx, word, buf, (uint32_t)cap);
}

void *ja_create(uint32_t sample_rate)
{
    struct ja_synth *s = (struct ja_synth *)calloc(1, sizeof *s);

    if (s == NULL)
        return NULL;
    if (sample_rate < 10000u) {   /* see ja_set_rate_hz: 8 kHz is refused */
        free(s);
        return NULL;
    }
    s->en = en_create(sample_rate);
    if (s->en == NULL) {
        free(s);
        return NULL;
    }
    s->voice = 0;
    s->wpm = (int)tv_v_speed(g_voices[0].engine);
    s->pitch = (int)tv_v_pitch(g_voices[0].engine);
    s->volume = 0xffffu;
    s->rate_hz = sample_rate;
    return s;
}

void ja_destroy(void *vs)
{
    struct ja_synth *s = (struct ja_synth *)vs;

    if (s == NULL)
        return;
    en_destroy(s->en);
    if (s->en_ask != NULL)
        en_destroy(s->en_ask);
    free(s);
}

void ja_set_voice(void *vs, int voice)
{
    struct ja_synth *s = (struct ja_synth *)vs;

    if (s != NULL && voice >= 0 && voice < JA_VOICES) {
        s->voice = voice;
        /* The engine's own voice too, not because the frame path reads it for
         * the adjustment row -- that comes from track 21 -- but because the
         * breathiness and gain of a voice past the stock ten do come from the
         * synth's setting, and leaving the two disagreeing would be a trap for
         * whoever adds one. */
        en_set_voice(s->en, g_voices[voice].engine);
    }
}

void ja_set_rate(void *vs, int wpm)
{
    struct ja_synth *s = (struct ja_synth *)vs;

    if (s == NULL)
        return;
    /*
     * Japanese is not restricted to the engine's eight-word-per-minute grid.
     * That grid is an artefact of stage 3 indexing a 26-row table, and this
     * language computes its durations from milliseconds instead, so any rate
     * in the range means something.  The range itself is kept, so a caller
     * that knows the API does not have to learn a second one.
     */
    if (wpm < TVTTS_RATE_MIN)
        wpm = TVTTS_RATE_MIN;
    else if (wpm > TVTTS_RATE_MAX)
        wpm = TVTTS_RATE_MAX;
    s->wpm = wpm;
}

void ja_set_pitch(void *vs, int pitch)
{
    struct ja_synth *s = (struct ja_synth *)vs;

    if (s == NULL)
        return;
    if (pitch < TVTTS_PITCH_MIN)
        pitch = TVTTS_PITCH_MIN;
    else if (pitch > TVTTS_PITCH_MAX)
        pitch = TVTTS_PITCH_MAX;
    s->pitch = pitch;
}

void ja_set_volume(void *vs, uint32_t volume)
{
    struct ja_synth *s = (struct ja_synth *)vs;

    if (s != NULL)
        s->volume = volume;
}

int ja_get_voice(const void *vs)
{
    const struct ja_synth *s = (const struct ja_synth *)vs;

    return s != NULL ? s->voice : -1;
}

int ja_get_rate(const void *vs)
{
    const struct ja_synth *s = (const struct ja_synth *)vs;

    return s != NULL ? s->wpm : -1;
}

int ja_get_pitch(const void *vs)
{
    const struct ja_synth *s = (const struct ja_synth *)vs;

    return s != NULL ? s->pitch : -1;
}

uint32_t ja_get_rate_hz(const void *vs)
{
    const struct ja_synth *s = (const struct ja_synth *)vs;

    return s != NULL ? s->rate_hz : 0u;
}

/*
 * 8 kHz is refused, and that is a measurement rather than a policy.
 *
 * The Japanese noise postures put a sibilant's energy at F5, which the engine
 * derives as max(3970, effective F4 + 400) and which for /s/ comes out at
 * 4720 Hz, and track 8's raw band runs 4-8 kHz.  At an 8 kHz output rate
 * Nyquist is 4 kHz, so all of that is AT OR ABOVE it: the result is
 * alternating-sample content at full amplitude.  Measured over 35 texts, even
 * Taro -- which raises no formant at all -- tears 377 times at 8 kHz and
 * clips 65 samples, where at 16 kHz it does neither.  Attenuating the voiced
 * source does not touch it; attenuating the noise tracks does, which is how
 * the cause was identified.
 *
 * So it is not a voice problem and not a level problem: at 8 kHz the noise
 * belongs somewhere else, and where is a calibration nobody has done.  The
 * project fitted the 11025 trim and never the 8000 one -- the Python applies
 * the 11025 figures at 8000 as well, which is the wrong table.
 *
 * The API is built for this: "an engine still gets to refuse a rate, and the
 * answer is passed through rather than assumed", and tvtts_set_language falls
 * back to 11025 when a language cannot take the rate it is asked for.  Saying
 * no is better than speaking through it.
 */
int ja_set_rate_hz(void *vs, uint32_t hz)
{
    struct ja_synth *s = (struct ja_synth *)vs;
    int r;

    if (s == NULL)
        return -1;
    if (hz < 10000u)
        return -1;
    r = en_set_rate_hz(s->en, hz);
    if (r == 0)
        s->rate_hz = hz;
    return r;
}

/*
 * Neither of these means anything here.  The compatibility switches are about
 * stage 0's preformatter and stage 1's tokenizer, and the Japanese front end
 * is its own reader -- there is no ANSI escape handling, no abbreviation
 * expansion and no textin mode to pick.  They are accepted and ignored rather
 * than refused, so a caller that sets them for English and then switches
 * language does not have to know that.
 */
void ja_set_compat(void *vs, int preformat, int textin, int terminators)
{
    (void)vs; (void)preformat; (void)textin; (void)terminators;
}

void ja_set_textin_mode(void *vs, int mode)
{
    (void)vs; (void)mode;
}

/*
 * Read a Latin word as romaji rather than as English.  OFF by default; see
 * TVTTS_EXT_JA_ROMAJI in tvtts.h for why that way round.
 */
static int g_romaji;

void ja_set_extensions(uint32_t mask)
{
    /*
     * Japanese has one flag.  Every behaviour change in the other two
     * languages is gated so the byte-exact corpus can still be reproduced
     * with them off; there is no Japanese corpus, so a flag here gates a
     * CHOICE rather than a change -- and `take` is a choice, not a fact.  The
     * feature flags the Python front end carries -- PHI, FINAL_N, BRIDGE_F3
     * and the rest -- exist for A/B listening and all of them are on; see
     * ja_frame.c.
     */
    g_romaji = (mask & TVTTS_EXT_JA_ROMAJI) != 0;
}

int ja_voice_count(void)
{
    return JA_VOICES;
}

const char *ja_voice_name(int voice)
{
    return (voice >= 0 && voice < JA_VOICES) ? g_voices[voice].name : NULL;
}

int ja_voice_rate(int voice)
{
    return (voice >= 0 && voice < JA_VOICES)
           ? (int)tv_v_speed(g_voices[voice].engine) : -1;
}

int ja_voice_pitch(int voice)
{
    return (voice >= 0 && voice < JA_VOICES)
           ? (int)tv_v_pitch(g_voices[voice].engine) : -1;
}

/* ---- punctuation -------------------------------------------------------- */

/*
 * The mora parser keeps [a-z], the length mark, the space and the two phrase
 * bars, and drops everything else -- so Japanese punctuation reaches it as
 * nothing at all, and a full stop becomes silence where it should become a
 * pause.  The prosody has the symbols for this already: '|' is Kawai's weaker
 * ICRLB boundary, worth a 0.1 s pause and a P3 phrase command, and '||' is a
 * clause boundary, worth 0.3 s and P2.
 *
 * So the punctuation is rewritten into those marks here, BEFORE the parser,
 * rather than inside it.  Keeping it out of ja_to_morae is deliberate: that
 * function has to stay the one the oracle tests, and none of the 533 reference
 * words contains punctuation, so a mapping added to it would be untested
 * either way.  Here it is visible, and it is the shipping path's own decision.
 *
 * A mark at the very END is dropped instead of becoming a pause.  A screen
 * reader that waits 0.3 s of silence after every sentence before returning is
 * a screen reader that feels slow, and the pause has nothing left to separate.
 */
static int map_punct(uint32_t u)
{
    switch (u) {
    case 0x3002:                /* the ideographic full stop */
    case '.': case '!': case '?':
    case 0xff01: case 0xff1f:   /* fullwidth ! and ? */
        return 2;               /* '||' */
    case 0x3001:                /* the ideographic comma */
    case ',': case ':': case ';':
    case 0x30fb:                /* the katakana middle dot */
    case 0xff0c: case 0xff1a: case 0xff1b:
        return 1;               /* '|' */
    default:
        return 0;
    }
}

/* One UTF-8 character, decoded far enough to classify it. */
static uint32_t next_cp(const unsigned char *p, size_t len, size_t *i)
{
    uint32_t u = p[*i];
    int extra, k;

    if (u < 0x80)       { (*i)++; return u; }
    else if (u >= 0xf0) { extra = 3; u &= 0x07; }
    else if (u >= 0xe0) { extra = 2; u &= 0x0f; }
    else if (u >= 0xc0) { extra = 1; u &= 0x1f; }
    else                { (*i)++; return 0xfffd; }
    if (*i + (size_t)extra >= len) { (*i)++; return 0xfffd; }
    for (k = 1; k <= extra; k++) {
        if ((p[*i + (size_t)k] & 0xc0) != 0x80) { (*i)++; return 0xfffd; }
        u = (u << 6) | (uint32_t)(p[*i + (size_t)k] & 0x3f);
    }
    *i += (size_t)extra + 1;
    return u;
}

/*
 * THE ENGINE'S INLINE ESCAPES, which this path has to consume itself.
 *
 * tvtts_pitch_sequence and its four siblings write ESC '[' digits letter:
 * 'p' pitch (the value is 2n), 'r' rate in words per minute, 'i' an index
 * mark, 's' a break in milliseconds, 'N'/'F' punctuation on and off.  The
 * English engine eats them in feed.c and input.c and Spanish in its own
 * escape.c.  Nothing here did, so they reached ja_to_morae, which drops the
 * ESC and the '[' as characters it cannot place and reads the rest as text:
 * NVDA announcing a capital sends ESC[50p then `A`, and it came out as
 * "fifty P A" with no pitch change at all.
 *
 * Pitch and rate are applied to THIS UTTERANCE ONLY and the synthesiser's own
 * settings are left alone.  That matters because of how the caller uses them:
 * NVDA closes a capital with a second PitchCommand to put the pitch back, and
 * the driver holds trailing escapes back and drops them, having nothing left
 * to apply to.  A change that persisted would therefore never be undone and
 * every word after the first capital would stay raised.
 *
 * The other three are consumed and dropped rather than honoured.  Dropping is
 * already right for 'N' and 'F' -- this path turns punctuation into pauses
 * rather than naming it -- and for 's' and 'i' it is the lesser of two
 * wrongs: a break of n milliseconds has no mora to be, and an index mark
 * needs an event this path does not emit at all.  Neither is made worse by
 * being silent, and both were being SPOKEN.
 *
 * A pitch or rate escape anywhere in the text applies to all of it, which is
 * not what the English engine does -- there it applies from where it stands.
 * A capital is its own utterance, so the case that was reported is exact; a
 * capital mid-sentence raises the sentence.  Honouring it properly means
 * splitting the utterance at the escape, which is a larger change than this.
 */
struct ja_esc {
    int pitch;                  /* <0: none seen */
    int wpm;
};

static void esc_init(struct ja_esc *e)
{
    e->pitch = -1;
    e->wpm = -1;
}

static size_t strip_escapes(const char *text, size_t len, char *out,
                            struct ja_esc *e)
{
    size_t i = 0, n = 0;

    while (i < len) {
        size_t j, start = i;
        unsigned long v;
        int any;

        if ((unsigned char)text[i] != 0x1b || i + 1 >= len ||
            text[i + 1] != '[') {
            out[n++] = text[i++];
            continue;
        }
        j = i + 2;
        v = 0;
        any = 0;
        while (j < len && text[j] >= '0' && text[j] <= '9') {
            if (v < 1000000ul)
                v = v * 10ul + (unsigned long)(text[j] - '0');
            any = 1;
            j++;
        }
        if (j >= len || !any) {
            /* not a complete escape: leave it alone rather than eat the rest */
            out[n++] = text[i++];
            continue;
        }
        switch (text[j]) {
        case 'p':
            e->pitch = (int)(v * 2ul);          /* the command carries n, not 2n */
            break;
        case 'r':
            e->wpm = (int)v;
            break;
        case 'i': case 's': case 'N': case 'F':
            break;                              /* consumed, see above */
        default:
            /* not one of ours: pass it through untouched */
            out[n++] = text[i++];
            continue;
        }
        i = j + 1;
        (void)start;
    }
    return n;
}

/*
 * Rewrite the punctuation and say whether the text asks a question.  Returns
 * the new length; the result needs at most 2 bytes per input character, which
 * is why the buffer is sized at twice the input.
 */
static size_t rewrite(const char *text, size_t len, char *out, int *question)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t i = 0, n = 0;
    int last_mark = 0;

    *question = 0;
    while (i < len) {
        size_t at = i;
        uint32_t u = next_cp(p, len, &i);
        int mark = map_punct(u);

        if (mark) {
            /* Hirose: the interrogative gets an extra accent command on the
             * final mora, which is what makes a question rise where a
             * statement falls.  Only a question mark says so; a Japanese
             * question written with /ka/ and a full stop is not detectable
             * without the parser nobody has written. */
            if (u == '?' || u == 0xff1f)
                *question = 1;
            /* Runs of punctuation collapse, and the strongest wins. */
            if (last_mark) {
                if (mark > last_mark) {
                    n -= (size_t)last_mark;
                    last_mark = 0;
                } else {
                    continue;
                }
            }
            out[n++] = '|';
            if (mark == 2)
                out[n++] = '|';
            last_mark = mark;
            continue;
        }
        last_mark = 0;
        memcpy(out + n, text + at, i - at);
        n += i - at;
    }
    /* Drop a trailing mark, and any space before it, so nothing ends in a
     * pause the listener has to sit through. */
    while (n > 0 && (out[n - 1] == '|' || out[n - 1] == ' '))
        n--;
    return n;
}

/* ---- the dictionary ----------------------------------------------------- */

/*
 * Loaded once per process and shared by every synth, because it is 26 MB and
 * every synth would otherwise carry its own copy.  Loaded LAZILY, on the
 * first Japanese utterance that needs it, so an English-only or kana-only
 * caller never reads the file at all.
 *
 * If it is not there, `g_dict_tried` records that and nothing tries again:
 * the kana and romaji path works exactly as it did before, which is the whole
 * degradation story.  A host that wants to say where the file is sets
 * TVTTS_JA_DICT; otherwise it is looked for beside the module and beside the
 * executable.  See ja_dict_path.
 */
static ja_dict g_dict;
static int g_dict_tried;
static int g_dict_ok;

static const ja_dict *dict_get(void)
{
    char path[1024];
    const char *p;

    if (g_dict_tried)
        return g_dict_ok ? &g_dict : NULL;
    g_dict_tried = 1;
    p = ja_dict_path(path, sizeof path);
    if (p == NULL)
        return NULL;
    g_dict_ok = ja_dict_open(&g_dict, p) == 0;
    return g_dict_ok ? &g_dict : NULL;
}

/*
 * Whether this text is WORTH TRYING as romaji.
 *
 * The analyser normalises ASCII TO fullwidth before it looks anything up,
 * because that is how naist-jdic is keyed -- so `konnichiwa` would reach it as
 * ＫＯＮＮＩＣＨＩＷＡ and come out as the Japanese names of eleven Latin
 * letters.  Romaji input therefore has to stay on the kana parser, which is
 * what it was written for.
 *
 * This is only the first half of the test: all-ASCII with at least one letter
 * and no digit.  A digit is not romaji precisely because `1250` says nothing
 * at all on the kana path and センニヒャクゴジュー through the analyser.  The
 * SECOND half is in the caller and is the one that matters -- see below.
 */
static int could_be_romaji(const char *t, uint32_t len)
{
    uint32_t i;
    int letters = 0;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c >= 0x80)
            return 0;
        if (c >= '0' && c <= '9')
            return 0;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            letters = 1;
    }
    return letters;
}

/* ---- speaking ----------------------------------------------------------- */

/*
 * The escapes come off here, before anything else looks at the bytes --
 * could_be_romaji would otherwise weigh them as text, and the mora parser
 * would read them as text.  Doing it in a wrapper keeps the buffer's lifetime
 * to one place: the body below has twenty-odd early returns and none of them
 * can leak it.
 */
static int speak_stripped(void *vs, const void *text, uint32_t len,
                          tvtts_callback cb, void *user,
                          const struct ja_esc *esc_in);

int ja_speak_bytes(void *vs, const void *text, uint32_t len,
                   tvtts_callback cb, void *user)
{
    struct ja_esc esc;
    char *stripped;
    int rc;

    if (vs == NULL || (text == NULL && len != 0))
        return -1;
    if (len == 0)
        return 0;
    esc_init(&esc);
    stripped = (char *)malloc(len);
    if (stripped == NULL)
        return -1;
    len = (uint32_t)strip_escapes((const char *)text, len, stripped, &esc);
    if (len == 0) {             /* escapes and nothing else: nothing to say */
        free(stripped);
        return 0;
    }
    rc = speak_stripped(vs, stripped, len, cb, user, &esc);
    free(stripped);
    return rc;
}

static int speak_stripped(void *vs, const void *text, uint32_t len,
                          tvtts_callback cb, void *user,
                          const struct ja_esc *esc_in)
{
    const struct ja_esc esc = *esc_in;
    struct ja_synth *s = (struct ja_synth *)vs;
    ja_mora *morae = NULL;
    char *buf = NULL;
    const ja_dict *d = NULL;
    ja_front fr;
    ja_opts o;
    ja_utt u;
    int n_morae = 0, question = 0, atten, i, rc, analysed = 0, romaji = 0;
    int eff_pitch, eff_wpm;
    size_t blen;

    if (s == NULL || (text == NULL && len != 0))
        return -1;
    if (len == 0)
        return 0;
    memset(&fr, 0, sizeof fr);

    /*
     * HOW A LATIN STRING IS READ.  Three tests, in this order, and the order
     * is the whole design.
     *
     * 1. THE LEXICON FIRST.  Every Japanese system reads a bare `take` as the
     *    English テイク, not the Japanese タケ -- measured on Google TTS,
     *    Microsoft OneCore Japanese and ETI-Eloquence -- because they look the
     *    word up before they consider anything else.  Trying romaji first got
     *    107 of the 840 Latin words naist-jdic knows wrong: `Amazon` came out
     *    /a.ma.zo.N/ instead of アマゾン and `DATE` /da.te/ instead of デイト,
     *    which is the same mistake as `take` on a word we already have.
     *
     * 2. THEN ROMAJI, AND ONLY IF IT CONSUMES THE WHOLE STRING.  "All-ASCII
     *    letters" is a test for the Latin alphabet, not for romaji.  The mora
     *    parser is lenient -- it has to be, because the kana path feeds it
     *    generated romaji -- so it accepted every English word and quietly
     *    dropped the letters Japanese has no mora for: `hello` became
     *    /he.Q.o/, `computer` /o.pu.te/ with the c, m and r gone, `blorf` the
     *    single mora /o/.  Over 20,280 dictionary readings round-tripped
     *    through romaji, none fails the strict test, so it costs nothing.
     *
     * 3. THEN SPELLING, which is what the analyser falls to anyway.
     *
     * What is MISSING is between 1 and 3: a reading for a word-like string
     * nothing knows, so `computer` is spelled out where it should be
     * コンピューター.  That is grapheme-to-phoneme conversion; see
     * lang/jpn/research/open_questions.md section 4.3 for what it would take.
     */
    d = dict_get();
    if (g_romaji && could_be_romaji((const char *)text, len)) {
        int skipped = 0, lexicon = 0;

        if (d != NULL) {
            /* step 1: does anything know this word? */
            char *z = (char *)malloc((size_t)len + 1);
            if (z == NULL)
                return -1;
            memcpy(z, text, len);
            z[len] = '\0';
            rc = ja_front_text_g2p(d, z, ask_phonemes, ask_synth(s), &fr);
            free(z);
            if (rc < 0)
                return -1;
            if (rc == 0 && fr.known > 0)
                lexicon = 1;    /* keep this reading; skip romaji */
            else
                ja_front_free(&fr);
        }
        if (lexicon) {
            morae = fr.morae;
            n_morae = fr.n_morae;
            question = fr.question;
            analysed = 1;
        } else {
            /* step 2 */
            buf = (char *)malloc((size_t)len * 2 + 2);
            morae = (ja_mora *)malloc(((size_t)len + 1) * sizeof *morae);
            if (buf == NULL || morae == NULL) {
                free(buf);
                free(morae);
                return -1;
            }
            blen = rewrite((const char *)text, len, buf, &question);
            n_morae = ja_to_morae_ex(buf, blen, morae, (int)len + 1,
                                     &skipped);
            if (skipped != 0 || n_morae <= 0) {
                free(buf);
                free(morae);
                buf = NULL;
                morae = NULL;
                n_morae = 0;
                question = 0;
            } else
                romaji = 1;
        }
    }
    if (!analysed && !romaji && d != NULL) {
        /* The analyser wants a NUL-terminated string, and the API takes a
         * length, so this is the one copy the whole path makes. */
        char *z = (char *)malloc((size_t)len + 1);
        if (z == NULL)
            return -1;
        memcpy(z, text, len);
        z[len] = '\0';
        rc = ja_front_text_g2p(d, z, ask_phonemes, ask_synth(s), &fr);
        free(z);
        if (rc < 0)
            return -1;
        if (rc == 0) {
            morae = fr.morae;
            n_morae = fr.n_morae;
            question = fr.question;
            analysed = 1;
        } else {
            /* The analyser found nothing sayable.  Silence rather than an
             * error: the caller asked for an utterance and got one. */
            ja_front_free(&fr);
            return 0;
        }
    }
    if (!analysed && !romaji) {
        /*
         * No dictionary, and the text is not clean romaji.  Spelling it out
         * from Open JTalk's pronunciation table needs no dictionary and says
         * all of the input, where the romaji parser would say some of it, so
         * that is tried first; the parser is the last resort and gets what it
         * was written for -- kana.
         */
        rc = ja_front_spell((const char *)text, &fr);
        if (rc < 0)
            return -1;
        if (rc == 0 && fr.n_morae > 0) {
            morae = fr.morae;
            n_morae = fr.n_morae;
            analysed = 1;
        } else
            ja_front_free(&fr);
    }
    if (!analysed && !romaji) {
        buf = (char *)malloc((size_t)len * 2 + 2);
        morae = (ja_mora *)malloc(((size_t)len + 1) * sizeof *morae);
        if (buf == NULL || morae == NULL) {
            free(buf);
            free(morae);
            return -1;
        }
        blen = rewrite((const char *)text, len, buf, &question);
        n_morae = ja_to_morae(buf, blen, morae, (int)len + 1);
        if (n_morae <= 0) {
            /* Nothing sayable -- a line of kanji with no dictionary and no
             * derivable reading.  Silence rather than an error: the caller
             * asked for an utterance and got one. */
            free(buf);
            free(morae);
            return 0;
        }
    }
    ja_opts_default(&o);
    o.sr = (int)s->rate_hz;
    o.engine_voice = g_voices[s->voice].engine;
    /* An inline escape overrides for this utterance only; see strip_escapes. */
    eff_pitch = esc.pitch >= 0 ? esc.pitch : s->pitch;
    eff_wpm = esc.wpm >= 0 ? esc.wpm : s->wpm;
    if (eff_pitch < TVTTS_PITCH_MIN)
        eff_pitch = TVTTS_PITCH_MIN;
    else if (eff_pitch > TVTTS_PITCH_MAX)
        eff_pitch = TVTTS_PITCH_MAX;
    if (eff_wpm < TVTTS_RATE_MIN)
        eff_wpm = TVTTS_RATE_MIN;
    o.rate_scale = (double)JA_WPM_REF / (double)eff_wpm;
    o.fb = JA_FB_REF * eff_pitch / (double)JA_PITCH_REF;
    o.question = question;
    o.accent = 0;
    /*
     * With the analyser: one accent type per accent phrase and one devoicing
     * flag per mora, both from the dictionary and the five NJD stages.
     *
     * Without it: heiban -- no accent nucleus -- for every word, because the
     * accent type of a Japanese word is lexical and there is nothing to look
     * it up in.  It is the right default to be wrong with: it is the
     * commonest pattern by a wide margin, and a wrong nucleus is heard as a
     * different word where a missing one is heard as a flat reading of the
     * right one.
     */
    if (analysed) {
        o.accents = fr.accents;
        o.n_accents = fr.n_accents;
        o.devoiced = fr.devoiced;
        o.n_devoiced = fr.n_morae;
    }
    if (ja_build(morae, n_morae, &o, &u) != 0) {
        free(buf);
        if (analysed)
            ja_front_free(&fr);
        else
            free(morae);
        return -1;
    }
    ja_pitch(&u, morae, n_morae, &o, NULL);
    free(buf);
    if (analysed)
        ja_front_free(&fr);
    else
        free(morae);

    /*
     * Volume, the way the engine does it: Volume_ToAtten turns the SAPI number
     * into whole decibels and stage 3 SUBTRACTS that from the amplitude tracks
     * (stage3.c:425 reads `0x3c - volume_atten`, and 0x3c is the default
     * amplitude).  Tracks 0, 1 and 2 are the three it applies to -- voicing,
     * frication and aspiration -- so those are the three applied here, which
     * keeps a fricative and a vowel moving together.
     */
    atten = (int)Volume_ToAtten(s->volume);
    /*
     * And the voice's own trim, on the same three tracks, because it is the
     * same quantity: how hard this voice may drive the filter bank before it
     * tears.  Measured per rate -- see g_voices -- so a voice that needs
     * nothing at 16 kHz is left exactly as the Python produced it.
     */
    atten += g_voices[s->voice].atten[s->rate_hz >= 16000u ? 2
                                      : (s->rate_hz >= 10000u ? 1 : 0)];
    if (atten != 0) {
        for (i = 0; i < u.n; i++) {
            uint8_t *t = u.frames + (size_t)i * JA_NTRACK;
            int k;

            for (k = 0; k <= 2; k++) {
                int v = (int)t[k] - atten;

                t[k] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
            }
        }
    }
    /* Track 21 names the ENGINE voice, not the public Japanese roster slot.
     * Keep it consistent with ja_set_voice above: the filtered roster is not
     * necessarily index-for-index with the engine. The temporary filtered
     * roster exposed this bug before the original order was restored. */
    if (g_voices[s->voice].engine != 0) {
        for (i = 0; i < u.n; i++)
            u.frames[(size_t)i * JA_NTRACK + 21] =
                (uint8_t)((u.frames[(size_t)i * JA_NTRACK + 21] & 0x0f) |
                          (g_voices[s->voice].engine << 4));
    }
    rc = en_speak_frames(s->en, u.frames, (uint32_t)u.n, cb, user);
    ja_utt_free(&u);
    return rc;
}
