/*
 * The inside of the library: what each language's port layer provides, and
 * what src/port/api.c calls.
 *
 * tvtts.h is the interface callers see, and it has one set of functions
 * whatever language a synth speaks.  Underneath, each language has its own
 * engine -- its own object layout, its own constant tables, its own
 * decompilation -- and so its own copy of the layer that owns one.  The two
 * are not variants: CGRM_EN is the 1997 engine and CGRM_ES the 1995 one, and
 * they share no code.
 *
 * So each language exports the same dozen entry points under its own prefix,
 * `en_` and `es_`, and api.c holds the public names and picks by language.  A
 * synth is an opaque `void *` across this boundary because the two structures
 * have nothing in common but their purpose; the language that made one is the
 * only thing that may look inside it.
 *
 * Adding a language is adding a file like lang/spa/port/tvtts_es.c, a row in
 * api.c's table, and nothing else.
 */
#ifndef TVTTS_PORT_H
#define TVTTS_PORT_H

#include <stdint.h>

#include "tvtts.h"

/*
 * OpenTV: `[:phone TruVoice on|off]` rewritten in place into the engine's
 * phoneme-mode escape, `ESC[1I` / `ESC[0I`.  Returns the new length; the
 * text only ever shrinks, so the caller's buffer is always big enough.
 * English spells the command, so English calls this.  See src/port/sing.c.
 */
size_t tv_phone_commands(char *text, size_t len);

/*
 * OpenTV: `CamelCase` split into `Camel Case`, gated on TVTTS_EXT_CAMEL.
 * Returns the spaces wanted; with buf non-NULL it writes the result, which
 * needs len plus that many bytes.  Run it *after* tv_phone_commands.  See
 * src/port/sing.c.
 */
size_t tv_camel_split(const char *t, size_t len, char *buf);

/* One language's engine, as the dispatcher sees it.  Every function takes the
 * synth its own create returned. */
typedef struct {
    const char *code;                   /* "en", "es" */
    const char *name;                   /* "American English" */

    void       *(*create)(uint32_t sample_rate);
    void        (*destroy)(void *s);

    void        (*set_voice)(void *s, int voice);
    void        (*set_rate)(void *s, int wpm);
    void        (*set_pitch)(void *s, int pitch);
    void        (*set_volume)(void *s, uint32_t volume);
    int         (*get_voice)(const void *s);
    int         (*get_rate)(const void *s);
    int         (*get_pitch)(const void *s);

    /* The sample rate in hertz rather than as an index, because which indices
     * exist is the dispatcher's business and which rates an engine can be built
     * for is the engine's.  All of them have all three, as it happens, but that
     * is the engine's answer to give and not the dispatcher's to assume. */
    uint32_t    (*get_rate_hz)(const void *s);
    int         (*set_rate_hz)(void *s, uint32_t hz);

    void        (*set_compat)(void *s, int preformat, int textin,
                              int terminators);
    /* Before the first utterance only: the engine makes its tokenizer when the
     * synth is made, so this rebuilds it. */
    void        (*set_textin_mode)(void *s, int mode);

    int         (*voice_count)(void);
    const char *(*voice_name)(int voice);
    int         (*voice_rate)(int voice);
    int         (*voice_pitch)(int voice);

    int         (*speak_bytes)(void *s, const void *text, uint32_t len,
                               tvtts_callback cb, void *user);

    /* Which OpenTV extensions are on.  Process-wide rather than per-synth,
     * because the engines keep their stage 2 working state in globals, so one
     * synth was never independent of another here.  Each language takes the
     * whole mask and picks out what means anything to it. */
    void        (*set_extensions)(uint32_t mask);

    /*
     * How speak_bytes wants its text.  0 is the engine's own single-byte code
     * page, which is what the two decompiled engines read and what their
     * corpora are stored in; 1 is UTF-8.
     *
     * This exists because tvtts_speak_utf8 and tvtts_speak_utf16 have to
     * narrow the text to SOMETHING before handing it over, and cp1252 -- the
     * right answer for English and Spanish -- turns every kana into a question
     * mark.  A language that reads its own script says so here.
     */
    int         utf8_text;
} tvtts_lang;

/* Whether this synth's language reads UTF-8; src/port/api.c owns the synth
 * structure, so this is the one thing the English file has to ask it. */
int tv_lang_utf8(const tvtts_synth *s);

/* English, src/port/tvtts.c. */
void       *en_create(uint32_t sample_rate);
void        en_destroy(void *s);
void        en_set_voice(void *s, int voice);
void        en_set_rate(void *s, int wpm);
void        en_set_pitch(void *s, int pitch);
void        en_set_volume(void *s, uint32_t volume);
int         en_get_voice(const void *s);
int         en_get_rate(const void *s);
int         en_get_pitch(const void *s);
uint32_t    en_get_rate_hz(const void *s);
int         en_set_rate_hz(void *s, uint32_t hz);
void        en_set_compat(void *s, int preformat, int textin, int terminators);
void        en_set_textin_mode(void *s, int mode);
void        en_set_extensions(uint32_t mask);
int         en_voice_count(void);
const char *en_voice_name(int voice);
int         en_voice_rate(int voice);
int         en_voice_pitch(int voice);
int         en_speak_bytes(void *s, const void *text, uint32_t len,
                           tvtts_callback cb, void *user);

/*
 * English's frame entry point, which is the layer below stage 3: 22 parameter
 * tracks per 10 ms frame, straight into the synthesiser.  Japanese is built on
 * it -- see lang/jpn/port/tvtts_ja.c -- so it is declared here rather than locally.
 */
/*
 * What the engine would SAY for this text, in its own one-character phoneme
 * alphabet.  snprintf-style: the return is the bytes wanted including the
 * terminator, and `buf` may be NULL with cap 0 to ask the size.
 *
 * The Japanese front end uses this to read a Latin word nothing knows --
 * `blorf` is ブローフ because the English engine says &BLg1F. and ja_g2p.c
 * adapts it.  It gets the trace by synthesising and discarding the audio, so
 * it ADVANCES the synthesiser it is given: the Japanese path keeps a separate
 * one for this and never asks the one it is speaking through.
 */
int         en_text_to_phonemes(void *s, const char *text,
                                char *buf, uint32_t cap);

int         en_speak_frames(void *s, const uint8_t *frames, uint32_t n_frames,
                            tvtts_callback cb, void *user);

/* Spanish, lang/spa/port/tvtts_es.c. */
void       *es_create(uint32_t sample_rate);
void        es_destroy(void *s);
void        es_set_voice(void *s, int voice);
void        es_set_rate(void *s, int wpm);
void        es_set_pitch(void *s, int pitch);
void        es_set_volume(void *s, uint32_t volume);
int         es_get_voice(const void *s);
int         es_get_rate(const void *s);
int         es_get_pitch(const void *s);
uint32_t    es_get_rate_hz(const void *s);
int         es_set_rate_hz(void *s, uint32_t hz);
void        es_set_compat(void *s, int preformat, int textin, int terminators);
void        es_set_textin_mode(void *s, int mode);
void        es_set_extensions(uint32_t mask);
int         es_voice_count(void);
const char *es_voice_name(int voice);
int         es_voice_rate(int voice);
int         es_voice_pitch(int voice);
int         es_speak_bytes(void *s, const void *text, uint32_t len,
                           tvtts_callback cb, void *user);

/*
 * Japanese, lang/jpn/port/tvtts_ja.c.  Not a decompilation and not an engine: the
 * front end is built from the phonetics literature and it drives the 1997
 * synthesiser through en_speak_frames.  See lang/jpn/port/ja.h.
 */
void       *ja_create(uint32_t sample_rate);
void        ja_destroy(void *s);
void        ja_set_voice(void *s, int voice);
void        ja_set_rate(void *s, int wpm);
void        ja_set_pitch(void *s, int pitch);
void        ja_set_volume(void *s, uint32_t volume);
int         ja_get_voice(const void *s);
int         ja_get_rate(const void *s);
int         ja_get_pitch(const void *s);
uint32_t    ja_get_rate_hz(const void *s);
int         ja_set_rate_hz(void *s, uint32_t hz);
void        ja_set_compat(void *s, int preformat, int textin, int terminators);
void        ja_set_textin_mode(void *s, int mode);
void        ja_set_extensions(uint32_t mask);
int         ja_voice_count(void);
const char *ja_voice_name(int voice);
int         ja_voice_rate(int voice);
int         ja_voice_pitch(int voice);
int         ja_speak_bytes(void *s, const void *text, uint32_t len,
                           tvtts_callback cb, void *user);

#endif
