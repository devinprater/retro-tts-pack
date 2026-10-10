/*
 * text -> morae, accent types, devoicing flags, question flag.
 *
 * Translated from build/Japanese_test/jp_front.py.  Everything below this is
 * unchanged and still covered by oracle_frames.tsv: the frame builder and the
 * Fujisaki contour take the same mora list the kana path hands them.  What is
 * new is what goes IN -- the accent type per phrase, which ja_pitch has always
 * accepted and always been handed 0, and a devoicing flag per mora.
 */
#include <stdlib.h>
#include <string.h>

#include "ja_njd.h"
#include "ja_ojt.h"

#define EQ(a, b) (strcmp((a), (b)) == 0)

/*
 * One katakana mora -> the one mora the frame builder understands, routed
 * through ja_to_morae rather than a second table, so the two inputs cannot
 * drift: whatever `shi` or `kya` or `tsa` means on the kana path, it means
 * the same here.  ja_to_morae folds katakana to hiragana itself.
 */
int ja_mora_of(const char *kata, int len, ja_mora *out)
{
    ja_mora got[4];
    int n, i;

    n = ja_to_morae(kata, (size_t)len, got, 4);
    if (n == 1) {
        *out = got[0];
        return 0;
    }
    /* A mora the kana reader has no symbol for, spelled out rather than
     * dropped; see ja_SPELL_OUT and jp_front._SPELL_OUT for what each one
     * costs. */
    for (i = 0; i < ja_SPELL_OUT_N; i++) {
        size_t kl = strlen(ja_SPELL_OUT[i].kata);
        if ((size_t)len == kl && memcmp(kata, ja_SPELL_OUT[i].kata, kl) == 0) {
            n = ja_to_morae(ja_SPELL_OUT[i].romaji,
                            strlen(ja_SPELL_OUT[i].romaji), got, 4);
            if (n == 1) {
                *out = got[0];
                return 0;
            }
            break;
        }
    }
    return -1;
}

/* Punctuation is not an unreadable word, which is the distinction the counts
 * exist to draw: an unknown token, a known token with no reading, and an
 * utterance that came out empty are three different things. */
static int in_break_sets(const char *s)
{
    int i;
    for (i = 0; i < ja_BREAK_STRONG_n; i++)
        if (EQ(s, ja_BREAK_STRONG[i]))
            return 1;
    for (i = 0; i < ja_BREAK_WEAK_n; i++)
        if (EQ(s, ja_BREAK_WEAK[i]))
            return 1;
    return 0;
}

void ja_front_free(ja_front *f)
{
    free(f->morae);
    free(f->devoiced);
    free(f->accents);
    memset(f, 0, sizeof *f);
}

static int push_mora(ja_front *f, int cap, const ja_mora *m, int dv)
{
    if (f->n_morae >= cap)
        return -1;
    f->morae[f->n_morae] = *m;
    f->devoiced[f->n_morae] = dv;
    f->n_morae++;
    return 0;
}

static void mark(ja_mora *m, int kind)
{
    m->kind = (unsigned char)kind;
    m->c = JA_C_NONE;
    m->v = JA_V_NONE;
}

/*
 * The mora list, with JA_M_SP between accent phrases, JA_M_BAR for a comma
 * and JA_M_BARBAR for a full stop; one accent type per accent phrase, in
 * order, which is what ja_pitch takes; and a devoicing flag per mora.
 *
 * A pause between two spoken morae is kept.  One at the very start or end is
 * not: it has nothing to separate and would only be latency.  So an
 * unreadable word at the edge of an utterance leaves no trace in the audio,
 * and the counts are the only way to know it was there.
 */
static int to_front(const ja_vec *v, ja_front *f)
{
    ja_dvm *flat = NULL;
    int n_flat, cap, i, at = 0, open_phrase = 0, rc = -1;

    memset(f, 0, sizeof *f);
    f->words = v->n;
    if (v->n == 0)
        return 1;

    n_flat = ja_set_unvoiced_vowel(v, NULL, 0);
    flat = (ja_dvm *)malloc((size_t)(n_flat > 0 ? n_flat : 1) * sizeof *flat);
    if (flat == NULL)
        goto done;
    if (ja_set_unvoiced_vowel(v, flat, n_flat) != n_flat)
        goto done;

    /* every mora, plus at most one boundary mark per word */
    cap = n_flat + v->n + 1;
    f->morae = (ja_mora *)malloc((size_t)cap * sizeof *f->morae);
    f->devoiced = (int *)malloc((size_t)cap * sizeof *f->devoiced);
    f->accents = (int *)malloc((size_t)(v->n + 1) * sizeof *f->accents);
    if (f->morae == NULL || f->devoiced == NULL || f->accents == NULL)
        goto done;

    for (i = 0; i < v->n; i++) {
        const ja_word *w = &v->w[i];
        int n_mora = ja_split_morae(w->pron, NULL, 0);
        int base = at, k;
        at += n_mora;
        /*
         * Keyed on the PRONUNCIATION, not the part of speech.  A question
         * mark can come out of the pronunciation stage as a filler rather
         * than a 記号 -- see the note there -- so testing the part of speech
         * lost every question.  The pronunciation is what the stage decides.
         */
        if (w->mora_size == 0
            && (EQ(w->pron, JA_PR_TOUTEN) || EQ(w->pron, JA_OUR_PERIOD)
                || EQ(w->pron, JA_PR_QUESTION))) {
            ja_mora m;
            if (EQ(w->pron, JA_PR_QUESTION)) {
                f->question = 1;
                continue;       /* the rise is prosody, not a pause */
            }
            if (!in_break_sets(w->string))
                f->unreadable++;        /* punctuation is not a failure */
            if (open_phrase) {
                mark(&m, EQ(w->pron, JA_OUR_PERIOD) ? JA_M_BARBAR : JA_M_BAR);
                if (push_mora(f, cap, &m, 0) != 0)
                    goto done;
                open_phrase = 0;
            }
            continue;
        }
        if (n_mora == 0)
            continue;
        if (w->chain_flag != 1) {
            if (open_phrase) {
                ja_mora m;
                mark(&m, JA_M_SP);
                if (push_mora(f, cap, &m, 0) != 0)
                    goto done;
            }
            f->accents[f->n_accents++] = w->acc;
            open_phrase = 1;
        }
        for (k = 0; k < n_mora; k++) {
            const ja_dvm *e = &flat[base + k];
            ja_mora m;
            if (ja_mora_of(e->mora, e->len, &m) != 0) {
                f->dropped++;   /* a mora the kana reader has no symbol for */
                continue;
            }
            if (push_mora(f, cap, &m, e->flag == 1) != 0)
                goto done;
        }
    }
    /* A trailing boundary is a pause with nothing after it to separate. */
    while (f->n_morae > 0 && ja_is_boundary(&f->morae[f->n_morae - 1]))
        f->n_morae--;
    rc = f->n_morae != 0 ? 0 : 1;

done:
    free(flat);
    if (rc < 0)
        ja_front_free(f);
    return rc;
}

/*
 * A reading with NO DICTIONARY: normalise, then take whatever Open JTalk's
 * 369-row pronunciation table can say about the characters, which covers the
 * kana and the Latin letters' Japanese names.  Accent is heiban throughout
 * and nothing is devoiced, because both of those are lexical.
 *
 * This exists so that the no-dictionary fallback is never LOSSY.  The romaji
 * parser drops letters it has no mora for, silently; refusing its reading and
 * having nothing to put in its place would turn `computer` into silence,
 * which is worse.  Spelling it out is at least all of the input.
 */
static int spell_out(const char *text, ja_front *f)
{
    ja_arena a;
    char *norm = NULL, *pron;
    size_t need;
    ja_dvm *flat = NULL;
    int n_flat, i, rc = -1;

    memset(&a, 0, sizeof a);
    memset(f, 0, sizeof *f);

    need = ja_normalize(text, NULL, 0);
    norm = (char *)malloc(need + 1);
    if (norm == NULL)
        goto done;
    ja_normalize(text, norm, need + 1);

    pron = ja_unknown_pron(&a, norm, NULL);
    if (pron == NULL)
        goto done;
    n_flat = ja_split_morae(pron, NULL, 0);
    if (n_flat == 0) {
        rc = 1;                         /* nothing sayable */
        goto done;
    }
    flat = (ja_dvm *)malloc((size_t)n_flat * sizeof *flat);
    f->morae = (ja_mora *)malloc((size_t)n_flat * sizeof *f->morae);
    f->devoiced = (int *)malloc((size_t)n_flat * sizeof *f->devoiced);
    f->accents = (int *)malloc(sizeof *f->accents);
    if (flat == NULL || f->morae == NULL || f->devoiced == NULL
        || f->accents == NULL)
        goto done;
    ja_split_morae(pron, flat, n_flat);
    f->words = 1;
    f->accents[0] = 0;
    f->n_accents = 1;
    for (i = 0; i < n_flat; i++) {
        ja_mora m;
        if (ja_mora_of(flat[i].mora, flat[i].len, &m) != 0) {
            f->dropped++;
            continue;
        }
        f->morae[f->n_morae] = m;
        f->devoiced[f->n_morae] = 0;
        f->n_morae++;
    }
    while (f->n_morae > 0 && ja_is_boundary(&f->morae[f->n_morae - 1]))
        f->n_morae--;
    rc = f->n_morae != 0 ? 0 : 1;

done:
    free(flat);
    free(norm);
    ja_arena_free(&a);
    if (rc < 0)
        ja_front_free(f);
    return rc;
}

int ja_front_spell(const char *text, ja_front *f)
{
    return spell_out(text, f);
}

int ja_front_text(const ja_dict *d, const char *text, ja_front *f)
{
    return ja_front_text_g2p(d, text, NULL, NULL, f);
}

int ja_front_text_g2p(const ja_dict *d, const char *text,
                      ja_ask_fn ask, void *ctx, ja_front *f)
{
    ja_arena a;
    ja_vec v;
    int rc = -1, i, known = 0;

    memset(&a, 0, sizeof a);
    memset(&v, 0, sizeof v);
    memset(f, 0, sizeof *f);

    if (ja_analyse(d, &a, text, &v) != 0)
        goto done;
    if (v.n == 0) {
        rc = 1;
        goto done;
    }
    if (ja_set_pronunciation(&a, &v) != 0)
        goto done;
    /*
     * How many words the DICTIONARY knew, counted HERE and not later.  The
     * pronunciation stage rewrites the first part-of-speech field to
     * フィラー when it derived a reading character by character and to
     * 記号 when it could not read the word at all, so anything else is an
     * entry -- which is what tells `Amazon`, that the lexicon has, from
     * `sakura`, that it does not.
     *
     * It has to be counted before ja_read_latin, which gives a Latin token a
     * reading of its own and makes it a noun.  Counting afterwards would
     * report `konnichiwa` as a word the dictionary knew.
     */
    for (i = 0; i < v.n; i++)
        if (!EQ(v.w[i].pos[0], JA_PR_FILLER)
            && !EQ(v.w[i].pos[0], JA_PR_KIGOU))
            known++;
    /* Not one of Open JTalk's stages: a reading for a Latin token nothing
     * knew, before the stages that need one. */
    if (ja_read_latin_g2p(&a, &v, ask, ctx) != 0)
        goto done;
    if (ja_set_digit(&a, &v) != 0)      /* Open JTalk's own order */
        goto done;
    if (ja_set_accent_phrase(&v) != 0)
        goto done;
    if (ja_set_accent_type(&v) != 0)
        goto done;
    rc = to_front(&v, f);
    if (rc >= 0)
        f->known = known;

done:
    ja_vec_free(&v);
    ja_arena_free(&a);
    return rc;
}
