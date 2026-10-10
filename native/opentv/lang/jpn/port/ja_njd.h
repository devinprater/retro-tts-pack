/*
 * Japanese: the morphological front end, in C.
 *
 * This is the half that was missing.  ja_mora.c and ja_frame.c take morae and
 * build parameter frames, which is everything BELOW the text analysis; what
 * reaches them has until now been kana or romaji, because supplying a kanji
 * reading and an accent type needs a morphological analyser over a dictionary
 * and there wasn't one in C.
 *
 * So: a Viterbi over naist-jdic (ja_dict.c), then Open JTalk's five rule
 * stages (ja_njd.c and ja_digit.c), then the same mora list the kana path
 * produces (ja_front.c).  The stages are translated from
 * build/Japanese_test/jp_njd.py and jp_digit.py, which were themselves read
 * from Open JTalk's sources as a specification; no Open JTalk code is copied.
 * Read the Python for WHY a rule is what it is, and read the generated
 * ja_ojt.c for the tables.  Open JTalk and naist-jdic are both BSD 3-clause;
 * see NOTICE.
 *
 * THE TEST.  build/Japanese_test/front_oracle.tsv holds 1,900 texts with the
 * morae, accents, devoicing flags and question flag the Python produces, and
 * every row of it agrees with the compiled Open JTalk 1.11 reference word for
 * word (tools/ja_stage_parity.py).  `ja_check front` requires this code to
 * reproduce every field of every row.  That is what turns a 2,000-line
 * translation into something checkable.
 *
 * DEGRADING.  The dictionary is a 26 MB file read from disk, not compiled in,
 * and it is loaded lazily on the first Japanese utterance.  If it is not
 * found, everything here is skipped and the kana and romaji path works exactly
 * as it did before -- an English-only or kana-only user pays nothing for this.
 */
#ifndef JA_NJD_H
#define JA_NJD_H

#include <stddef.h>
#include <stdint.h>

/* ---- the compiled dictionary (ja_dict.c) -------------------------------- */

/*
 * lang/jpn/data/jadic.bin, format version 4, as tools/gen_ja_dict.py writes it.
 * The whole file is read into one buffer and nothing is copied out of it: the
 * string pools are NUL-separated, so a surface or a reading is a pointer into
 * the buffer, and a part-of-speech tuple is six consecutive strings.
 */
#define JA_DICT_MAGIC "OPENTVJ1"
#define JA_DICT_VERSION 4
#define JA_ENTRY_SIZE 28
#define JA_COMP_SIZE  16
#define JA_UNK_SIZE   12

typedef struct {
    unsigned char *b;
    size_t         len;
    uint32_t version, n_entries, n_pos, n_chain, n_cat, n_unk, dim, n_comp;
    uint32_t off_surf, len_surf, off_pron, len_pron, off_posp, len_posp;
    uint32_t off_chainp, len_chainp, off_posi, off_chaini;
    uint32_t off_entry, off_matrix, off_cat, off_cmap, off_unk, off_comp;
} ja_dict;

/*
 * 0 on success.  -1 the file could not be read, -2 it is not a dictionary,
 * -3 it is the wrong format version, -4 out of memory.  ja_dict_error() turns
 * one of those into something a user can act on.
 */
int         ja_dict_open(ja_dict *d, const char *path);
void        ja_dict_close(ja_dict *d);
const char *ja_dict_error(int code);

/*
 * Where the dictionary is: $TVTTS_JA_DICT if set, else jadic.bin or
 * lang/jpn/data/jadic.bin beside the loaded module, then beside the executable.
 * Writes an absolute path into `buf` and returns it, or NULL if none exists.
 */
const char *ja_dict_path(char *buf, size_t cap);

/* ---- an arena ----------------------------------------------------------- */

/*
 * Most strings here point into the dictionary buffer or into a generated
 * table and are never copied.  The few that are built -- a run of unknown kana
 * chained into one word, a counter whose first mora has been voiced, a word
 * loaded from a table's CSV feature string -- come from here, and the whole
 * lot is released when the utterance is done.
 */
typedef struct ja_arena_blk ja_arena_blk;
typedef struct { ja_arena_blk *head; } ja_arena;

void  ja_arena_free(ja_arena *a);
void *ja_arena_alloc(ja_arena *a, size_t n);
char *ja_arena_str(ja_arena *a, const char *s);
char *ja_arena_strn(ja_arena *a, const char *s, size_t n);
char *ja_arena_cat(ja_arena *a, const char *s, const char *t);

/* ---- a word ------------------------------------------------------------- */

#define JA_POS_N 6

/*
 * Open JTalk's NJDNode, less the fields nothing here reads.  `pron` is NULL
 * when a stage has silenced the word -- upstream calls NJD_remove_silent_node
 * and this calls ja_njd_compact, which is the same thing.
 *
 * `read` is the kana as WRITTEN where `pron` is the kana as SAID: 通り reads
 * トオリ and is pronounced トーリ.  Only njd_set_digit's class3 table tests
 * it, and it has to be the dictionary's own field because the passes that run
 * before class3 rewrite the pronunciation and leave the reading alone.
 */
typedef struct {
    const char *string;
    const char *pos[JA_POS_N];
    const char *pron;
    const char *read;
    int         acc, mora_size;
    const char *chain_rule;
    int         chain_flag;      /* -1 unset, 0 starts a phrase, 1 chains */
} ja_word;

/* A growable word list.  The stages insert and delete, so it is not an array
 * of fixed length: set_digit replaces a run of digits with a longer run of
 * place names, and several passes silence a word and drop it. */
typedef struct {
    ja_word *w;
    int      n, cap;
} ja_vec;

void     ja_vec_free(ja_vec *v);
ja_word *ja_vec_push(ja_vec *v);             /* -> a reset word, or NULL */
int      ja_vec_add(ja_vec *v, const ja_word *w);
void     ja_vec_compact(ja_vec *v);          /* drop the words with no pron */
void     ja_word_reset(ja_word *w);

/* ---- the analysis (ja_dict.c) ------------------------------------------- */

/*
 * text2mecab's substitutions.  Writes at most `cap` bytes including the NUL
 * and returns the length it wanted, so a short buffer is detectable.
 */
size_t ja_normalize(const char *text, char *out, size_t cap);

/*
 * A reading for a word the dictionary has no reading for, from Open JTalk's
 * own 369-row table.  `morae` may be NULL.  A character nothing matches is
 * skipped, and a word with no matches at all comes back empty: kanji has no
 * reading derivable from its shape.
 */
char *ja_unknown_pron(ja_arena *a, const char *s, int *morae);

/* Viterbi over the dictionary.  0 on success, -1 out of memory. */
int ja_analyse(const ja_dict *d, ja_arena *a, const char *text, ja_vec *out);

/* ---- the stages (ja_njd.c, ja_digit.c) ---------------------------------- */

int ja_set_pronunciation(ja_arena *a, ja_vec *v);
int ja_set_digit(ja_arena *a, ja_vec *v);
/*
 * A reading for a Latin token the dictionary does not know, PER TOKEN.  Runs
 * between the pronunciation stage and the digit stage, so a dictionary
 * reading, a derived one and Japanese text can share a sentence; see the
 * function for why the decision cannot be made per utterance.
 */
int ja_read_latin(ja_arena *a, ja_vec *v);

/*
 * How to ask what an English word sounds like, as a callback rather than a
 * direct call.  It keeps this file free of any dependency on the English
 * engine -- `ja_check` links the Japanese front end ALONE, with no engine and
 * no data, and that is worth keeping -- and it is the seam a different
 * pronunciation source would plug into.
 *
 * snprintf-style: the return is the bytes wanted including the terminator.
 */
typedef int (*ja_ask_fn)(void *ctx, const char *word, char *buf, size_t cap);

/*
 * The same, with the Latin-word rules available.  `ask` must be backed by a
 * synthesiser kept SOLELY for asking: the trace is got by synthesising, so
 * asking the one the Japanese path is speaking through would corrupt the
 * utterance in progress.  NULL falls back to the romaji-and-spelling
 * behaviour of ja_read_latin.
 */
int ja_read_latin_g2p(ja_arena *a, ja_vec *v, ja_ask_fn ask, void *ctx);

/* ---- the Latin-word rules (ja_g2p.c) ------------------------------------ */

enum { JA_G2P_NONE = 0, JA_G2P_WORD, JA_G2P_LETTERS };

/*
 * An English phoneme string, as tvtts_text_to_phonemes returns it, plus the
 * word it came from -> a Japanese reading in katakana and an accent type.
 *
 * JA_G2P_WORD when it produced one, JA_G2P_LETTERS when the word is an
 * initialism and should keep the Japanese letter names it already gets, and
 * JA_G2P_NONE when there was nothing to read.  `word` may be NULL; it is used
 * only for the all-capitals test, which the phoneme string alone cannot make.
 */
int ja_g2p_read(const char *phonemes, const char *word, char *kana,
                size_t cap, int *accent);

int ja_set_accent_phrase(ja_vec *v);
int ja_set_accent_type(ja_vec *v);

/* One mora of a pronunciation, as the devoicing stage sees it. */
typedef struct {
    const char *mora;           /* into ja_MORA, or into the pron for ヮ */
    int         len;
    int         flag;           /* -1 undecided, 0 voiced, 1 devoiced */
    int         word;           /* index into the word list */
    int         midx, acc;      /* position in the phrase, and its accent */
} ja_dvm;

/*
 * Splits every pronunciation into morae and decides devoicing over the whole
 * utterance.  Writes at most `cap` and returns the count it wanted.
 */
int ja_split_morae(const char *pron, ja_dvm *out, int cap);
int ja_set_unvoiced_vowel(const ja_vec *v, ja_dvm *out, int cap);

/* ---- the front end (ja_front.c) ---------------------------------------- */

#include "ja.h"

/*
 * What jp_front.to_front produces: the mora list the frame builder takes, one
 * accent type per accent phrase, a devoicing flag per mora, and whether the
 * utterance was a question.
 */
typedef struct {
    ja_mora *morae;
    int     *devoiced;          /* one per mora */
    int      n_morae;
    int     *accents;
    int      n_accents;
    int      question;
    int      words, unreadable, dropped;   /* what did not survive */
    /*
     * How many words the DICTIONARY actually knew -- a real entry, not a
     * reading derived character by character from the pronunciation table.
     * The router needs this to tell "the lexicon has a reading for this Latin
     * word" from "nothing knew it, so it was spelled out", because those two
     * want opposite decisions about whether to try romaji instead.
     */
    int      known;
} ja_front;

void ja_front_free(ja_front *f);

/*
 * text -> everything ja_build and ja_pitch need.  0 on success, -1 out of
 * memory, 1 when nothing sayable came out (`f` is then zeroed but valid).
 */
int ja_front_text(const ja_dict *d, const char *text, ja_front *f);

/*
 * The same, with the Latin-word rules available; see ja_read_latin_g2p for
 * what `ask` has to be.  A NULL `ask` is ja_front_text.
 */
int ja_front_text_g2p(const ja_dict *d, const char *text,
                      ja_ask_fn ask, void *ctx, ja_front *f);

/*
 * A reading with no dictionary at all, from Open JTalk's pronunciation table
 * alone: the kana it knows, and the Latin letters' Japanese names.  Heiban
 * throughout and nothing devoiced, both being lexical.  This is what keeps
 * the no-dictionary fallback from being lossy; see the function.
 */
int ja_front_spell(const char *text, ja_front *f);

/*
 * One katakana mora -> the one mora the frame builder understands, routed
 * through ja_to_morae so the kana path and this one cannot drift.  Returns 0
 * and fills `out`, or -1 for a mora nothing can say.
 */
int ja_mora_of(const char *kata, int len, ja_mora *out);

#endif  /* JA_NJD_H */
