/*
 * Japanese: the front end, in C.
 *
 * This is a translation of the Python in build/Japanese_test, which is where the
 * phonetics was worked out and where every number in ja_tables.c is sourced.
 * Read the Python's comments for WHY a value is what it is; this side is
 * deliberately only the HOW, so the two cannot drift apart in their reasoning.
 *
 * Japanese is not like English and Spanish here.  Those are decompilations of
 * Centigram DLLs and the test is byte-identical audio against the original.
 * There is no Japanese TruVoice, so there is nothing to be byte-exact against
 * -- but there IS something to be byte-exact against on the way in: the Python
 * prototype, whose output the project's listener has signed off on.  So
 * oracle_frames.tsv holds 533 words as parameter frames and tests/ja_oracle.c
 * requires this code to reproduce every byte of it.  That turns a 2,500-line
 * translation from an act of faith into a check.
 *
 * Because of that the arithmetic here is not "equivalent to" the Python's, it
 * is the SAME arithmetic: doubles where Python has floats, ja_round() where
 * Python has round(), truncation where Python has int().  Anywhere those part
 * company the oracle fails and says which word and which frame.
 *
 * What this language does NOT have is its own engine.  It builds the 22-track
 * parameter frames the 1997 synthesiser already reads and hands them to
 * en_speak_frames, so nothing below the frame array is Japanese-specific and
 * src/engine stays untouched.
 */
#ifndef JA_H
#define JA_H

#include <stdint.h>
#include <stddef.h>

#define JA_NTRACK 22
#define JA_SR_DEFAULT 16000

/* ---- vowels -------------------------------------------------------------- */

enum { JA_V_A = 0, JA_V_I, JA_V_U, JA_V_E, JA_V_O, JA_NV };
#define JA_V_NONE (-1)

/* ---- consonants ---------------------------------------------------------- */

/*
 * Index 0 is the empty onset, which MANNER really has a row for ('none'), so a
 * bare vowel needs no special case.  The last four are not onsets at all: they
 * are the places the moraic nasal assimilates to, which moraic_n() returns and
 * oral() reads.
 */
enum {
    JA_C_NONE = 0,
    JA_C_K, JA_C_KY, JA_C_G, JA_C_GY,
    JA_C_T, JA_C_D,
    JA_C_P, JA_C_PY, JA_C_B, JA_C_BY, JA_C_V,
    JA_C_S, JA_C_SH, JA_C_Z, JA_C_J, JA_C_CH, JA_C_TS,
    JA_C_H, JA_C_HY, JA_C_F,
    JA_C_N, JA_C_NY, JA_C_M, JA_C_MY,
    JA_C_R, JA_C_RY,
    JA_C_W, JA_C_Y,
    JA_C_MN,                    /* the moraic nasal, LOCUS/MANNER key 'N' */
    JA_C_N_N, JA_C_N_M, JA_C_N_K, JA_C_N_Q,
    JA_NC
};

/*
 * What onset_of() answers when the thing it was asked about is not a CV mora.
 * The Python passes around the mora STRING here, so `nxt_c` can be ' ', '|',
 * 'Q', ':' or None as well as a consonant, and three separate tests in build()
 * depend on telling those apart.
 */
#define JA_C_QMARK  (-2)        /* the next mora is 'Q' */
#define JA_C_LONGM  (-3)        /* the next mora is ':' */
#define JA_C_BOUND  (-4)        /* ' ', '|' or '||' */
#define JA_C_NOMORA (-5)        /* there is no next mora (Python's None) */
/*
 * The place the moraic nasal takes where there is nothing to assimilate to --
 * before a vowel, a glide, /h/ or another special mora.  It is a nasalised
 * vowel rather than a closure, so it needs no locus row and is a pseudo-code
 * rather than an index: ja_oral_post handles it directly.
 */
#define JA_C_N_NAS  (-6)

/* ---- morae --------------------------------------------------------------- */

enum {
    JA_M_CV = 0,                /* c may be JA_C_NONE; v is always set */
    JA_M_N,                     /* the moraic nasal */
    JA_M_Q,                     /* the first half of a geminate */
    JA_M_LONG,                  /* the length mark */
    JA_M_SP,                    /* accent-phrase boundary: pitch, no pause */
    JA_M_BAR,                   /* major phrase: a pause */
    JA_M_BARBAR                 /* clause: a longer pause */
};

typedef struct {
    unsigned char kind;
    signed char   c;
    signed char   v;
} ja_mora;

/* text -> morae.  The bytes are decoded as UTF-8, with any invalid byte taken
 * as the Latin-1 character of the same value, so plain romaji and kana both go
 * in unannounced.  Returns the count written, at most cap. */
int ja_to_morae(const char *text, size_t len, ja_mora *out, int cap);

/*
 * The same, reporting how many characters it could not place.
 *
 * The parser is LENIENT by design -- it is fed generated romaji from the kana
 * path and must not reject a stray mark -- and that leniency is silent.  Fed
 * an English word it drops the letters Japanese has no mora for, so `hello`
 * came out /he.Q.o/ and `blorf` came out as the single mora /o/.  `skipped`
 * is what lets a caller tell deliberate romaji from a Latin word that merely
 * survived the filter: `sakura` consumes whole, `computer` does not.
 */
int ja_to_morae_ex(const char *text, size_t len, ja_mora *out, int cap,
                   int *skipped);

/*
 * Romaji in, KATAKANA out, with the same skip count.  The rest of the
 * pipeline wants a katakana pronunciation -- that is what the mora splitter,
 * the devoicing stage and the mora count read -- so this is what lets a Latin
 * token the dictionary does not know be re-read as Japanese.  The map is
 * derived from the mora inventory rather than written out; see the function.
 * Returns the bytes wanted including the NUL, or -1.
 */
int ja_romaji_to_kana(const char *text, size_t len, char *out, size_t cap,
                      int *skipped);

int ja_onset_of(const ja_mora *m);
int ja_is_boundary(const ja_mora *m);

/* A mora back to the symbol the Python front end names it by -- 'N', 'Q',
 * ':', ' ', '|', '||' and the CV romaji.  Not the romaji this file PARSES;
 * see the function. */
int ja_mora_symbol(const ja_mora *m, char *buf, size_t cap);

/*
 * And back: a mora's symbol -> its katakana, or NULL for one the inventory
 * has none for.  Needed because serialising morae to romaji and re-parsing
 * them re-applies the parser's conventions -- `aa` becomes a long vowel --
 * so a resolved symbol must not be parsed again.  See the function.
 */
const char *ja_mora_kana(const char *sym);

/* ---- postures ------------------------------------------------------------ */

/*
 * F1 F2 F3 F4 B1 B2 B3, in hertz.  Doubles rather than ints because _lerp and
 * the F3 bridge produce halves, and rounding them early would change the
 * frames.
 */
typedef double ja_post[7];

/*
 * A frame's source overrides -- the Python's `src` dict.  A sparse map is what
 * the code actually wants, because it copies one (dict(SIBIL)) and then sets a
 * single track in the copy.
 */
typedef struct {
    uint32_t mask;
    int16_t  val[JA_NTRACK];
} ja_src;

/*
 * CPython's float.__round__ with no digits: C round(), then round-to-even on
 * an exact halfway case.  Not the same function as C's round(), and the
 * difference shows up wherever a duration lands on half a frame.
 */
double ja_round(double x);

void ja_src_clear(ja_src *s);
void ja_src_set(ja_src *s, int track, int value);

/* ---- tables (ja_tables.c) ------------------------------------------------ */

enum { JA_MAN_NONE = 0, JA_MAN_STOP, JA_MAN_FRIC, JA_MAN_AFFRIC,
       JA_MAN_NASAL, JA_MAN_FLAP, JA_MAN_GLIDE };

typedef struct {
    unsigned char manner, voiced;
    short         hold, vot;
} ja_manner;

extern const double    ja_V[JA_NV][7];
extern const short     ja_LOCUS[JA_NC][JA_NV];        /* 0 = no row */
extern const ja_manner ja_MANNER[JA_NC];
extern const short     ja_VOT_MEDIAL[JA_NC];          /* 0 = not in the table */
extern const short     ja_CLOSURE_MEDIAL[2];
extern const short     ja_BURST_PEAK[JA_NC][JA_NV];   /* 0 = no row */
extern const short     ja_BURST_BW[JA_NC];
extern const double    ja_SIB_POST[JA_NC][7];         /* [0] == 0 = no row */
extern const short     ja_VOICED_F2[JA_NC][JA_NV];    /* 0 = no row */
extern const short     ja_NASAL_ORAL[JA_NC][JA_NV];   /* 0 = no row */
extern const double    ja_LONG_DELTA[JA_NV][3];
extern const double    ja_V_DUR[JA_NV][2];
extern const short     ja_NOISE_TRIM_UNITS[JA_NC];
extern const short     ja_ASPIR_TRIM_C[JA_NC];        /* keyed by consonant */
extern const short     ja_ASPIR_TRIM_V[JA_NV];        /* the 'V'+vowel keys */

/* Ruddell's four realizations of /f/: track-2 peak, B2 onset, B2 held, B3. */
enum { JA_PHI_F = 0, JA_PHI_P, JA_PHI_PH, JA_PHI_H };
extern const short ja_PHI[4][4];
#define JA_PHI_BAND 1700

extern const ja_src ja_SRC_ASPIR, ja_SRC_NASAL_A, ja_SRC_DEVOICED,
                    ja_SRC_VOICE_BAR, ja_SRC_SIBIL, ja_SRC_SIBIL_S,
                    ja_SRC_SIBIL_Z, ja_SRC_BURST_T, ja_SRC_BURST_D;

int ja_has_burst_src(int c);
const ja_src *ja_burst_src(int c);

/* Arai's flap, mode (b).  The locus rows are precomputed into ja_LOCUS. */
#define JA_FLAP_AVD 25
#define JA_FLAP_AVG 3
#define JA_FLAP_FG  8
#define JA_FLAP_F1  500

/* ---- the voice functions (ja_tables.c) ---------------------------------- */

int  ja_vowel_post(int v, int ctx_c, int longv, ja_post out);
int  ja_onset_post(int c, int v, ja_post out);
int  ja_coda_post(int c, int pv, ja_post out);
int  ja_oral_post(int c, int v, ja_post out);
int  ja_burst_post(int c, int v, ja_post out);
int  ja_noise_post(int c, int v, ja_post out);
int  ja_phi_post(int v, ja_post out);
int  ja_phi_mode(int v, int nxt_c, int devoiced);
int  ja_moraic_n(int nc, int final_);
void ja_voice_bar_post(const ja_post in, ja_post out);

/* ---- building (ja_frame.c) ---------------------------------------------- */

typedef struct {
    uint8_t *frames;            /* n * JA_NTRACK bytes */
    int      n;
    double  *ends;              /* one per mora, in seconds */
    int      n_ends;
    double  *q_ends;            /* geminate closure ends, in seconds */
    int      n_q;
} ja_utt;

typedef struct {
    int    sr;                  /* output rate; only the noise trim reads it */
    int    engine_voice;        /* source calibration; stock engine index */
    double rate_scale;          /* 1.0 at the default rate; see tvtts_ja.c */
    double fb;                  /* baseline F0 in hertz */
    int    question;
    int    accent;              /* Tokyo accent type for the whole utterance */
    /*
     * What the analyser supplies when there is one, and what the kana path
     * leaves NULL.  `accents` is one accent type per accent phrase, in order,
     * which is more than `accent` above can say: that applies a single type to
     * the first phrase and leaves the rest heiban, which is as much as text
     * alone knows.  `devoiced` is one flag per mora, and it overrides the
     * frame builder's own rule -- the generalisation over the mora string is
     * measurably coarser than the lexicon, which blocks two adjacent devoiced
     * morae and protects the one carrying the accent nucleus.
     */
    const int *accents;
    int        n_accents;
    const int *devoiced;
    int        n_devoiced;
} ja_opts;

void ja_opts_default(ja_opts *o);
void ja_voice_noise(uint8_t *t, int voice, int sr, int c, int v);

/* Build and pitch one utterance.  Returns 0, or -1 out of memory. */
int  ja_build(const ja_mora *morae, int n_morae, const ja_opts *o, ja_utt *u);
void ja_utt_free(ja_utt *u);

/* ---- pitch (ja_pitch.c) ------------------------------------------------- */

/* Writes track 17 of every frame.  `hz` may be NULL; when it is not it takes
 * u->n doubles and receives the contour, which is what the diagnostics want. */
int  ja_pitch(ja_utt *u, const ja_mora *morae, int n_morae, const ja_opts *o,
              double *hz);

#endif
