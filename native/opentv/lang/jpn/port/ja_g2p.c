/*
 * An unfamiliar Latin word -> a Japanese reading.
 *
 * Translated from build/Japanese_test/jp_g2p.py, whose comments carry the
 * reasoning and the probing; the tables come through tools/gen_ja_g2p.py so
 * the two cannot drift.  `tools/ja_latin_table.py` prints what the rules make
 * of five categories of word, and the `i`-series samples from make_dict.py
 * are what was listened to.
 *
 * WHERE THE PRONUNCIATION COMES FROM.  Not from here: OpenTV already contains
 * an English pronunciation engine, and `tvtts_text_to_phonemes` returns its
 * answer.  It gets that answer by synthesising and discarding the audio,
 * which sounds expensive and is not -- MEASURED at 0.28 ms per word, so a
 * five-Latin-word utterance costs 1.4 ms to produce over a second of audio.
 * That is 0.1% of the duration it accompanies, and it is the reason this
 * calls the English front end rather than reimplementing it, and the reason
 * nothing was refactored to avoid the synthesis.
 *
 * What this file is, is the other half: those phonemes adapted to Japanese,
 * which is not transliteration because Japanese has no syllable for `blorf`
 * and has to build one.
 */
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"
#include "ja.h"
#include "ja_njd.h"
#include "ja_g2p_tab.h"

#define EQ(a, b) (strcmp((a), (b)) == 0)
#define JA_G2P_MAX 128          /* morae in one word; a word is not a corpus */

/* ---- the engine's string -> units of phonemes ---------------------------- */

/*
 * `&` opens a stressed unit and `%` an unstressed one, so a word is ONE unit
 * and an initialism is several: `NVDA` comes back `&e1N&VE1&DE1&A1.`.  That
 * count is the word-versus-letters test.
 *
 * Stress is `1` and `2` ONLY.  `3`, `4` and `5` are phonemes -- hurt, hear,
 * fire -- and treating every digit as stress ate them, which is why
 * `computer` first came out キンプ and `printer` プリント.
 */
typedef struct {
    signed char id;
    unsigned char stress;
} ja_ph;

static int parse_phonemes(const char *ph, ja_ph *out, int cap, int *n_units)
{
    int n = 0, units = 0, fresh = 1;
    const unsigned char *p = (const unsigned char *)ph;

    *n_units = 0;
    for (; *p != '\0'; p++) {
        if (*p == '&' || *p == '%') {
            fresh = 1;
            continue;
        }
        if (*p == '.')
            continue;
        if (*p == '1' || *p == '2') {
            if (n > 0)
                out[n - 1].stress = (unsigned char)(*p - '0');
            continue;
        }
        if (*p >= 128)
            continue;
        {
            signed char id = ja_g2p_sym[*p];
            if (id < 0)
                continue;
            if (ja_g2p_tab[id].kind == JA_P_DROP)
                continue;       /* a release marker, not a sound */
            if (fresh) {
                units++;
                fresh = 0;
            }
            if (n >= cap)
                return -1;
            out[n].id = id;
            out[n].stress = 0;
            n++;
        }
    }
    *n_units = units;
    return n;
}

/* ---- the small lookups -------------------------------------------------- */

static const char *lookup(const ja_g2p_map *t, int n, const char *k)
{
    int i;
    for (i = 0; i < n; i++)
        if (EQ(t[i].from, k))
            return t[i].to;
    return k;
}

static char epenthetic(const char *ons)
{
    int i;
    for (i = 0; i < ja_g2p_epenthetic_n; i++)
        if (EQ(ja_g2p_epenthetic[i].onset, ons))
            return ja_g2p_epenthetic[i].v;
    return JA_G2P_EPENTHETIC_DEFAULT[0];
}

static int geminates(const char *ons)
{
    int i;
    for (i = 0; i < ja_g2p_geminate_n; i++)
        if (EQ(ja_g2p_geminate[i], ons))
            return 1;
    return 0;
}

/* ---- building morae ----------------------------------------------------- */

typedef struct {
    char m[JA_G2P_MAX][8];
    int  n;
} molist;

static int push(molist *L, const char *m)
{
    if (L->n >= JA_G2P_MAX || strlen(m) >= sizeof L->m[0])
        return -1;
    strcpy(L->m[L->n++], m);
    return 0;
}

/*
 * One onset and one vowel -> morae, with the inventory's gaps repaired.
 * Japanese has no /si zi tu du/ and the kana reader has no ウィ ウェ ウォ
 * チェ シェ ジェ フュ, so those are substituted rather than emitted and lost.
 */
static int cv(molist *L, const char *ons, char v)
{
    char buf[8];
    int i;

    for (i = 0; i < ja_g2p_repair_n; i++)
        if (EQ(ja_g2p_repair_tab[i].onset, ons)
            && ja_g2p_repair_tab[i].v == v) {
            if (push(L, ja_g2p_repair_tab[i].mora[0]) != 0)
                return -1;
            if (ja_g2p_repair_tab[i].mora[1] != NULL)
                return push(L, ja_g2p_repair_tab[i].mora[1]);
            return 0;
        }
    if (strlen(ons) + 2 > sizeof buf)
        return -1;
    strcpy(buf, ons);
    buf[strlen(ons)] = v;
    buf[strlen(ons) + 1] = '\0';
    return push(L, buf);
}

/*
 * A REDUCED VOWEL TAKES ITS JAPANESE VOWEL FROM THE SPELLING.
 *
 * English reduces an unstressed vowel to something central -- the engine
 * writes it IX or AX -- and Japanese does not reduce at all, so it has to put
 * SOME vowel there and the one it puts is the written one:
 *
 *     computer  コンピューター    the o of `com`, not the schwa
 *     lemon     レモン           melon メロン, method メソッド, London ロンドン
 *     banana    バナナ           sofa ソファ, about アバウト, Canada カナダ
 *     pilot     パイロット      carrot キャロット, xylophone ザイロフォーン
 *
 * Without this `computer` came out キンピューター -- "kimputer" -- which is
 * what every other system does not say.
 *
 * The alignment is ordinal and crude: the word's RUNS of vowel letters are
 * counted off against its syllable nuclei, so the Nth nucleus takes the Nth
 * run.  That is not a real grapheme-to-phoneme alignment and does not have to
 * be; it only has to find the vowel of the syllable the reduced one is in,
 * and a reduced vowel is almost always its own syllable.  Where the counts do
 * not line up, the phonetic vowel stands.
 */
static int vowel_runs(const char *word, const char **runs, int cap)
{
    int n = 0;
    const char *p = word;

    if (word == NULL)
        return 0;
    while (*p != '\0') {
        if (strchr("aeiouyAEIOUY", *p) != NULL) {
            if (n < cap)
                runs[n] = p;
            n++;
            while (*p != '\0' && strchr("aeiouyAEIOUY", *p) != NULL)
                p++;
        } else
            p++;
    }
    return n;
}

static char letter_vowel(char c)
{
    switch (c) {
    case 'a': case 'A': return 'a';
    case 'e': case 'E': return 'e';
    case 'i': case 'I': return 'i';
    case 'o': case 'O': return 'o';
    case 'u': case 'U': return 'u';
    case 'y': case 'Y': return 'i';
    default:            return 0;
    }
}

static int reduced(const char *name)
{
    return EQ(name, "IX") || EQ(name, "AX");
}

/*
 * The r-coloured reduced vowels, but ONLY away from the end of the word and
 * only before another vowel.  A final -er is アー whatever it is spelled --
 * コンピューター, プリンター, ウォーター -- and so is one with no vowel after
 * it, which is what makes `internet` インターネット and not インテネット.  A
 * medial one before a vowel takes the written vowel and hands its /r/ to the
 * next mora: /mərə/ is メラ, two morae, with the r starting the second.
 * Losing that r made `camera` キャメア and `america` アメイカ.
 */
static int reduced_medial(const char *name)
{
    return EQ(name, "ER") || EQ(name, "RR");
}

static char spelled_vowel(const char *name, int n, const char **runs,
                          int n_runs, int n_nuclei, int before_vowel)
{
    if (n < 1 || n > n_runs)
        return 0;
    if (!reduced(name)) {
        if (!reduced_medial(name))
            return 0;
        if (n >= n_nuclei || !before_vowel)
            return 0;
    }
    return letter_vowel(runs[n - 1][0]);
}

/* An onset plus one phoneme's vowel.  -> how many morae it added, or -1. */
static int nucleus(molist *L, const char *ons, int id, int stress,
                   char spelled)
{
    int before = L->n;
    const ja_g2p_phone *p = (id >= 0) ? &ja_g2p_tab[id] : NULL;

    if (ons == NULL)
        ons = "";
    if (p == NULL) {
        if (cv(L, ons, epenthetic(ons)) != 0)
            return -1;
        return L->n - before;
    }
    if (spelled != 0) {
        /* a reduced vowel: the written one, not the central one */
        if (cv(L, ons, spelled) != 0)
            return -1;
        return L->n - before;
    }
    if (p->kind == JA_P_YU) {
        /* /ju:/ palatalises instead of adding a glide: /pju:/ is ピュー and
         * not ピユー, which is a different number of morae. */
        const char *o = (*ons != '\0')
            ? lookup(ja_g2p_palatal, ja_g2p_palatal_n, ons) : "y";
        if (cv(L, o, 'u') != 0 || push(L, ":") != 0)
            return -1;
        return L->n - before;
    }
    if (EQ(p->name, "AE")) {
        const char *o = lookup(ja_g2p_ae_palatal, ja_g2p_ae_palatal_n, ons);
        if (cv(L, o, 'a') != 0)
            return -1;
        return L->n - before;
    }
    if (p->nv > 1) {
        int i;
        if (cv(L, ons, p->v[0]) != 0)
            return -1;
        for (i = 1; i < p->nv; i++) {
            char one[2];
            one[0] = p->v[i];
            one[1] = '\0';
            if (push(L, one) != 0)
                return -1;
        }
        return L->n - before;
    }
    if (cv(L, ons, p->v[0]) != 0)
        return -1;
    /* An unstressed /oʊ/ or /ɔ/ comes out short: `microsoft` is マイクロソフト
     * and not マイクローソーフト.  The r-coloured vowels stay long whatever
     * their stress, because a final one is long even unstressed --
     * コンピューター, プリンター, ウォーター. */
    if (p->long_ && !(stress == 0 && p->shorten_unstressed))
        if (push(L, ":") != 0)
            return -1;
    return L->n - before;
}

static int is_vowelish(int id)
{
    unsigned k;
    if (id < 0)
        return 0;
    k = ja_g2p_tab[id].kind;
    return k == JA_P_VOWEL || k == JA_P_DIPH || k == JA_P_TRIPH
        || k == JA_P_YU;
}

/*
 * A geminate needs a REAL short vowel before it: not the moraic nasal, not
 * another geminate, not a long vowel, not a vowel this inserted, and not the
 * tail of a diphthong.  `cat` is キャット because its /t/ follows the real æ;
 * `test` is テスト because its /t/ follows an inserted ウ; `take` is テイク
 * because its /k/ follows a diphthong.
 */
static int can_geminate(const molist *L, int inserted, int heavy)
{
    const char *last;
    if (L->n == 0 || inserted || heavy)
        return 0;
    last = L->m[L->n - 1];
    return !(EQ(last, "N") || EQ(last, "Q") || EQ(last, ":"));
}

/* Where the fall goes: the antepenultimate mora, passed left off a mora that
 * cannot carry one.  A tendency rather than a rule, and the least tested part
 * of this. */
static int accent_of(const molist *L)
{
    int a;
    if (L->n == 0)
        return 0;
    a = L->n - 2;
    while (a > 1 && (EQ(L->m[a - 1], "N") || EQ(L->m[a - 1], "Q")
                     || EQ(L->m[a - 1], ":")))
        a--;
    if (a < 1)
        a = 1;
    return a <= L->n ? a : 0;
}

static int adapt(const ja_ph *u, int n, const char *word, molist *L,
                 int *accent)
{
    int k = 0, inserted = 0, heavy = 0;
    const char *runs[32];
    int n_runs, n_nuclei = 0, at = 0;
    const char *carry = "";     /* an onset the mora before handed over */

    L->n = 0;
    n_runs = vowel_runs(word, runs, 32);
    for (k = 0; k < n; k++)
        if (is_vowelish(u[k].id))
            n_nuclei++;
    k = 0;
    while (k < n) {
        int id = u[k].id;
        int nxt = (k + 1 < n) ? u[k + 1].id : JA_P_NONE;
        const ja_g2p_phone *p = &ja_g2p_tab[id];
        const char *ons;
        int got;

        if (is_vowelish(id)) {
            char sp = spelled_vowel(p->name, ++at, runs, n_runs, n_nuclei,
                                    is_vowelish(nxt));
            const char *o = carry;
            carry = "";
            got = nucleus(L, o, id, u[k].stress, sp);
            if (got < 0)
                return -1;
            if (sp != 0 && reduced_medial(p->name))
                carry = "r";    /* the /r/ starts the next mora */
            inserted = 0;
            heavy = got > 1;
            k++;
            continue;
        }
        if (p->kind == JA_P_SPECIAL) {
            if (EQ(p->name, "NG") || EQ(p->name, "UN")) {
                if (push(L, "N") != 0)
                    return -1;
            } else if (EQ(p->name, "LX") || EQ(p->name, "UL")) {
                /* syllabic /l/: an onset when a vowel follows, a mora of its
                 * own otherwise.  `xylophone` is ザイロフォーン. */
                if (is_vowelish(nxt)) {
                    int after = (k + 2 < n) ? u[k + 2].id : JA_P_NONE;
                    char sp = spelled_vowel(ja_g2p_tab[nxt].name, ++at, runs,
                                            n_runs, n_nuclei,
                                            is_vowelish(after));
                    if (nucleus(L, "r", nxt, u[k + 1].stress, sp) < 0)
                        return -1;
                    /* these were left alone here, so a diphthong before a
                     * syllabic /l/ still blocked the geminate after it and
                     * `pilot` came out パイロト */
                    inserted = 0;
                    heavy = 0;
                    k += 2;
                    continue;
                }
                if (push(L, "ru") != 0)
                    return -1;
            } else if (EQ(p->name, "UM")) {
                if (push(L, "mu") != 0)
                    return -1;
            }
            inserted = 0;
            heavy = 0;
            k++;
            continue;
        }
        ons = p->onset;
        if (ons == NULL) {
            k++;
            continue;
        }
        /* The engine spells the unaspirated /t/ of an /st/ cluster as D --
         * `stop` is SDo1Pp -- and Japanese hears /t/: ストップ. */
        if (EQ(ons, "d") && L->n > 0
            && (EQ(L->m[L->n - 1], "su") || EQ(L->m[L->n - 1], "shi")))
            ons = "t";
        /* a nasal with no vowel after it is the moraic nasal, not /nu/ */
        if ((EQ(ons, "n") || EQ(ons, "m")) && !is_vowelish(nxt)) {
            if (push(L, "N") != 0)
                return -1;
            inserted = 0;
            heavy = 0;
            k++;
            continue;
        }
        /* an affricate followed by its own fricative release is one
         * consonant: `orange` ends JH ZH and is ジ, not ジジ */
        if (nxt >= 0 && (EQ(ons, "j") || EQ(ons, "ch"))
            && (EQ(ja_g2p_tab[nxt].name, "ZH")
                || EQ(ja_g2p_tab[nxt].name, "SH"))) {
            int j;
            ja_ph *w = (ja_ph *)u;      /* collapse in place */
            for (j = k + 1; j + 1 < n; j++)
                w[j] = w[j + 1];
            n--;
            nxt = (k + 1 < n) ? u[k + 1].id : JA_P_NONE;
        }
        if (is_vowelish(nxt)) {
            /* English's syllabic -le is a consonant plus /l/ in Japanese and
             * the reduced vowel between them goes: `google` is グーグル and
             * `apple` アップル, not グーギル and アピル. */
            int after = (k + 2 < n) ? u[k + 2].id : JA_P_NONE;
            int red = (EQ(ja_g2p_tab[nxt].name, "IX")
                       || EQ(ja_g2p_tab[nxt].name, "AX"));
            int syl = (after >= 0 && (EQ(ja_g2p_tab[after].name, "LX")
                                      || EQ(ja_g2p_tab[after].name, "UL")));
            if (!(red && syl)) {
                int after2 = (k + 2 < n) ? u[k + 2].id : JA_P_NONE;
                char sp = spelled_vowel(ja_g2p_tab[nxt].name, ++at, runs,
                                        n_runs, n_nuclei,
                                        is_vowelish(after2));
                const char *o = *carry != '\0' ? (*ons ? ons : carry) : ons;
                carry = "";
                got = nucleus(L, o, nxt, u[k + 1].stress, sp);
                if (got < 0)
                    return -1;
                if (sp != 0 && reduced_medial(ja_g2p_tab[nxt].name))
                    carry = "r";
                inserted = 0;
                heavy = got > 1;
                k += 2;
                continue;
            }
            at++;               /* the reduced vowel is consumed here */
            if (nucleus(L, ons, JA_P_NONE, 0, 0) < 0)
                return -1;
            inserted = 1;
            heavy = 0;
            k += 2;
            continue;
        }
        /* no vowel of its own: geminate, or take an inserted vowel */
        if (geminates(ons) && can_geminate(L, inserted, heavy)
            && (nxt < 0
                || (ja_g2p_tab[nxt].onset != NULL
                    && !EQ(ja_g2p_tab[nxt].onset, "r")
                    && !EQ(ja_g2p_tab[nxt].onset, "w")
                    && !EQ(ja_g2p_tab[nxt].onset, "y")))) {
            if (push(L, "Q") != 0
                || nucleus(L, ons, JA_P_NONE, 0, 0) < 0)
                return -1;
            inserted = 1;
            heavy = 0;
            k++;
            continue;
        }
        if (nucleus(L, ons, JA_P_NONE, 0, 0) < 0)
            return -1;
        inserted = 1;
        heavy = 0;
        k++;
    }
    *accent = accent_of(L);
    return 0;
}

/* ---- the entry point ---------------------------------------------------- */

/*
 * The morae this builds carry their own symbols, so each one is looked up
 * directly: ja_mora_kana derives the map from the inventory, and a resolved
 * symbol must not be re-parsed as romaji -- doing that re-applied the
 * parser's conventions and `take` came out テーク instead of テイク.
 */
static int to_kana(const molist *L, char *out, size_t cap)
{
    size_t w = 0;
    int i;

    for (i = 0; i < L->n; i++) {
        const char *k = ja_mora_kana(L->m[i]);
        size_t kl;
        if (k == NULL)
            return -1;          /* the inventory has no katakana for it */
        kl = strlen(k);
        if (w + kl + 1 > cap)
            return -1;
        memcpy(out + w, k, kl);
        w += kl;
    }
    if (cap != 0)
        out[w < cap ? w : cap - 1] = 0;
    return (int)w + 1;
}

int ja_g2p_read(const char *phonemes, const char *word, char *kana,
                size_t cap, int *accent)
{
    ja_ph u[JA_G2P_MAX];
    molist L;
    int n, units, acc = 0;

    if (accent != NULL)
        *accent = 0;
    if (phonemes == NULL || *phonemes == '\0')
        return JA_G2P_NONE;
    n = parse_phonemes(phonemes, u, JA_G2P_MAX, &units);
    if (n <= 0 || units == 0)
        return JA_G2P_NONE;
    /*
     * More than one unit means the English front end read it as letters, and
     * letters must stay letters: an initialism should get the Japanese letter
     * names it already gets, because adapting the English pronunciation of
     * each letter would produce different ones.
     *
     * The unit count is not enough on its own.  The front end reads `TTS` as
     * one unit and pronounces it -- テックスタスビーチ came out of that -- so
     * an all-capital word with no vowel LETTER is taken as an initialism
     * whatever the engine made of it.  Capitalisation alone does not decide:
     * `NASA` and `SAPI` are all capitals, have vowels, and are words.
     */
    if (units > 1)
        return JA_G2P_LETTERS;
    if (word != NULL) {
        const unsigned char *p = (const unsigned char *)word;
        int upper = 1, vowel = 0, len = 0;
        for (; *p != '\0'; p++, len++) {
            if (*p >= 'a' && *p <= 'z')
                upper = 0;
            if (strchr("AEIOUY", (int)*p) != NULL)
                vowel = 1;
        }
        if (upper && len > 1 && !vowel)
            return JA_G2P_LETTERS;
    }
    if (adapt(u, n, word, &L, &acc) != 0 || L.n == 0)
        return JA_G2P_NONE;
    if (to_kana(&L, kana, cap) <= 0)
        return JA_G2P_NONE;
    if (accent != NULL)
        *accent = acc;
    return JA_G2P_WORD;
}
