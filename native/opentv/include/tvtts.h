/*
 * TruVoice as a library.
 *
 * A flat C API over the decompiled Centigram TruVoice engine, meant for
 * screen readers and anything else that wants the synthesizer without
 * SAPI: no COM, no registry, no window messages, no threads, and no audio
 * device.  The caller drives synthesis on its own thread and plays the
 * samples itself, which is what a screen reader wants -- it already owns
 * its output device, its ducking and its cancellation.
 *
 * The shape is deliberately close to eSpeak's: one callback receives audio
 * and index marks interleaved in stream order, and returning non-zero from
 * it aborts synthesis at once.
 *
 *   tvtts_synth *s = tvtts_create(11025);
 *   tvtts_set_voice(s, 0);
 *   tvtts_speak_utf8(s, "Hello world.", on_event, NULL);
 *   tvtts_destroy(s);
 *
 * Thread safety: a tvtts_synth is not thread safe; give each thread its
 * own.  tvtts_add_lexicon is process-global (the engine's user lexicon is
 * a single static table) and is not thread safe against anything.
 */
#ifndef TVTTS_H
#define TVTTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(TVTTS_SHARED)
#  ifdef TVTTS_BUILD
#    define TVTTS_API __declspec(dllexport)
#  else
#    define TVTTS_API __declspec(dllimport)
#  endif
#else
#  define TVTTS_API
#endif

/* Everything is cdecl, and the DLL exports undecorated names, so ctypes
 * and P/Invoke can bind it without a mangled-name dance. */
#define TVTTS_CALL

typedef struct tvtts_synth tvtts_synth;

enum {
    TVTTS_AUDIO = 0,   /* samples are ready */
    TVTTS_MARK  = 1,   /* synthesis reached an index mark */
    TVTTS_END   = 2    /* end of this utterance; no samples follow */
};

/*
 * One event.  Audio and marks arrive in stream order, so when a mark
 * arrives the caller has already been handed every sample that precedes
 * it: queue the audio, remember sample_pos, and raise the bookmark when
 * the play cursor passes it.
 *
 * Every field is 32 bits or smaller and the struct has no padding holes on
 * any normal ABI, so it binds from other languages without surprises.
 */
typedef struct {
    int32_t        type;        /* TVTTS_AUDIO / TVTTS_MARK / TVTTS_END */
    uint32_t       count;       /* TVTTS_AUDIO: number of samples */
    const int16_t *samples;     /* TVTTS_AUDIO: signed 16-bit mono, host order */
    uint32_t       mark;        /* TVTTS_MARK: the value from the escape */
    uint32_t       sample_pos;  /* samples emitted before this event */
} tvtts_event;

/* Return non-zero to abort synthesis immediately. */
typedef int (TVTTS_CALL *tvtts_callback)(const tvtts_event *ev, void *user);

/* rate is 11025 (the engine's native rate) or 8000.  NULL on failure. */
TVTTS_API tvtts_synth *TVTTS_CALL tvtts_create(uint32_t sample_rate);
/* The same, in a given language: "en", "es".  tvtts_create is this with the
 * first language the library carries, which is English. */
TVTTS_API tvtts_synth *TVTTS_CALL tvtts_create_lang(uint32_t sample_rate,
                                                    const char *lang);

/*
 * Output rate, as an index rather than a number of hertz, because the
 * engine has exactly three and they are not interchangeable with arbitrary
 * rates: each needs its own resonator tables.
 *
 *   TVTTS_SR_8K    8000 Hz, the original's narrowband set, for telephony
 *   TVTTS_SR_11K  11025 Hz, what TruVoice shipped as its desktop rate
 *   TVTTS_SR_16K  16000 Hz, which the original never offered
 *
 * 11 kHz is the default and is what people know the voices to sound like;
 * the other two are the same voices resampled by the synthesiser itself
 * rather than afterwards, so 16 kHz is genuinely more bandwidth and not an
 * upsample.  Its tables are OpenTV's own, computed from formulas that
 * reproduce both of the original's sets exactly.
 *
 * Changing rate re-initialises the filters and the output stage, so it is
 * refused part way through an utterance: call it between them.  Voice,
 * pitch, rate and volume are preserved across it.  Returns 0, or -1 if the
 * synth is null, the index is not one of the three, or an utterance is in
 * progress.
 */
#define TVTTS_SR_8K   0
#define TVTTS_SR_11K  1
#define TVTTS_SR_16K  2

TVTTS_API int TVTTS_CALL tvtts_set_sample_rate(tvtts_synth *s, int which);
TVTTS_API int TVTTS_CALL tvtts_get_sample_rate(const tvtts_synth *s);
/* The three output rates, as tvtts_set_sample_rate indexes them: 8000, 11025
 * and 16000.  The original offered the first two; the third is OpenTV's, and
 * every engine has it -- the tables for it are computed from the formulas that
 * reproduce both of the original's sets exactly, and those formulas hold for the
 * 1995 engines as well as the 1997 one. */
#define TVTTS_SR_8K   0
#define TVTTS_SR_11K  1
#define TVTTS_SR_16K  2
TVTTS_API uint32_t TVTTS_CALL tvtts_sample_rate_hz(int which);
TVTTS_API void TVTTS_CALL tvtts_destroy(tvtts_synth *s);

/*
 * Speak.  Blocks until the utterance finishes or the callback aborts it.
 * Returns 0 when it finished, 1 when the callback stopped it, negative on
 * error.  The synth is left ready for the next call either way.
 *
 * The engine reads cp1252, which is the encoding SAPI's WideCharToMultiByte
 * handed it; the utf8 and utf16 entry points convert the same way, so a
 * character the engine cannot represent becomes '?' exactly as it did then.
 * tvtts_speak_bytes takes cp1252 directly, for callers that already have it.
 */
TVTTS_API int TVTTS_CALL tvtts_speak_utf8(tvtts_synth *s, const char *text,
                                          tvtts_callback cb, void *user);
TVTTS_API int TVTTS_CALL tvtts_speak_utf16(tvtts_synth *s, const uint16_t *text,
                                           tvtts_callback cb, void *user);
TVTTS_API int TVTTS_CALL tvtts_speak_bytes(tvtts_synth *s, const void *text,
                                           uint32_t len, tvtts_callback cb,
                                           void *user);

/*
 * Phonemes, in the engine's own alphabet: one character per phoneme, with
 * "1" and "2" marking primary and secondary stress, so "hello" is "HeLO1".
 * The alphabet is the engine's internal one, not ARPABET and not the
 * bracket notation ("[HH AH L OW]") the rule interpreter also understands.
 *
 * This is the 5.1 builds' tts_SpeakPhoneme, which is a wrapper that puts
 * ESC[1I in front of the string and ESC[0I after it and speaks the result;
 * the escape is in this engine too.  Round-tripping is close but not exact:
 * speaking the phonemes tvtts_text_to_phonemes gives for "hello" is byte
 * for byte the same as speaking the word, while for some other words the
 * final intonation differs slightly.  See docs/VOICES.md.
 */
/*
 * Drive the synthesiser directly, one parameter frame per 10 ms.
 *
 * `frames` is `n_frames` groups of TVTTS_FRAME_TRACKS bytes, one group per
 * frame, in the track order documented in docs/VOICES.md: 0..8 amplitudes and
 * source controls, 9..12 the formant frequencies F1..F4, 13..17 bandwidths with
 * 17 the pitch period, 18..21 source parameters.  A value is clamped by the
 * engine the same way the escape path clamps it.
 *
 * This bypasses the text front end and stages 0 to 3 completely: nothing is
 * read, no phoneme is looked up, and the frames go where stage 3 would have put
 * them.  It is the way to synthesise something the front end has no notion of
 * -- a language it was never taught, or a held note -- without splicing
 * separate utterances together, which restarts the glottal phase at every join.
 *
 * English only, as the phoneme entry points are.  Returns 0, or non-zero if the
 * callback aborted.
 */
#define TVTTS_FRAME_TRACKS 22
TVTTS_API int TVTTS_CALL tvtts_speak_frames(tvtts_synth *s,
                                            const uint8_t *frames,
                                            uint32_t n_frames,
                                            tvtts_callback cb, void *user);

TVTTS_API int TVTTS_CALL tvtts_speak_phonemes(tvtts_synth *s,
                                              const char *phonemes,
                                              tvtts_callback cb, void *user);

/*
 * Singing.  A score is phonemes in that same alphabet, each optionally
 * followed by how long to hold it and what to sing it on:
 *
 *     PHONEME<duration_ms,pitch>
 *
 * so `H<100,20>e<600,20>L<100,20>O<1500,20>` is "hello" sung on G3.  A phoneme
 * with no `<...>` is spoken at its ordinary length, so a score can mix the two.
 *
 * The pitch scale is DECtalk's: 0 rests, 1 to 37 is a chromatic scale from C2
 * to C5, and 38 upwards is hertz directly, which is what allows a glide.
 * tvtts_note_hz converts one, and is exposed because a caller writing a score
 * generally wants to know what it will get.
 *
 * Requires TVTTS_EXT_SING, which is on by default.  English only.  A note
 * longer than about 600 ms is sung as repeats of its phoneme, which is the
 * track window's doing rather than a choice -- see docs/SINGING.md.
 */
TVTTS_API int TVTTS_CALL tvtts_sing(tvtts_synth *s, const char *score,
                                    tvtts_callback cb, void *user);
TVTTS_API int TVTTS_CALL tvtts_note_hz(int note);

/*
 * The score compiler on its own: writes the escape string tvtts_sing would
 * speak, snprintf-style, so the return is the bytes wanted including the
 * terminator and a short buffer truncates without losing that count.
 */
TVTTS_API int TVTTS_CALL tvtts_sing_compile(const char *score,
                                            char *buf, size_t cap);

/*
 * The other direction: what the engine would say for this text, in that
 * same alphabet.  Written NUL-terminated into buf, snprintf-style -- the
 * return is the number of bytes needed including the terminator, so a
 * short buffer is truncated but the return still says how much was wanted,
 * and buf may be NULL (with cap 0) purely to ask the size.  Returns -1 if
 * the arguments are bad or it ran out of memory.
 *
 * The engine only produces the trace as a side effect of synthesising, so
 * this runs the text through and throws the audio away: it costs what
 * speaking would, and it advances the synth exactly as a tvtts_speak call
 * does.  Settings are left alone.
 */
TVTTS_API int TVTTS_CALL tvtts_text_to_phonemes(tvtts_synth *s,
                                                const char *text,
                                                char *buf, uint32_t cap);

/*
 * Index marks.  The engine takes them inline in the text, so a caller with
 * a sequence of (text, bookmark) pieces writes the escape between them.
 * Writes at most 16 bytes including the terminator and returns the length,
 * or 0 if the buffer is too small.
 */
TVTTS_API int TVTTS_CALL tvtts_mark_sequence(char *buf, size_t cap, uint32_t mark);

/*
 * A pause, for the same reason: the engine takes one inline, in hundredths
 * of a second, biased by 49.  Rounded down to a hundredth and clamped to
 * the 2060 ms the engine can express.  Writes at most 16 bytes including
 * the terminator and returns the length, or 0 if the buffer is too small.
 */
TVTTS_API int TVTTS_CALL tvtts_break_sequence(char *buf, size_t cap, uint32_t ms);

/*
 * Speak punctuation rather than pause on it: with this on, the comma and
 * period of "Hi, there." are said aloud.  It is a flag, so it stays on
 * until turned off -- including into later utterances on the same synth.
 *
 * It does not spell anything.  There is no need to: the engine already
 * names a letter given on its own, so "A" alone says "ay" rather than the
 * article.  What loses that is putting any escape after the letter, which
 * makes it no longer the last word -- so a caller wanting a letter named
 * should end the text with the letter.
 */
TVTTS_API int TVTTS_CALL tvtts_punctuation_sequence(char *buf, size_t cap, int on);

/*
 * Pitch and rate part way through an utterance, for the prosody a caller
 * puts on a capital letter or an emphasised word.  Both take the same units
 * as tvtts_set_pitch and tvtts_set_rate and clamp to what the engine can
 * usefully do: pitch 50..400, rate 46..253 words per minute (see
 * TVTTS_RATE_MIN below for where those two numbers come from).
 */
TVTTS_API int TVTTS_CALL tvtts_pitch_sequence(char *buf, size_t cap, int pitch);
TVTTS_API int TVTTS_CALL tvtts_rate_sequence(char *buf, size_t cap, int wpm);

/*
 * The engine's rate is a 26-row table, picked by (wpm - 46) / 8, so it
 * takes 46..253 words per minute in steps of eight -- and only those.
 *
 * Below 46 the subtraction is done unsigned and wraps, giving an index
 * of about 0x1fffffff and a wild read: the original crashes there and so
 * does this, so tvtts_set_rate raises anything lower to 46.  Above 253
 * the index runs past the end of the table and the engine reads whatever
 * follows it, which sounds nothing like speeding up -- a sentence at 254
 * comes out ten times longer than at 253.  That is the original's own
 * behaviour, reproduced exactly, so it is left alone rather than clamped;
 * callers that want speech rather than fidelity should stay inside the
 * range.  46..76 all select the slowest row.
 */
#define TVTTS_RATE_MIN 46
#define TVTTS_RATE_MAX 253

/*
 * With TVTTS_EXT_RATE on -- which is the default -- the ceiling moves to
 * TVTTS_RATE_MAX_EXT.  The original's 26 rows are untouched, so 46..253
 * sounds exactly as it always did; above that OpenTV shortens durations
 * instead, which the original never did because it had no rows there at
 * all.  It tops out around three and a half times the speed of 150 wpm,
 * where the per-phoneme minimum durations in g_phone_dur stop it going any
 * further.
 */
#define TVTTS_RATE_MAX_EXT 400

/*
 * OpenTV extensions: behaviour this project adds that the 1997 engine did
 * not have.  All are on by default.  Clearing them gives the original
 * exactly, which is what the test corpus runs so that its byte-for-byte
 * comparison keeps proving the decompilation correct.
 *
 * Process-wide rather than per-synth, like tvtts_add_lexicon: the engine's
 * own Stage 2 keeps its working state in globals, so one synth was never
 * independent of another here.
 */
/*
 * TVTTS_EXT_RATE: rate above the table actually speeds up.
 *
 * Both engines turn words per minute into a table row with (wpm - 46) >> 3 and
 * then read off the end of the table above it, which is nonsense in each but
 * differently shaped nonsense.  English gets slower in steps that stop meaning
 * anything; the Spanish engine feeds three tables of different lengths -- 26,
 * 24 and 16 rows -- so at 254 wpm it comes out longer than at 253, and by 300
 * it says a whole sentence in a tenth of a second.
 *
 * With the flag on, each table is held inside its own length and durations are
 * scaled down past the last row instead, to 40% by 400 wpm.  Every row the
 * table really has is untouched, so English is unchanged to 253 wpm and Spanish
 * to 237 -- the latter is lower only because its phoneme table is two rows
 * shorter.
 */
#define TVTTS_EXT_RATE  0x1u

/*
 * TVTTS_EXT_CLARITY widens the formant bandwidths as the rate climbs, which
 * is what keeps fast speech from slurring: a narrow resonator rings for
 * longer than a shortened phoneme lasts, so its energy smears into the next
 * one.  A wider bandwidth settles sooner and each phoneme keeps its own
 * identity.
 *
 * It does nothing at or below 253 wpm, so the voices are unchanged
 * everywhere the original could reach; it only shapes the range OpenTV
 * added.  The approach and its constants come from Tamas Geczy's
 * TGSpeechBox (MIT) -- see NOTICE.
 */
#define TVTTS_EXT_CLARITY 0x2u

/*
 * TVTTS_EXT_PITCH lifts the Spanish engine's pitch ceiling to the English
 * engine's.  Stage 2 clamps every node's pitch before storing it, and the 1995
 * engine clamps to 50..200 where the 1997 one clamps to 50..500 -- but both
 * DLLs ship the same ten-voice table, in which Carlos is 203 and Josefa 208.
 * Those two voices are above their own engine's ceiling, so every node of their
 * contour is pinned to it and they speak in a monotone; the rest lose the top of
 * their range wherever the contour reaches for it.
 *
 * What the flag changes is one constant, to the value the other generation of
 * the same engine uses, and the byte the pitch is stored in has always had the
 * room: it holds half the pitch, so 500 is what fits and 200 never needed the
 * limit.  Nothing here is invented -- it is the 1997 engine's own number.
 *
 * In English the same flag takes that ceiling the last of the way, from 500 to
 * **510**, which is where the storage actually stops: the pitch goes into a
 * byte as half of itself, so 255 is the last value there is.  500 was the round
 * number, not the limit.  Measured on a held tone, the escape rises cleanly to
 * 255 and sings 501 Hz, where it used to stop at 486; 256 wraps, and badly --
 * it reads as 82 Hz.  The command ceiling moves with it.
 *
 * This is the top of a singing scale.  It does not reach C5, and nothing in a
 * byte can: 523 Hz needs 262.  A sung note gets its pitch from `ESC[<lo>;<hi>q`
 * instead, which is quarter-hertz and below the byte, so the whole scale is in
 * tune whatever this clamp says; see docs/SINGING.md.
 */
#define TVTTS_EXT_PITCH 0x4u

/*
 * TVTTS_EXT_CONTOUR keeps the Spanish engine's intonation as the pitch moves.
 *
 * That engine builds its pitch contour by adding fixed numbers of hertz to the
 * base pitch, so the excursion is an interval only at the pitch the voice was
 * tuned at.  Raise the pitch and the same few hertz shrink to nothing: at 400
 * the contour spans 2.7 semitones where at 160 it spanned 5.8, and the voice
 * goes flat while keeping its speed and timbre.  The 1997 English engine takes
 * its excursion as a fraction of the pitch instead, so its intonation holds
 * wherever the voice is put.
 *
 * With this on, the excursion the Spanish engine works out is scaled by the
 * pitch against a fixed reference of 85 -- the pitch at which that engine's
 * contour and the 1997 engine's already agree, measured rather than chosen.
 * The reference is one number for every voice, because neither engine's contour
 * depends on which voice is speaking, so at a given pitch they all stay equally
 * inflected.  At 85 itself nothing changes at all.  It does nothing to English,
 * which never had the problem.
 */
#define TVTTS_EXT_CONTOUR 0x8u

/*
 * TVTTS_EXT_FLOOR lets the Spanish engine reach the bottom of its range.
 *
 * That engine treats a pitch track under 60 as an error and substitutes 65 --
 * a low C, 65.41 Hz, which is audibly where its bottom stops.  The 1997 English
 * engine has no such check, and nothing needs it: Stage 2 has already clamped
 * the track to 50, which is the floor English works down to and a period the
 * same arithmetic handles there.
 *
 * It matters most for the lowest voice of a language.  Jorge and Sidney both
 * sit at pitch 50, and without this only Sidney can get there; Jorge stops a
 * fifth short and loses the bottom of every fall.  It does nothing to English.
 */
#define TVTTS_EXT_FLOOR 0x10u

/*
 * TVTTS_EXT_SING enables the singing commands, which the original engine has
 * no equivalent for: ESC[<n>d holds the next phoneme for n hundredths of a
 * second, ESC[<lo>;<hi>q names a pitch in quarter-hertz, and the pitch escape
 * reaches the engine's own ceiling rather than stopping at 400.  Nothing
 * happens until a score asks for it, so this costs an ordinary utterance
 * nothing.
 *
 * It also enables **`[:phone TruVoice on]`** and **`[:phone TruVoice off]`**,
 * written in the text itself, which turn the engine's phoneme input on and off
 * -- the same thing tvtts_speak_phonemes does by wrapping a string in
 * ESC[1I and ESC[0I, but reachable from inside a document.  The spelling is
 * that product's; its own demo writes `[:phone arpa TruVoice]`, and the `arpa`
 * is a fiction, since these are one-character phonemes rather than two-letter
 * ARPABET names.  Case and spacing are free, and anything that is not exactly
 * this command is left alone, byte for byte.
 *
 * The command reaches the engine from this library, from `tv`, and from the
 * speak window.  It does not reach it through NVDA or the SAPI driver, both of
 * which strip inline commands out of the text first.
 *
 * See tvtts_sing and docs/SINGING.md.
 */
#define TVTTS_EXT_SING  0x20u

/*
 * TVTTS_EXT_CAMEL reads a word written in camel case as the words it is made
 * of: `CamelCase` is said as `Camel Case`, `helloWorld` as `hello World`, and
 * `HTMLParser` as `HTML Parser`.
 *
 * The engine has no notion of it.  A capital inside a word reaches the
 * letter-to-sound rules and changes nothing: `CamelCase` renders byte for byte
 * what `camelcase` renders, so the two words run together as one.  NVDA splits
 * such words before they ever reach a driver, which is why the add-on has
 * always sounded right and the speak window did not; this puts the same
 * behaviour under every front end at once.
 *
 * The rule is the conventional one, on ASCII letters and digits: a space goes
 * before a capital that follows a lowercase letter or a digit, and before a
 * capital that follows another capital and is itself followed by a lowercase
 * letter -- that second case is what keeps `HTMLParser` from becoming
 * `H T M L Parser`.  An all-capitals word is left alone.
 *
 * Text only.  tvtts_speak_phonemes and tvtts_sing are not touched, because a
 * capital means something else in the phoneme alphabet.
 */
#define TVTTS_EXT_CAMEL 0x40u

/*
 * TVTTS_EXT_JA_ROMAJI reads a Latin word in Japanese as ROMAJI rather than as
 * English.  It is OFF by default, and the default is the interesting part.
 *
 * `take` is both the Japanese タケ and the English テイク, and nothing in the
 * spelling says which was meant.  Three shipping Japanese systems were
 * listened to -- Google, Microsoft OneCore and ETI-Eloquence -- and all three
 * read a bare `take` as テイク: a Latin word is foreign text, looked up and
 * then guessed at with English rules.  So that is the default here, and
 * `mouse` is マウス rather than モーセ, `fire` ファイア rather than フィレ.
 *
 * Turn this on and an utterance that parses CLEANLY as romaji -- every letter
 * consumed, nothing in the dictionary -- is read as Japanese instead:
 * `konnichiwa` is コンニチワ` and not コニチーワ.  That is what somebody who
 * cannot type kana wants, and it is why the behaviour is kept rather than
 * removed.  It costs the English reading of every short Latin word that
 * happens to be parseable as romaji, which is most of them.
 *
 * Either way the DICTIONARY wins first, so `Windows` is ウィンドーズ and
 * `Amazon` アマゾン with its own accent, and an initialism is still spelled:
 * `NVDA` is エヌブイディーエー.  Japanese only; the other languages ignore it.
 */
#define TVTTS_EXT_JA_ROMAJI 0x80u

#define TVTTS_EXT_ALL   0xffu

/*
 * What is on when nothing has said otherwise: everything except the romaji
 * reading, which is a CHOICE rather than a fix and whose other side is the
 * one three shipping systems make.  TVTTS_EXT_ALL turns it on along with the
 * rest, which is what a caller asking for "all of it" should get.
 */
#define TVTTS_EXT_DEFAULT (TVTTS_EXT_ALL & ~TVTTS_EXT_JA_ROMAJI)

/*
 * How a sung note wavers, both in hundredths of a hertz: the rate, and the
 * depth either side of the note.  The defaults are DECtalk's, 625 and 205 --
 * 6.25 Hz and plus or minus 2.05 Hz -- which is what the demo this engine
 * shipped with was sung with.
 *
 * A depth of 0 turns it off, which is how it sang before this existed.  The
 * depth is in hertz rather than a fraction of the note, so it is worth about
 * 35 cents at the bottom of the scale and 7 at the top; that is DECtalk's
 * choice and it is kept.  Process-wide, like the extensions.
 *
 * It applies to a note of the scale and not to a pitch written out in hertz,
 * because that is what a glide is written with and a glide does not waver.
 */
/*
 * How long a sung note takes to arrive, in milliseconds -- the slide from the
 * note before it.  DECtalk walks to a note over 100 ms; the default here is 70,
 * a little quicker.  0 steps straight to the note, which sounds switched rather
 * than sung.  Process-wide, like the extensions.
 *
 * A pitch written out in hertz is not affected: that is what a written glide
 * uses, and it travels over its whole phoneme however this is set.
 */
TVTTS_API void TVTTS_CALL tvtts_set_portamento(int ms);
TVTTS_API int TVTTS_CALL tvtts_get_portamento(void);

TVTTS_API void TVTTS_CALL tvtts_set_vibrato(int rate_chz, int depth_chz);
TVTTS_API void TVTTS_CALL tvtts_get_vibrato(int *rate_chz, int *depth_chz);

TVTTS_API void TVTTS_CALL tvtts_set_extensions(uint32_t mask);
TVTTS_API uint32_t TVTTS_CALL tvtts_get_extensions(void);

/*
 * Pitch, in the engine's own units.  Stage 2 clamps every node it emits
 * to 50..500 and then stores the value halved in a byte, so 50..500 is
 * exactly what the engine can represent -- 500 is the largest pitch whose
 * half still fits.
 *
 * Outside it the base pitch is not ignored, because it is arithmetic on
 * the way in rather than the clamped value: a base below 50 still lifts
 * the accented nodes, and the audio keeps changing down to about 28 and
 * up to about 516 before it saturates.  None of that is a pitch the
 * engine can hold, though, so a caller wanting the range should use
 * these.  tvtts_set_pitch does not enforce them: the corpus checks the
 * original's behaviour at 40, which is outside.
 *
 * The inline escape is narrower still.  ESC[<n>p takes n 25..200 and
 * doubles it (preformat.c), so tvtts_pitch_sequence reaches 400 and no
 * further, whatever tvtts_set_pitch has been given.
 */
#define TVTTS_PITCH_MIN 50
#define TVTTS_PITCH_MAX 500

/* Settings.  These persist across utterances, as SAPI's did. */
TVTTS_API void TVTTS_CALL tvtts_set_voice(tvtts_synth *s, int voice);
TVTTS_API void TVTTS_CALL tvtts_set_rate(tvtts_synth *s, int wpm);
TVTTS_API void TVTTS_CALL tvtts_set_pitch(tvtts_synth *s, int pitch);
TVTTS_API void TVTTS_CALL tvtts_set_volume(tvtts_synth *s, uint32_t volume);

TVTTS_API int TVTTS_CALL tvtts_get_voice(const tvtts_synth *s);
TVTTS_API int TVTTS_CALL tvtts_get_rate(const tvtts_synth *s);
TVTTS_API int TVTTS_CALL tvtts_get_pitch(const tvtts_synth *s);

/* The voices, in the order the engine indexes them.  With more than one
 * language in the library they run on: English 0..9, then the next language's,
 * and tvtts_voice_language says which language a voice belongs to. */
TVTTS_API int TVTTS_CALL tvtts_voice_count(void);
TVTTS_API const char *TVTTS_CALL tvtts_voice_name(int voice);

/* ---- languages ----------------------------------------------------------- */
/*
 * The library carries one engine per language it has been taught, and a synth
 * speaks one of them at a time.  They are not variants of one engine: TruVoice
 * shipped five language DLLs and the 1995 four are a different generation from
 * the 1997 English one, so each is its own decompilation with its own object
 * layout.  What they share is this interface.
 *
 * A language is named by its two-letter code -- "en", "es" -- and the voices
 * are numbered across all of them, so a caller that only wants a list of
 * voices can ignore languages entirely and ask tvtts_voice_language which one
 * each voice speaks.
 *
 * Changing language builds a new engine and throws the old one away, so it
 * cannot be done part way through an utterance and returns -1 if tried.  The
 * settings that belong to the caller rather than to the engine -- rate, pitch,
 * volume, sample rate -- are carried across; the voice becomes the first one
 * of the new language, because a voice number means nothing outside its own.
 */
TVTTS_API int TVTTS_CALL tvtts_language_count(void);
TVTTS_API const char *TVTTS_CALL tvtts_language(int index);
/*
 * A language's name for a person to read: "American English".  Takes the code,
 * so it composes with tvtts_voice_language, and returns NULL for a code the
 * library does not carry.  The names come from what each DLL says it is -- its
 * version resource gives a LANGID, 0x0409 for CGRM_EN and 0x040a for CGRM_ES --
 * rather than from anybody's idea of what the voices sound like.
 */
TVTTS_API const char *TVTTS_CALL tvtts_language_name(const char *code);
TVTTS_API const char *TVTTS_CALL tvtts_voice_language(int voice);
TVTTS_API const char *TVTTS_CALL tvtts_get_language(const tvtts_synth *s);
TVTTS_API int TVTTS_CALL tvtts_set_language(tvtts_synth *s, const char *lang);

/* The engine's default rate and pitch for a voice, as SAPI reported them. */
TVTTS_API int TVTTS_CALL tvtts_voice_rate(int voice);
TVTTS_API int TVTTS_CALL tvtts_voice_pitch(int voice);

/*
 * Add a word to the user lexicon, as ITTSDialogs' lexicon editor did.
 * Process-global: it affects every synth in the process, including ones
 * created later.  Returns 0 on success.
 */
TVTTS_API int TVTTS_CALL tvtts_add_lexicon(const char *word, const char *phonemes);

/*
 * Reproduction knobs, for callers that need to match the original exactly.
 *
 * preformat and textin are the two front-end passes the original let you
 * turn off through the registry.  terminators is how many NUL bytes SAPI
 * put after the text; the feed routine branches on the total length, so it
 * changes the output.  The defaults (1, 1, 2) are what SAPI produced.
 */
/*
 * Which tokenizer the text goes through, as SAPI's "TextInMode" chose.  0 is
 * the ordinary one and what every caller wants; 4 is the mail reader, which
 * reads From:, Subject:, Date:, Cc: and Bcc: out of a message and announces
 * quoted passages.  The engine builds its tokenizer when the synth is made, so
 * this has to be set before the first utterance; it is here because the
 * original had it and the corpus covers it, not because a screen reader wants
 * it.
 */
TVTTS_API void TVTTS_CALL tvtts_set_textin_mode(tvtts_synth *s, int mode);

TVTTS_API void TVTTS_CALL tvtts_set_compat(tvtts_synth *s, int preformat,
                                           int textin, int terminators);

#ifdef __cplusplus
}
#endif

#endif
