/*
 * The NJD rule stages: pronunciation, accent phrase, accent type, devoicing.
 * The digit stage is large enough to have its own file; see ja_digit.c.
 *
 * Translated from build/Japanese_test/jp_njd.py, which was read from Open
 * JTalk's sources as a specification.  Read the Python for WHY each rule is
 * what it is -- the comments there carry the measurements and the four bugs
 * that running the real thing turned up -- and read tools/ja_stage_parity.py
 * for the evidence that these rules agree with it: all five stages match the
 * compiled 1.11 reference over 356,486 words and 1,476,548 morae.
 *
 * Every part-of-speech name and every kana tested here comes from ja_ojt.h,
 * extracted from upstream's own headers.  None of it is typed out by hand,
 * because a mistyped character would land in one front end and not the other
 * and the symptom would surface somewhere else entirely.
 */
#include <stdlib.h>
#include <string.h>

#include "ja_njd.h"
#include "ja_ojt.h"

#define EQ(a, b) (strcmp((a), (b)) == 0)

static int in_list(const char *s, const char *const *list, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (EQ(s, list[i]))
            return 1;
    return 0;
}

/* ---- splitting a pronunciation into morae -------------------------------- */

/*
 * Longest-prefix match against Open JTalk's 159-mora inventory, walked in
 * upstream's own order: the list puts each base's two-character combinations
 * before the bare one, so the first match IS the longest.
 *
 * A quotation mark after a mora is the dictionary saying that mora's vowel is
 * devoiced, so it is consumed here and reported rather than parsed.
 *
 * ONE DELIBERATE DIFFERENCE.  A character the inventory does not contain --
 * ヮ, the small WA of クヮルテット, is the only one that occurs in a reading --
 * makes upstream print "Wrong pron" and abandon the whole utterance, losing
 * every mora after it as well.  Here it becomes a mora of its own and the
 * kana reader decides what to do with it.
 */
int ja_split_morae(const char *pron, ja_dvm *out, int cap)
{
    size_t i = 0, n = strlen(pron);
    size_t mlen = strlen(JA_UV_QUOTATION);
    int got = 0, k;

    while (i < n) {
        int size = 0, flag = 0;
        if (n - i >= mlen && memcmp(pron + i, JA_UV_QUOTATION, mlen) == 0) {
            i += mlen;                  /* a mark with no mora before it */
            continue;
        }
        for (k = 0; k < ja_MORA_N; k++) {
            size_t ls = strlen(ja_MORA[k]);
            if (ls <= n - i && memcmp(pron + i, ja_MORA[k], ls) == 0) {
                size = (int)ls;
                break;
            }
        }
        if (size == 0) {
            /* not in the inventory; take one UTF-8 character */
            size = 1;
            while (i + (size_t)size < n
                   && ((unsigned char)pron[i + size] & 0xc0u) == 0x80u)
                size++;
        }
        if (out != NULL && got < cap) {
            out[got].mora = pron + i;
            out[got].len = size;
            out[got].flag = -1;
            out[got].word = 0;
            out[got].midx = 0;
            out[got].acc = 0;
        }
        i += (size_t)size;
        if (n - i >= mlen && memcmp(pron + i, JA_UV_QUOTATION, mlen) == 0) {
            flag = 1;
            i += mlen;
        }
        if (out != NULL && got < cap)
            out[got].flag = flag ? 1 : -1;
        got++;
    }
    return got;
}

/* ---- njd_set_pronunciation ---------------------------------------------- */

static int is_one_of(const char *s, const char *const *list, int n)
{
    return in_list(s, list, n);
}

/* The surfaces of njd_set_pronunciation_list, for the EXACT-match test the
 * kana-filler chaining does.  A prefix walk there joined ええ and と into one
 * word where upstream keeps two. */
static int in_pron_surfaces(const char *s)
{
    int i;
    for (i = 0; i < ja_PRON_LIST_N; i++)
        if (EQ(s, ja_PRON_LIST[i].s))
            return 1;
    return 0;
}

int ja_set_pronunciation(ja_arena *a, ja_vec *v)
{
    int i;
    ja_word *head = NULL;

    for (i = 0; i < v->n; i++) {
        ja_word *w = &v->w[i];
        /*
         * A question mark is settled first and whatever the dictionary says
         * about it, because the analyser can hand it back as an ordinary
         * symbol with a reading and then nothing downstream would know a
         * question had been asked.  Its part of speech stays as the
         * dictionary gave it; 1.11 sets FILLER only when a reading was
         * actually derived, and ？ yields no morae.
         */
        if (is_one_of(w->string, ja_QUESTION_FORMS, ja_QUESTION_FORMS_n)) {
            w->pron = JA_PR_QUESTION;
            continue;
        }
        if (w->mora_size == 0 || w->pron == NULL || EQ(w->pron, "*")) {
            const char *s = w->string;
            /*
             * Only the FIRST part-of-speech field is rewritten, and only in
             * the two branches upstream rewrites it in: a derived reading
             * makes the word a filler and clears the three groups, and a word
             * that yields no reading at all becomes a 記号 with its groups
             * intact.  So 。 keeps 記号/句点 and ？ keeps 記号/一般, while an
             * unreadable 添励 goes from 名詞/一般 to 記号/一般.
             */
            if (is_one_of(s, ja_BREAK_STRONG, ja_BREAK_STRONG_n)) {
                w->pron = JA_OUR_PERIOD;
                w->pos[0] = JA_PR_KIGOU;
            } else if (is_one_of(s, ja_BREAK_WEAK, ja_BREAK_WEAK_n)) {
                w->pron = JA_PR_TOUTEN;
                w->pos[0] = JA_PR_KIGOU;
            } else {
                int nm = 0;
                char *r = ja_unknown_pron(a, s, &nm);
                if (r == NULL)
                    return -1;
                if (nm != 0) {
                    int k;
                    w->pron = r;
                    w->mora_size = nm;
                    w->pos[0] = JA_PR_FILLER;
                    for (k = 1; k < JA_POS_N; k++)
                        w->pos[k] = "*";
                } else {
                    w->pron = JA_PR_TOUTEN;
                    w->pos[0] = JA_PR_KIGOU;
                }
            }
        }
    }

    /*
     * 1.11 adds this: a RUN of unknown kana becomes one word rather than
     * several.  Without it a kana string the dictionary does not know is a
     * string of one-mora words, and the eighteen accent-phrase rules then see
     * a boundary between every character of it.
     */
    for (i = 0; i < v->n; i++) {
        ja_word *w = &v->w[i];
        if (EQ(w->pos[0], JA_PR_FILLER) && in_pron_surfaces(w->string)) {
            if (head == NULL)
                head = w;
            else {
                char *s = ja_arena_cat(a, head->string, w->string);
                char *p = ja_arena_cat(a, head->pron, w->pron);
                if (s == NULL || p == NULL)
                    return -1;
                head->string = s;
                head->pron = p;
                head->mora_size += w->mora_size;
                w->pron = NULL;
            }
        } else
            head = NULL;
    }
    ja_vec_compact(v);

    /* A verb or auxiliary followed by the auxiliary う lengthens instead:
     * はなそう is /hanasoo/. */
    for (i = 0; i + 1 < v->n; i++) {
        ja_word *x = &v->w[i], *y = &v->w[i + 1];
        if (EQ(y->pron, JA_PR_U) && EQ(y->pos[0], JA_PR_JODOUSHI)
            && (EQ(x->pos[0], JA_PR_DOUSHI) || EQ(x->pos[0], JA_PR_JODOUSHI))
            && x->mora_size > 0)
            y->pron = JA_PR_CHOUON;
    }
    /* です and ます before a question mark keep their vowel voiced, because
     * the question rises on that mora. */
    for (i = 0; i + 1 < v->n; i++) {
        ja_word *x = &v->w[i], *y = &v->w[i + 1];
        if (!EQ(x->pos[0], JA_PR_JODOUSHI))
            continue;
        if (!(EQ(y->string, JA_PR_QUESTION) || EQ(y->string, "?")))
            continue;
        if (EQ(x->string, JA_PR_DESU_STR))
            x->pron = JA_PR_DESU_PRON;
        else if (EQ(x->string, JA_PR_MASU_STR))
            x->pron = JA_PR_MASU_PRON;
    }
    ja_vec_compact(v);
    return 0;
}

/* ---- a reading for a Latin token the dictionary does not know ----------- */

/*
 * THIS IS A DECISION PER TOKEN, and that is the point of it.
 *
 * The router used to make it per utterance, which meant one known word
 * suppressed the romaji reading of every other: `Windows konnichiwa` found
 * `Windows`, kept the analyser, and spelled `konnichiwa` out as the Japanese
 * names of its ten letters.  A reading belongs to a word, so the decision
 * goes where words get readings -- here, after the pronunciation stage and
 * before the digit, accent and devoicing stages, so that a dictionary
 * reading, a derived one and Japanese text can share a sentence.
 *
 * What this does NOT do is guess an English pronunciation.  It applies to a
 * token the pronunciation stage has already given up on and SPELLED OUT, so
 * anything readable is an improvement; a token the dictionary knew keeps its
 * entry and is not touched.  When a Latin-to-kana rule set arrives it plugs
 * in exactly here, as the step between those two.
 */
int ja_read_latin(ja_arena *a, ja_vec *v)
{
    return ja_read_latin_g2p(a, v, NULL, NULL);
}

int ja_read_latin_g2p(ja_arena *a, ja_vec *v, ja_ask_fn ask, void *ctx)
{
    int i;

    for (i = 0; i < v->n; i++) {
        ja_word *w = &v->w[i];
        char ascii[256], kana[512];
        const unsigned char *p;
        size_t n = 0;
        int skipped = 0, got, acc = 0;

        /* only a token the pronunciation stage derived a reading for: a
         * dictionary entry has a real part of speech and is left alone */
        if (!EQ(w->pos[0], JA_PR_FILLER))
            continue;
        /*
         * and only one written in fullwidth Latin, which is what text2mecab
         * turns ASCII into.  Anything with a kana in it is already Japanese
         * and the derived reading is the right one.
         */
        p = (const unsigned char *)w->string;
        while (*p != '\0' && n + 1 < sizeof ascii) {
            unsigned long u;
            if (p[0] != 0xef)               /* U+FF01..U+FF5E are EF BC/BD xx */
                break;
            if (p[1] == 0xbc)
                u = 0xff00u + (p[2] & 0x3fu);
            else if (p[1] == 0xbd)
                u = 0xff40u + (p[2] & 0x3fu);
            else
                break;
            if (!((u >= 0xff21u && u <= 0xff3au)
                  || (u >= 0xff41u && u <= 0xff5au)))
                break;
            ascii[n++] = (char)(u - 0xfee0u);
            p += 3;
        }
        if (*p != '\0' || n == 0)
            continue;                       /* not an all-Latin token */
        ascii[n] = '\0';

        /*
         * THE RULES FIRST, when there is an English front end to ask.
         *
         * Romaji-before-rules read `mouse` as モーセ, `fire` フィレ, `orange`
         * オランゲ and `button` ブットン: every one of them consumes whole as
         * romaji, so romaji answered before the rules were asked.  Complete
         * consumption proves a spelling CAN be read as romaji, never that it
         * was meant as one, and ordinary text wants the English default --
         * every Japanese system that was listened to reads a bare `take` as
         * テイク.
         *
         * Deliberate romaji is then the explicit case, and its signal is that
         * the WHOLE utterance is clean romaji with nothing in the dictionary,
         * which tvtts_ja.c tests before it gets here.  That keeps the romaji
         * path bit-identical to what has been signed off by ear, and it is
         * the thing that cannot express `これはsakuraです` meaning the flower.
         */
        acc = 0;
        if (ask != NULL) {
            char ph[512];
            if (ask(ctx, ascii, ph, sizeof ph) > 0) {
                int r = ja_g2p_read(ph, ascii, kana, sizeof kana, &acc);
                if (r == JA_G2P_WORD)
                    goto have_reading;
                if (r == JA_G2P_LETTERS)
                    continue;       /* an initialism keeps its letter names */
            }
        }
        got = ja_romaji_to_kana(ascii, n, kana, sizeof kana, &skipped);
        if (got <= 0 || skipped != 0 || (size_t)got > sizeof kana)
            continue;                       /* not readable as romaji */
    have_reading:
        {
            char *pr = ja_arena_str(a, kana);
            if (pr == NULL)
                return -1;
            w->pron = pr;
            w->read = pr;
            w->mora_size = ja_split_morae(pr, NULL, 0);
            w->acc = acc;       /* the rules' accent, or 0 from romaji */
            /*
             * And it is a NOUN now, not a filler.  A filler is upstream's
             * marker for "a reading was derived character by character", and
             * it carries a rule of its own: the devoicing stage's rule 0 says
             * a filler never devoices.  Leaving the marker on a word that now
             * has a real reading would read スコシ with both its high vowels
             * voiced.
             *
             * A noun with no subclass, because that is what is actually
             * known: the groups are left `*` rather than guessed, and nothing
             * downstream tests any of them for 一般.
             */
            {
                int k;
                w->pos[0] = JA_AP_MEISHI;
                for (k = 1; k < JA_POS_N; k++)
                    w->pos[k] = "*";
            }
        }
    }
    return 0;
}

/* ---- njd_set_accent_phrase ---------------------------------------------- */

static int renyou(const char *cform)
{
    size_t n = strlen(JA_AP_RENYOU);
    return cform != NULL && strlen(cform) >= n
        && memcmp(cform, JA_AP_RENYOU, n) == 0;
}

static int any3(const char *s, const char *a, const char *b, const char *c)
{
    return EQ(s, a) || EQ(s, b) || EQ(s, c);
}

/*
 * Eighteen numbered rules, each either chaining this word onto the phrase
 * before it (1) or starting a new one (0).  A LATER RULE OVERRIDES AN EARLIER
 * ONE, so the order is the rule and they are kept in it rather than collapsed
 * into one decision.
 *
 * The first word is left UNSET, not set to 0.  Upstream's loop starts at
 * njd->head->next and never touches the head, so its chain flag stays -1 and
 * every consumer tests `!= 1` rather than `== 0`.  Writing 0 here changed
 * nothing audible and showed up as a disagreement on the first word of every
 * utterance -- 313 of 1,824 words in a parity run, enough noise to hide a
 * real one.
 */
int ja_set_accent_phrase(ja_vec *v)
{
    int i;
    for (i = 1; i < v->n; i++) {
        ja_word *w = &v->w[i], *p = &v->w[i - 1];
        if (w->chain_flag >= 0)
            continue;
        w->chain_flag = 1;                                          /* 01 */
        if (EQ(p->pos[0], JA_AP_MEISHI) && EQ(w->pos[0], JA_AP_MEISHI))
            w->chain_flag = 1;                                      /* 02 */
        if (EQ(p->pos[0], JA_AP_KEIYOUSHI) && EQ(w->pos[0], JA_AP_MEISHI))
            w->chain_flag = 0;                                      /* 03 */
        if (EQ(p->pos[0], JA_AP_MEISHI)
            && EQ(p->pos[1], JA_AP_KEIYOUDOUSHI_GOKAN)
            && EQ(w->pos[0], JA_AP_MEISHI))
            w->chain_flag = 0;                                      /* 04 */
        if (EQ(p->pos[0], JA_AP_DOUSHI)
            && (EQ(w->pos[0], JA_AP_KEIYOUSHI) || EQ(w->pos[0], JA_AP_MEISHI)))
            w->chain_flag = 0;                                      /* 05 */
        if (any3(w->pos[0], JA_AP_FUKUSHI, JA_AP_SETSUZOKUSHI, JA_AP_RENTAISHI)
            || any3(p->pos[0], JA_AP_FUKUSHI, JA_AP_SETSUZOKUSHI,
                    JA_AP_RENTAISHI))
            w->chain_flag = 0;                                      /* 06 */
        if (EQ(p->pos[0], JA_AP_MEISHI) && EQ(p->pos[1], JA_AP_FUKUSHI_KANOU))
            w->chain_flag = 0;                                      /* 07 */
        if (EQ(w->pos[0], JA_AP_MEISHI) && EQ(w->pos[1], JA_AP_FUKUSHI_KANOU))
            w->chain_flag = 0;
        if (EQ(w->pos[0], JA_AP_JODOUSHI) || EQ(w->pos[0], JA_AP_JOSHI))
            w->chain_flag = 1;                                      /* 08 */
        if ((EQ(p->pos[0], JA_AP_JODOUSHI) || EQ(p->pos[0], JA_AP_JOSHI))
            && !(EQ(w->pos[0], JA_AP_JODOUSHI) || EQ(w->pos[0], JA_AP_JOSHI)))
            w->chain_flag = 0;                                      /* 09 */
        if (EQ(p->pos[1], JA_AP_SETSUBI) && EQ(w->pos[0], JA_AP_MEISHI))
            w->chain_flag = 0;                                      /* 10 */
        if (EQ(w->pos[0], JA_AP_KEIYOUSHI)
            && EQ(w->pos[1], JA_AP_HIJIRITSU)) {                    /* 11 */
            if (EQ(p->pos[0], JA_AP_DOUSHI) || EQ(p->pos[0], JA_AP_KEIYOUSHI)) {
                if (renyou(p->pos[5]))
                    w->chain_flag = 1;
            } else if (EQ(p->pos[0], JA_AP_JOSHI)
                       && EQ(p->pos[1], JA_AP_SETSUZOKUJOSHI)) {
                if (EQ(p->string, JA_AP_TE) || EQ(p->string, JA_AP_DE))
                    w->chain_flag = 1;
            }
        }
        if (EQ(w->pos[0], JA_AP_DOUSHI)
            && EQ(w->pos[1], JA_AP_HIJIRITSU)) {                    /* 12 */
            if (EQ(p->pos[0], JA_AP_DOUSHI) && renyou(p->pos[5]))
                w->chain_flag = 1;
            else if (EQ(p->pos[0], JA_AP_MEISHI)
                     && EQ(p->pos[1], JA_AP_SAHEN_SETSUZOKU))
                w->chain_flag = 1;
        }
        if (EQ(p->pos[0], JA_AP_MEISHI)) {                          /* 13 */
            if (EQ(w->pos[0], JA_AP_DOUSHI) || EQ(w->pos[0], JA_AP_KEIYOUSHI)
                || EQ(w->pos[1], JA_AP_KEIYOUDOUSHI_GOKAN))
                w->chain_flag = 0;
        }
        if (EQ(w->pos[0], JA_AP_KIGOU) || EQ(p->pos[0], JA_AP_KIGOU))
            w->chain_flag = 0;                                      /* 14 */
        if (EQ(w->pos[0], JA_AP_SETTOUSHI))
            w->chain_flag = 0;                                      /* 15 */
        if (EQ(p->pos[3], JA_AP_SEI) && EQ(w->pos[0], JA_AP_MEISHI))
            w->chain_flag = 0;                                      /* 16 */
        if (EQ(p->pos[0], JA_AP_MEISHI) && EQ(w->pos[3], JA_AP_MEI))
            w->chain_flag = 0;                                      /* 17 */
        if (EQ(w->pos[1], JA_AP_SETSUBI))
            w->chain_flag = 1;                                      /* 18 */
    }
    return 0;
}

/* ---- njd_set_accent_type ------------------------------------------------ */

/*
 * The chain rule's small grammar.  A rule is either a bare code (C1), or
 * alternatives chosen by the PREVIOUS word's part of speech, where '%'
 * introduces a test, '@' a numeric offset and '/' separates alternatives:
 *
 *     動詞%F4@1/形容詞%F2@-1
 */
#define RULE_MAXTOK 48
#define RULE_MAXLEN 128

static void get_rule(const char *chain_rule, const char *prev_pos,
                     char *rule, int *add_type)
{
    char buf[RULE_MAXTOK][RULE_MAXLEN];
    char sep[RULE_MAXTOK];
    int n = 0, i;
    size_t j = 0, k = 0;

    strcpy(rule, "*");
    *add_type = 0;
    if (chain_rule == NULL || *chain_rule == '\0')
        return;

    for (j = 0; chain_rule[j] != '\0' && n < RULE_MAXTOK - 1; j++) {
        char ch = chain_rule[j];
        if (ch == '%' || ch == '@' || ch == '/') {
            buf[n][k] = '\0';
            sep[n++] = ch;
            k = 0;
        } else if (k + 1 < RULE_MAXLEN)
            buf[n][k++] = ch;
    }
    buf[n][k] = '\0';
    sep[n++] = '\0';

    i = 0;
    while (i < n) {
        if (sep[i] == '%') {
            if (prev_pos != NULL && buf[i][0] != '\0'
                && strstr(prev_pos, buf[i]) != NULL) {
                if (i + 1 < n && buf[i + 1][0] != '\0')
                    strcpy(rule, buf[i + 1]);
                if (i + 1 < n && sep[i + 1] == '@' && i + 2 < n)
                    *add_type = atoi(buf[i + 2]);
                return;
            }
            i++;                        /* skip this alternative */
            while (i < n && (sep[i - 1] == '%' || sep[i - 1] == '@'))
                i++;
            continue;
        }
        if (sep[i] == '@' && i + 1 < n)
            *add_type = atoi(buf[i + 1]);
        if (buf[i][0] != '\0')
            strcpy(rule, buf[i]);
        return;
    }
}

/* The digit branch tests these by name; SUU and KAZU are the same character
 * and upstream has both names, so this does too. */
static const char *const ONES[] = {
    JA_AC_ICHI, JA_AC_NI, JA_AC_SAN, JA_AC_YON, JA_AC_GO,
    JA_AC_ROKU, JA_AC_NANA, JA_AC_HACHI, JA_AC_KYUU
};

/*
 * Where the fall goes in a number: 三十 is サンジュー, 七百 ナナヒャク.  A
 * place name does not simply inherit what its chain rule would give it --
 * each of 十 百 千 万 億 兆 has its own rule, several of them depending on
 * which digit precedes -- and the accent it writes belongs to the PRECEDING
 * word, not to the place name.
 */
static void digit_accent(ja_vec *v, int k)
{
    ja_word *w = &v->w[k];
    ja_word *p = k > 0 ? &v->w[k - 1] : NULL;
    ja_word *nx = k + 1 < v->n ? &v->w[k + 1] : NULL;

    if (p != NULL && w->chain_flag == 1
        && EQ(p->pos[1], JA_AC_KAZU) && EQ(w->pos[1], JA_AC_KAZU)) {
        if (EQ(w->string, JA_AC_JYUU)) {                       /* 10^1 */
            /* Upstream tests 三四九何数 here and then sets 1 in both arms, so
             * the test is vestigial; what matters is the override below it. */
            p->acc = 1;
            if ((EQ(p->string, JA_AC_GO) || EQ(p->string, JA_AC_ROKU)
                 || EQ(p->string, JA_AC_HACHI))
                && nx != NULL
                && in_list(nx->string, ONES, (int)(sizeof ONES / sizeof *ONES)))
                p->acc = 0;     /* 五十一 is flat up to the one */
        } else if (EQ(w->string, JA_AC_HYAKU)) {               /* 10^2 */
            if (EQ(p->string, JA_AC_NANA))
                p->acc = 2;
            else if (EQ(p->string, JA_AC_SAN) || EQ(p->string, JA_AC_YON)
                     || EQ(p->string, JA_AC_KYUU) || EQ(p->string, JA_AC_NAN))
                p->acc = 1;
            else
                p->acc = p->mora_size + w->mora_size;
        } else if (EQ(w->string, JA_AC_SEN))                   /* 10^3 */
            p->acc = p->mora_size + 1;
        else if (EQ(w->string, JA_AC_MAN))                     /* 10^4 */
            p->acc = p->mora_size + 1;
        else if (EQ(w->string, JA_AC_OKU))                     /* 10^8 */
            p->acc = (EQ(p->string, JA_AC_ICHI) || EQ(p->string, JA_AC_ROKU)
                      || EQ(p->string, JA_AC_NANA)
                      || EQ(p->string, JA_AC_HACHI)
                      || EQ(p->string, JA_AC_IKU)) ? 2 : 1;
        else if (EQ(w->string, JA_AC_CHOU))                    /* 10^12 */
            p->acc = (EQ(p->string, JA_AC_ROKU)
                      || EQ(p->string, JA_AC_NANA)) ? 2 : 1;
    }
    /* Independent of all that: a 十 heading its own phrase with a number
     * after it is flat.  十二 is ジューニ with no fall on ジュ. */
    if (EQ(w->string, JA_AC_JYUU) && w->chain_flag != 1
        && nx != NULL && EQ(nx->pos[1], JA_AC_KAZU))
        w->acc = 0;
}

/*
 * Each chained word applies its chain rule to the accent of the word that
 * HEADS its phrase, offset by how many morae precede it.  C1 carries the
 * second element's own accent across; C2 puts the fall on the first mora of
 * the second element; C3 on the last of the first; C4 flattens the compound.
 */
int ja_set_accent_type(ja_vec *v)
{
    char rule[RULE_MAXLEN];
    int i, mora = 0, add;
    ja_word *top = NULL;

    for (i = 0; i < v->n; i++) {
        ja_word *w = &v->w[i];
        if (i == 0 || w->chain_flag != 1) {
            top = w;
            mora = 0;
        } else {
            get_rule(w->chain_rule, v->w[i - 1].pos[0], rule, &add);
            if (EQ(rule, "F2")) {
                if (top->acc == 0)
                    top->acc = mora + add;
            } else if (EQ(rule, "F3")) {
                if (top->acc != 0)
                    top->acc = mora + add;
            } else if (EQ(rule, "F4"))
                top->acc = mora + add;
            else if (EQ(rule, "F5"))
                top->acc = 0;
            else if (EQ(rule, "C1"))
                top->acc = mora + w->acc;
            else if (EQ(rule, "C2"))
                top->acc = mora + 1;
            else if (EQ(rule, "C3"))
                top->acc = mora;
            else if (EQ(rule, "C4"))
                top->acc = 0;
            else if (EQ(rule, "P1"))
                top->acc = w->acc == 0 ? 0 : mora + w->acc;
            else if (EQ(rule, "P2"))
                top->acc = w->acc == 0 ? mora + 1 : mora + w->acc;
            else if (EQ(rule, "P6"))
                top->acc = 0;
            else if (EQ(rule, "P14")) {
                if (w->acc != 0)
                    top->acc = mora + w->acc;
            }
            /* '*', F1 and C5 leave the accent where it is */
        }
        digit_accent(v, i);
        mora += w->mora_size;
    }
    return 0;
}

/* ---- njd_set_unvoiced_vowel --------------------------------------------- */

static int mora_is(const ja_dvm *m, const char *s)
{
    size_t n = strlen(s);
    return (size_t)m->len == n && memcmp(m->mora, s, n) == 0;
}

/* The CANDIDATE is matched exactly: ス devoices, スー does not. */
static int mora_in(const ja_dvm *m, const char *const *list, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        size_t ls = strlen(list[i]);
        if ((size_t)m->len == ls && memcmp(m->mora, list[i], ls) == 0)
            return 1;
    }
    return 0;
}

/*
 * The FOLLOWING mora is matched by PREFIX, which upstream does with
 * strtopcmp and is not the same test.  キャ is not in next_mora_list3, but it
 * starts with キ, which is -- so ツ devoices before キャ.  Matching the whole
 * mora here left 77 morae undevoiced that upstream devoices.
 */
static int mora_starts(const ja_dvm *m, const char *const *list, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        size_t ls = strlen(list[i]);
        if ((size_t)m->len >= ls && memcmp(m->mora, list[i], ls) == 0)
            return 1;
    }
    return 0;
}

/* 1 devoiced, 0 voiced, -1 the rule has nothing to say. */
static int apply_unvoice_rule(const ja_dvm *cur, const ja_dvm *nxt)
{
    if (cur == NULL)
        return -1;
    if (nxt == NULL)
        return 0;
    if (mora_in(cur, ja_uv_candidate_list1, ja_uv_candidate_list1_n))
        return mora_starts(nxt, ja_uv_next_mora_list1,
                           ja_uv_next_mora_list1_n);
    if (mora_in(cur, ja_uv_candidate_list2, ja_uv_candidate_list2_n))
        return mora_starts(nxt, ja_uv_next_mora_list2,
                           ja_uv_next_mora_list2_n);
    if (mora_in(cur, ja_uv_candidate_list3, ja_uv_candidate_list3_n))
        return mora_starts(nxt, ja_uv_next_mora_list3,
                           ja_uv_next_mora_list3_n);
    return -1;
}

/*
 * Six rules, in Open JTalk's order and numbering:
 *
 *   0  a filler never devoices
 *   1  /masu/ and /desu/ devoice their final mora, unless a question mark or
 *      a long vowel follows, where the question has to rise on it
 *   2  /shi/ as a verb, auxiliary or particle, by the same look-ahead
 *   3  NO TWO ADJACENT devoiced morae
 *   4  the mora carrying the ACCENT NUCLEUS does not devoice
 *   5  otherwise the three candidate classes decide
 *
 * Rules 3 and 4 are the two a string-level rule has no equivalent of, and
 * they are not small: of the 199,504 morae such a rule devoiced over all
 * 486,646 pronounced naist-jdic entries, 11.5% were the first of an adjacent
 * pair and 11.2% were accent nuclei -- 20.8% caught by one or the other.
 */
int ja_set_unvoiced_vowel(const ja_vec *v, ja_dvm *out, int cap)
{
    int i, got = 0, acc = 0, midx = 0, n;

    /* flatten to morae, carrying each one's word, its index within its accent
     * phrase, and that phrase's accent type */
    for (i = 0; i < v->n; i++) {
        const ja_word *w = &v->w[i];
        int k, c;
        if (w->chain_flag != 1) {
            midx = 0;
            acc = w->acc;
        }
        c = ja_split_morae(w->pron, out != NULL && got < cap ? out + got : NULL,
                           out != NULL ? cap - got : 0);
        for (k = 0; k < c; k++) {
            if (out != NULL && got + k < cap) {
                out[got + k].word = i;
                out[got + k].midx = midx;
                out[got + k].acc = acc;
            }
            midx++;
        }
        got += c;
    }
    if (out == NULL || got > cap)
        return got;
    n = got;

    for (i = 0; i < n; i++) {
        ja_dvm *e = &out[i];
        const ja_word *w = &v->w[e->word];
        ja_dvm *nx = i + 1 < n ? &out[i + 1] : NULL;
        ja_dvm *nx2 = i + 2 < n ? &out[i + 2] : NULL;

        /*
         * Rules 1 and 2 look AHEAD and settle the next mora, so they run even
         * when this one is already decided -- which it is whenever the
         * dictionary marked it.
         */
        if (e->flag == -1) {
            /* rule 1: the /masu/ and /desu/ look-ahead */
            if (nx != NULL && nx2 != NULL
                && nx->word == e->word && nx2->word != e->word
                && (mora_is(e, JA_UV_MA) || mora_is(e, JA_UV_DE))
                && mora_is(nx, JA_UV_SU)) {
                const ja_word *nw = &v->w[nx->word];
                const ja_word *n2 = &v->w[nx2->word];
                if (EQ(nw->pos[0], JA_UV_DOUSHI) || EQ(nw->pos[0], JA_UV_JODOUSHI)
                    || EQ(nw->pos[0], JA_UV_KANDOUSHI))
                    nx->flag = (EQ(n2->pron, JA_UV_QUESTION)
                                || EQ(n2->pron, JA_UV_CHOUON)) ? 0 : 1;
            }
            /* rule 2: the /shi/ look-ahead */
            if (nx != NULL && nx->flag == -1 && mora_is(nx, JA_UV_SHI)) {
                const ja_word *nw = &v->w[nx->word];
                if (EQ(nw->pron, JA_UV_SHI)
                    && (EQ(nw->pos[0], JA_UV_DOUSHI)
                        || EQ(nw->pos[0], JA_UV_JODOUSHI)
                        || EQ(nw->pos[0], JA_UV_JOSHI))) {
                    if (nx->acc == nx->midx + 1)
                        nx->flag = 0;                           /* rule 4 */
                    else
                        nx->flag = apply_unvoice_rule(nx, nx2); /* rule 5 */
                    if (nx->flag == 1) {
                        e->flag = 0;                            /* rule 3 */
                        if (nx2 != NULL && nx2->flag == -1)
                            nx2->flag = 0;
                    }
                }
            }
        }

        /* Then settle this mora, if nothing above or before it already did. */
        if (e->flag == -1) {
            if (EQ(w->pos[0], JA_UV_FILLER))
                e->flag = 0;                                    /* rule 0 */
            else if (nx != NULL && nx->flag == 1)
                e->flag = 0;                                    /* rule 3 */
            else if (e->acc == e->midx + 1)
                e->flag = 0;                                    /* rule 4 */
            else
                e->flag = apply_unvoice_rule(e, nx);            /* rule 5 */
            if (e->flag == -1)
                e->flag = 0;
        }

        /*
         * Rule 3, FORWARDS, and this is the half that does the work.
         * Upstream ends every iteration of its own loop with
         *
         *     if (flag1 == 1 && flag2 == -1) flag2 = 0;
         *
         * A mora just marked devoiced forces the NEXT UNDECIDED one voiced.
         * The backward test above almost never fires, because the loop runs
         * forwards and the next mora is still undecided when this one is
         * settled.  Without this line adjacent devoicing is permitted: 複数
         * came out フ’ク’スー against upstream's フ’クスー.
         *
         * It runs for EVERY mora, including one the dictionary already
         * marked, because upstream's is outside its own decision branch.
         */
        if (e->flag == 1 && nx != NULL && nx->flag == -1)
            nx->flag = 0;
    }
    return n;
}
