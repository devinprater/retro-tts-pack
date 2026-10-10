/*
 * njd_set_digit: numbers.
 *
 * Translated from build/Japanese_test/jp_digit.py, which was read from Open
 * JTalk 1.11's njd_set_digit.c as a specification.  This is the one stage
 * where 1.11 and the 1.09 line differ in substance -- 211 lines, where every
 * other stage differs only in its copyright year -- and 1.11 is what both
 * front ends follow.
 *
 * What it does, in the order it does it:
 *
 *   PLACE VALUE.  A run of digits becomes a quantity: 1250 is セン ニヒャク
 *   ゴジュー, with 十 百 千 inside each group of four and 万 億 兆 between
 *   groups, and a zero in a place is silenced rather than read.
 *
 *   OR AN IDENTIFIER.  03-1234-5678 is not a quantity, and the context score
 *   says so -- a hyphen either side, a 番号 before it, a bracket -- so it is
 *   read digit by digit with 0, 2 and 5 lengthened to ゼロ ニー ゴー, the
 *   forms used when reading a number out so it cannot be misheard.
 *
 *   COMMA GROUPING, 1.11's addition.  `1,250` is a number and `1,25,0` is
 *   not: counting back from the last digit, a comma must sit at a thousands
 *   position and every thousands position must carry one.  Commas that pass
 *   are removed; commas that fail end the number at the first of them.  A
 *   leading zero makes it an identifier whatever the commas say.
 *
 *   THE COUNTER ASSIMILATIONS.  一本 is イッポン and 三本 サンボン: the digit
 *   changes its reading before some counters and voices or semi-voices the
 *   counter's first mora before others.
 *
 *   THE NAMED EXCEPTIONS, in set_counters: the native numerals (一粒
 *   ヒトツブ), people (一人 ヒトリ), and days of the month (一日 ツイタチ
 *   after a month, 二十日 ハツカ, 十四日 ジューヨッカ).  The native-numeral
 *   table keys on the counter's `read` field, which is why jadic.bin carries
 *   it; see tools/gen_ja_dict.py.
 *
 * No Open JTalk code is copied; the tables are generated into ja_ojt.c.
 */
#include <stdlib.h>
#include <string.h>

#include "ja_njd.h"
#include "ja_ojt.h"

#define EQ(a, b) (strcmp((a), (b)) == 0)
#define STAR "*"

/* A table and the number of strings in it, so one helper can walk any of
 * them: the arity differs per table and each caller knows its own. */
typedef struct {
    const char *const *t;
    int n;
} tbl;

#define T(name) { ja_dt_##name, ja_dt_##name##_n }

/* The eleven classes that change the digit's reading, and the five that
 * change the counter's, paired in upstream's own order because the first
 * match wins. */
static const struct { tbl cls, conv; } DIGIT_CLASSES[] = {
    { T(numerative_class1b),  T(conv_table1b)  },
    { T(numerative_class1c1), T(conv_table1c1) },
    { T(numerative_class1c2), T(conv_table1c2) },
    { T(numerative_class1d),  T(conv_table1d)  },
    { T(numerative_class1e),  T(conv_table1e)  },
    { T(numerative_class1f),  T(conv_table1f)  },
    { T(numerative_class1g),  T(conv_table1g)  },
    { T(numerative_class1h),  T(conv_table1h)  },
    { T(numerative_class1i),  T(conv_table1i)  },
    { T(numerative_class1j),  T(conv_table1j)  },
    { T(numerative_class1k),  T(conv_table1k)  }
};
static const struct { tbl cls, conv; } NUMERATIVE_CLASSES[] = {
    { T(numerative_class2b), T(conv_table2b) },
    { T(numerative_class2c), T(conv_table2c) },
    { T(numerative_class2d), T(conv_table2d) },
    { T(numerative_class2e), T(conv_table2e) },
    { T(numerative_class2f), T(conv_table2f) }
};

static const char *const HAIHUN[] = {
    JA_DC_HAIHUN1, JA_DC_HAIHUN2, JA_DC_HAIHUN3,
    JA_DC_HAIHUN4, JA_DC_HAIHUN5
};
static const char *const ZEROS[] = { JA_DC_ZERO1, JA_DC_ZERO2 };

static int in_tbl(const char *s, const tbl *t)
{
    int i;
    for (i = 0; i < t->n; i++)
        if (EQ(s, t->t[i]))
            return 1;
    return 0;
}

static int in_strs(const char *s, const char *const *list, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (EQ(s, list[i]))
            return 1;
    return 0;
}

static int all_digits(const char *s)
{
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (*s < '0' || *s > '9')
            return 0;
    return 1;
}

/* ---- a word from a table's CSV feature string --------------------------- */

/*
 * NJDNode_load's single-word path: the surface, six part-of-speech fields,
 * the base form, the reading, the pronunciation, accent/mora, the chain rule
 * and optionally the chain flag.
 */
static int word_from_feature(ja_arena *a, const char *feature, ja_word *w)
{
    char *buf = ja_arena_str(a, feature);
    char *f[16];
    int n = 0, i;
    char *p;

    if (buf == NULL)
        return -1;
    f[n++] = buf;
    for (p = buf; *p != '\0' && n < 16; p++)
        if (*p == ',') {
            *p = '\0';
            f[n++] = p + 1;
        }
    for (i = n; i < 13; i++)
        f[i] = STAR;

    ja_word_reset(w);
    w->string = f[0];
    for (i = 0; i < JA_POS_N; i++)
        w->pos[i] = f[1 + i];
    w->read = f[8];
    w->pron = f[9];
    w->chain_rule = f[11];
    if (strchr(f[10], '*') != NULL || strchr(f[10], '/') == NULL) {
        w->acc = w->mora_size = 0;
    } else {
        char *slash = strchr(f[10], '/');
        *slash = '\0';
        w->acc = all_digits(f[10]) ? atoi(f[10]) : 0;
        w->mora_size = all_digits(slash + 1) ? atoi(slash + 1) : 0;
    }
    if (n > 12 && (EQ(f[12], "0") || EQ(f[12], "1")))
        w->chain_flag = atoi(f[12]);
    return 0;
}

/* Replace a word in place from a feature string, as NJDNode_load does. */
static int load_over(ja_arena *a, ja_word *w, const char *feature)
{
    ja_word nw;
    if (word_from_feature(a, feature, &nw) != 0)
        return -1;
    w->string = nw.string;
    memcpy(w->pos, nw.pos, sizeof w->pos);
    w->pron = nw.pron;
    w->read = nw.read;
    w->acc = nw.acc;
    w->mora_size = nw.mora_size;
    w->chain_rule = nw.chain_rule;
    if (nw.chain_flag != -1)
        w->chain_flag = nw.chain_flag;
    return 0;
}

/* ---- the small predicates ----------------------------------------------- */

static int is_period(const char *st)
{
    return st != NULL && (EQ(st, JA_DC_TEN1) || EQ(st, JA_DC_TEN2));
}

static int is_comma(const char *st)
{
    return st != NULL && EQ(st, JA_DC_COMMA);
}

/*
 * The value of a digit word, or -1.  With `convert`, also normalise its
 * spelling to the kanji form the later tables are keyed by, which is how a
 * fullwidth １ and a 一 reach the same rules.
 */
static int get_digit(ja_word *w, int convert)
{
    int i;
    if (EQ(w->string, STAR) || !EQ(w->pos[1], JA_DC_KAZU))
        return -1;
    for (i = 0; i + 2 < ja_dt_numeral_list1_n; i += 3)
        if (EQ(ja_dt_numeral_list1[i], w->string)) {
            if (convert)
                w->string = ja_dt_numeral_list1[i + 2];
            return atoi(ja_dt_numeral_list1[i + 1]);
        }
    return -1;
}

/* Positive: a quantity.  Negative: an identifier, read digit by digit. */
static int sequence_score(const ja_vec *v, int s, int e)
{
    int score = 0;
    const ja_word *w;
    if (s - 1 >= 0) {
        w = &v->w[s - 1];
        if (EQ(w->pos[1], JA_DC_SUUSETSUZOKU))
            score += 2;
        if (EQ(w->pos[2], JA_DC_JOSUUSHI) || EQ(w->pos[1], JA_DC_FUKUSHIKANOU))
            score += 1;
        if (is_period(w->string)) {
            if (s - 2 >= 0 && EQ(v->w[s - 2].pos[1], JA_DC_KAZU))
                score -= 5;
        } else if (in_strs(w->string, HAIHUN, 5)
                   || EQ(w->string, JA_DC_KAKKO2)
                   || EQ(w->string, JA_DC_BANGOU))
            score -= 2;
        else if (EQ(w->string, JA_DC_KAKKO1)) {
            if (s - 2 >= 0 && EQ(v->w[s - 2].pos[1], JA_DC_KAZU))
                score -= 2;
        }
        if (s - 2 >= 0 && EQ(v->w[s - 2].string, JA_DC_BANGOU))
            score -= 2;
    }
    if (e + 1 < v->n) {
        w = &v->w[e + 1];
        if (EQ(w->pos[2], JA_DC_JOSUUSHI) || EQ(w->pos[1], JA_DC_FUKUSHIKANOU))
            score += 2;
        if (in_strs(w->string, HAIHUN, 5) || EQ(w->string, JA_DC_KAKKO1)
            || EQ(w->string, JA_DC_BANGOU))
            score -= 2;
        else if (EQ(w->string, JA_DC_KAKKO2)) {
            if (e + 2 < v->n && EQ(v->w[e + 2].pos[1], JA_DC_KAZU))
                score -= 2;
        } else if (is_period(w->string))
            score += 4;
    }
    return score;
}

/* ---- the two readings of a run ------------------------------------------ */

/*
 * Read each digit, as an identifier rather than a quantity.  0, 2 and 5
 * lengthen, and the digits pair into accent phrases two at a time.
 *
 * A single digit is left exactly as it is.  Both of upstream's conversion
 * functions open with that guard, and dropping it from this one lengthened
 * the digit after a decimal point: 1.5 came out イッテンゴー where the
 * reference gives イッテンゴ.
 */
static void non_numerical(ja_word *run, int n)
{
    int k;
    if (n <= 1)
        return;
    for (k = 0; k < n; k++) {
        ja_word *w = &run[k];
        if (in_strs(w->string, ZEROS, 2)) {
            w->pron = JA_DC_ZERO_AFTER_DP;
            w->mora_size = 2;
        } else if (EQ(w->string, JA_DC_TWO)) {
            w->pron = JA_DC_TWO_AFTER_DP;
            w->mora_size = 2;
        } else if (EQ(w->string, JA_DC_FIVE)) {
            w->pron = JA_DC_FIVE_AFTER_DP;
            w->mora_size = 2;
        }
        w->chain_rule = STAR;
        if (k % 2 == 0)
            w->chain_flag = 0;
        else {
            w->chain_flag = 1;
            run[k - 1].acc = 3;
        }
    }
}

/*
 * Place value: 十 百 千 inside each group of four, 万 億 兆 between groups.
 * Appends to `out`.
 */
static int numerical(ja_arena *a, ja_word *run, int n, ja_vec *out)
{
    int index, place, have = 0, k;

    if (n <= 1) {
        for (k = 0; k < n; k++)
            if (ja_vec_add(out, &run[k]) != 0)
                return -1;
        return 0;
    }
    index = n % 4;
    if (index == 0)
        index = 4;
    place = n > index ? (n - index) / 4 : 0;
    index -= 1;
    if (place > 17) {
        for (k = 0; k < n; k++)
            if (ja_vec_add(out, &run[k]) != 0)
                return -1;
        return 0;
    }
    for (k = 0; k < n; k++) {
        ja_word *w = &run[k];
        int digit = get_digit(w, 0);
        int at;
        if (ja_vec_add(out, w) != 0)
            return -1;
        at = out->n - 1;
        if (index == 0) {
            if (digit == 0) {
                out->w[at].pron = NULL;
                out->w[at].acc = 0;
                out->w[at].mora_size = 0;
            } else
                have = 1;
            if (have == 1) {
                if (place > 0) {
                    ja_word nw;
                    if (place >= ja_dt_numeral_list3_n
                        || word_from_feature(a, ja_dt_numeral_list3[place],
                                             &nw) != 0)
                        return -1;
                    if (ja_vec_add(out, &nw) != 0)
                        return -1;
                }
                have = 0;
            }
            place -= 1;
        } else {
            if (digit <= 0) {
                out->w[at].pron = NULL;
                out->w[at].acc = 0;
                out->w[at].mora_size = 0;
            } else if (digit == 1) {
                /*
                 * 1.11 REPLACES the one with its place name -- 一十 is 十 --
                 * where 1.09 silenced the digit and inserted a new node after
                 * it.  The surviving word sequence is the same either way.
                 */
                ja_word nw;
                if (index >= ja_dt_numeral_list2_n
                    || word_from_feature(a, ja_dt_numeral_list2[index],
                                         &nw) != 0)
                    return -1;
                out->w[at] = nw;
                have = 1;
            } else {
                ja_word nw;
                if (index >= ja_dt_numeral_list2_n
                    || word_from_feature(a, ja_dt_numeral_list2[index],
                                         &nw) != 0)
                    return -1;
                if (ja_vec_add(out, &nw) != 0)
                    return -1;
                have = 1;
            }
        }
        index -= 1;
        if (index < 0)
            index = 3;
    }
    return 0;
}

/*
 * 1.11's recursive driver.  A run may hold digits, commas and periods, and it
 * is cut into pieces that are each read as a quantity or as an identifier.
 * The comma test is the new part: counting back from the last digit before
 * any decimal point, a comma must sit at index 3, 7, 11 and a thousands
 * position must carry one, or this is not a grouped number.
 */
static int convert_sequence(ja_arena *a, ja_vec *in, int s, int e, ja_vec *out)
{
    int fd, numerical_f = 1, n_comma = 0, first_comma = -1, rindex = 0, k, last;

    if (s > e || s >= in->n)
        return 0;
    if (is_comma(in->w[s].string) || is_period(in->w[s].string)) {
        if (ja_vec_add(out, &in->w[s]) != 0)
            return -1;
        if (s != e)
            return convert_sequence(a, in, s + 1, e, out);
        return 0;
    }

    /* the last digit before any period, ignoring a trailing comma */
    fd = s;
    while (fd < e && !is_period(in->w[fd + 1].string))
        fd++;
    while (fd > s && is_comma(in->w[fd].string))
        fd--;

    k = fd;
    for (;;) {
        if (is_comma(in->w[k].string)) {
            first_comma = k;
            n_comma++;
            if (numerical_f == 1 && rindex % 4 != 3)
                numerical_f = 0;
        } else if (numerical_f == 1 && rindex % 4 == 3)
            numerical_f = 0;
        if (k == s)
            break;
        k--;
        rindex++;
    }

    if (s != fd && get_digit(&in->w[s], 0) == 0)
        numerical_f = -1;           /* a leading zero is an identifier */
    if (numerical_f == 1 && n_comma == 0)
        numerical_f = 0;            /* no commas, so no evidence either way */

    if (numerical_f == 1) {
        ja_word *run = (ja_word *)malloc((size_t)(fd - s + 1) * sizeof *run);
        int n = 0, rc;
        if (run == NULL)
            return -1;
        for (k = s; k <= fd; k++)
            if (!is_comma(in->w[k].string))
                run[n++] = in->w[k];
        rc = numerical(a, run, n, out);
        free(run);
        if (rc != 0)
            return -1;
        if (fd != e)
            return convert_sequence(a, in, fd + 1, e, out);
        return 0;
    }

    last = first_comma < 0 ? fd : first_comma - 1;
    if (numerical_f == 0)
        numerical_f = sequence_score(in, s, last) >= 0 ? 1 : -1;
    {
        int n = last - s + 1, rc = 0;
        if (numerical_f == 1)
            rc = numerical(a, &in->w[s], n, out);
        else {
            non_numerical(&in->w[s], n);
            for (k = s; k <= last; k++)
                if (ja_vec_add(out, &in->w[k]) != 0)
                    return -1;
        }
        if (rc != 0)
            return -1;
    }
    if (last != e)
        return convert_sequence(a, in, last + 1, e, out);
    return 0;
}

/* ---- the counter conversions -------------------------------------------- */

static int in_class(const ja_word *w, const tbl *t)
{
    return !EQ(w->string, STAR) && in_tbl(w->string, t);
}

/*
 * Change the DIGIT's reading before a counter -- 四 as ヨ, 七 as シチ.  Each
 * row is four strings: the digit, its new reading, its accent and its morae.
 */
static void convert_digit_pron(const tbl *t, ja_word *w)
{
    int i;
    if (EQ(w->string, STAR))
        return;
    for (i = 0; i + 3 < t->n; i += 4)
        if (EQ(t->t[i], w->string)) {
            const char *acc = t->t[i + 2];
            w->pron = t->t[i + 1];
            w->acc = (acc[0] == '-' ? all_digits(acc + 1) : all_digits(acc))
                ? atoi(acc) : 0;
            w->mora_size = all_digits(t->t[i + 3]) ? atoi(t->t[i + 3]) : 0;
            return;
        }
}

/* Swap the first mora for its voiced or semi-voiced partner. */
static const char *replace_head_mora(ja_arena *a, const char *pron,
                                     const tbl *pairs)
{
    int i;
    for (i = 0; i + 1 < pairs->n; i += 2) {
        size_t ls = strlen(pairs->t[i]);
        if (strncmp(pron, pairs->t[i], ls) == 0) {
            char *p = ja_arena_cat(a, pairs->t[i + 1], pron + ls);
            return p != NULL ? p : pron;
        }
    }
    return pron;
}

/*
 * Voice or semi-voice the COUNTER after a digit: 一本 is イッポン.  The table
 * is pairs of (digit, type); type 1 voices the counter's first mora -- 三本
 * ホン -> ボン -- and type 2 semi-voices it: 一本 ホン -> ポン.
 */
static void convert_numerative_pron(ja_arena *a, const tbl *t,
                                    const ja_word *prev, ja_word *w)
{
    int i, kind = 0;
    if (EQ(prev->string, STAR))
        return;
    for (i = 0; i + 1 < t->n; i += 2)
        if (EQ(t->t[i], prev->string)) {
            kind = all_digits(t->t[i + 1]) ? atoi(t->t[i + 1]) : 0;
            break;
        }
    if (kind == 1) {
        tbl p = T(voiced_sound_symbol_list);
        w->pron = replace_head_mora(a, w->pron, &p);
    } else if (kind == 2) {
        tbl p = T(semivoiced_sound_symbol_list);
        w->pron = replace_head_mora(a, w->pron, &p);
    }
}

/* ---- the named exceptions ----------------------------------------------- */

/* (digit, feature) pairs: replace the digit and silence the counter. */
static int pair_table(ja_arena *a, const tbl *t, ja_word *digit,
                      ja_word *counter)
{
    int i;
    for (i = 0; i + 1 < t->n; i += 2)
        if (EQ(t->t[i], digit->string)) {
            if (load_over(a, digit, t->t[i + 1]) != 0)
                return -1;
            counter->pron = NULL;
            return 1;
        }
    return 0;
}

/*
 * The native numerals: 一粒 is ヒトツブ, 二口 フタクチ.
 *
 * Sixty-two counters take 一 二 in their native readings rather than the
 * Sino-Japanese ones, and the table keys on the counter's SURFACE AND ITS
 * READING together -- 夜 appears twice, once as ヤ and once as ヨ, and 重ね
 * twice, as カサネ and as its voiced ガサネ.  The reading has to be the
 * dictionary's own `read` field and not the pronunciation: 通り reads トオリ
 * and is pronounced トーリ, so keying on the pronunciation would miss 一通り,
 * and the voicing passes that run before this one rewrite the pronunciation
 * while leaving the reading alone.
 *
 * 1.11 removed the 三 row from the conversion table, so 三粒 is サンツブ and
 * not ミツブ.  The row is still in the header, commented out.
 */
static void class3(ja_word *digit, const ja_word *counter)
{
    int i, j;
    for (i = 0; i + 1 < ja_dt_numerative_class3_n; i += 2)
        if (EQ(counter->string, ja_dt_numerative_class3[i])
            && EQ(counter->read, ja_dt_numerative_class3[i + 1])) {
            for (j = 0; j + 3 < ja_dt_conv_table3_n; j += 4)
                if (EQ(digit->string, ja_dt_conv_table3[j])) {
                    digit->read = digit->pron = ja_dt_conv_table3[j + 1];
                    digit->acc = atoi(ja_dt_conv_table3[j + 2]);
                    digit->mora_size = atoi(ja_dt_conv_table3[j + 3]);
                    return;
                }
            return;
        }
}

/*
 * People, days of the month, and spans of days.  These replace the digit and
 * silence the counter, so two words become one -- which is why a missing one
 * showed up as a word-count difference against the reference rather than as a
 * wrong reading.
 */
static int set_counters(ja_arena *a, ja_vec *v)
{
    int k;
    tbl t4 = T(conv_table4), t5 = T(conv_table5), t6 = T(conv_table6);

    for (k = 0; k < v->n; k++) {
        ja_word *w = &v->w[k];
        ja_word *nx = k + 1 < v->n ? &v->w[k + 1] : NULL;
        ja_word *pv = k > 0 ? &v->w[k - 1] : NULL;
        if (nx == NULL || EQ(nx->string, STAR) || !EQ(w->pos[1], JA_DC_KAZU))
            continue;
        /* 1.11 adds the 記号 arm: a digit after a symbol still counts as the
         * first of its run, so 2日 works at the start of a bracketed date. */
        if (!(pv == NULL || EQ(pv->pos[0], JA_DC_KIGOU)
              || !EQ(pv->pos[1], JA_DC_KAZU)))
            continue;
        if (!(EQ(nx->pos[2], JA_DC_JOSUUSHI)
              || EQ(nx->pos[1], JA_DC_FUKUSHIKANOU)))
            continue;
        class3(w, nx);
        if (EQ(nx->string, JA_DC_NIN)) {
            if (pair_table(a, &t4, w, nx) < 0)
                return -1;
        } else if (EQ(nx->string, JA_DC_NICHI) && !EQ(w->string, STAR)) {
            if (pv != NULL && strstr(pv->string, JA_DC_GATSU) != NULL
                && EQ(w->string, JA_DC_ONE)) {
                if (load_over(a, w, JA_DC_TSUITACHI) != 0)
                    return -1;
                nx->pron = NULL;
            } else if (pair_table(a, &t5, w, nx) < 0)
                return -1;
        } else if (EQ(nx->string, JA_DC_NICHIKAN)) {
            if (pair_table(a, &t6, w, nx) < 0)
                return -1;
        }
    }

    /*
     * 十四日 and 二十日 span three or four words, so they are their own pass.
     * 二十日 is ハツカ, not ニジューニチ, and 二十四日 splits as 二十 plus
     * 四日 rather than collapsing, which is why the four-word arm exists.
     */
    for (k = 0; k < v->n; k++) {
        ja_word *w = &v->w[k];
        ja_word *pv = k > 0 ? &v->w[k - 1] : NULL;
        ja_word *x, *y, *z;
        if (pv != NULL && EQ(pv->pos[1], JA_DC_KAZU))
            continue;
        if (k + 2 >= v->n)
            continue;
        x = &v->w[k + 1];
        y = &v->w[k + 2];
        z = k + 3 < v->n ? &v->w[k + 3] : NULL;
        if (EQ(w->string, JA_DC_TEN) && EQ(x->string, JA_DC_FOUR)) {
            if (EQ(y->string, JA_DC_NICHI)) {
                if (load_over(a, w, JA_DC_JUYOKKA) != 0)
                    return -1;
                x->pron = y->pron = NULL;
            } else if (EQ(y->string, JA_DC_NICHIKAN)) {
                if (load_over(a, w, JA_DC_JUYOKKAKAN) != 0)
                    return -1;
                x->pron = y->pron = NULL;
            }
        } else if (EQ(w->string, JA_DC_TWO) && EQ(x->string, JA_DC_TEN)) {
            if (EQ(y->string, JA_DC_NICHI)) {
                if (load_over(a, w, JA_DC_HATSUKA) != 0)
                    return -1;
                x->pron = y->pron = NULL;
            } else if (EQ(y->string, JA_DC_NICHIKAN)) {
                if (load_over(a, w, JA_DC_HATSUKAKAN) != 0)
                    return -1;
                x->pron = y->pron = NULL;
            } else if (EQ(y->string, JA_DC_FOUR) && z != NULL) {
                if (EQ(z->string, JA_DC_NICHI)) {
                    if (load_over(a, w, JA_DC_NIJU) != 0
                        || load_over(a, x, JA_DC_YOKKA) != 0)
                        return -1;
                    y->pron = z->pron = NULL;
                } else if (EQ(z->string, JA_DC_NICHIKAN)) {
                    if (load_over(a, w, JA_DC_NIJU) != 0
                        || load_over(a, x, JA_DC_YOKKAKAN) != 0)
                        return -1;
                    y->pron = z->pron = NULL;
                }
            }
        }
    }
    ja_vec_compact(v);
    return 0;
}

/* ---- the driver --------------------------------------------------------- */

/* 1.11 takes a comma or a period INTO the run when the dictionary tags it 数,
 * which is what lets `1,250` and `1.5` be seen whole. */
static int in_run(ja_word *w)
{
    return get_digit(w, 1) >= 0
        || (EQ(w->pos[1], JA_DC_KAZU)
            && (is_period(w->string) || is_comma(w->string)));
}

int ja_set_digit(ja_arena *a, ja_vec *v)
{
    ja_vec out;
    int found = 0, i, k, rc = -1;

    memset(&out, 0, sizeof out);

    /* Pass 1: expand every run of digits. */
    i = 0;
    while (i < v->n) {
        if (EQ(v->w[i].pos[1], JA_DC_KAZU))
            found = 1;
        if (in_run(&v->w[i])) {
            int j = i;
            while (j + 1 < v->n && in_run(&v->w[j + 1]))
                j++;
            if (convert_sequence(a, v, i, j, &out) != 0)
                goto done;
            i = j + 1;
            continue;
        }
        if (ja_vec_add(&out, &v->w[i]) != 0)
            goto done;
        i++;
    }
    if (!found) {
        /* Nothing numeric: the word list is unchanged, and in particular the
         * spellings get_digit normalised are irrelevant because no table was
         * going to be keyed by them. */
        rc = 0;
        goto done;
    }
    ja_vec_compact(&out);       /* the expansion silenced the unsaid digits */

    /* Pass 2: a decimal point between two digits becomes テン, and the digit
     * before it lengthens if it is 0, 2 or 5. */
    k = 1;
    while (k < out.n - 1) {
        ja_word *w = &out.w[k];
        if (!EQ(w->string, STAR) && !EQ(out.w[k - 1].string, STAR)
            && is_period(w->string)
            && EQ(out.w[k - 1].pos[1], JA_DC_KAZU)
            && EQ(out.w[k + 1].pos[1], JA_DC_KAZU)) {
            ja_word nw, *p;
            if (word_from_feature(a, JA_DC_TEN_FEATURE, &nw) != 0)
                goto done;
            nw.chain_flag = 1;
            out.w[k] = nw;
            p = &out.w[k - 1];
            if (in_strs(p->string, ZEROS, 2)) {
                p->pron = JA_DC_ZERO_BEFORE_DP;
                p->mora_size = 2;
            } else if (EQ(p->string, JA_DC_TWO)) {
                p->pron = JA_DC_TWO_BEFORE_DP;
                p->mora_size = 2;
            } else if (EQ(p->string, JA_DC_FIVE)) {
                p->pron = JA_DC_FIVE_BEFORE_DP;
                p->mora_size = 2;
            } else if (EQ(p->string, JA_DC_SIX))
                p->acc = 1;                         /* new in 1.11 */
            /* and skip past the digits after the point, so a second period in
             * the same number is not treated as another decimal point */
            k++;
            while (k < out.n && EQ(out.w[k].pos[0], JA_DC_MEISHI))
                k++;
            k++;
        } else
            k++;
    }

    /* Pass 3: the counter readings, and the accent phrase around them. */
    for (k = 1; k < out.n; k++) {
        ja_word *w = &out.w[k], *p = &out.w[k - 1];
        size_t c;
        if (!EQ(p->pos[1], JA_DC_KAZU))
            continue;
        if (!(EQ(w->pos[2], JA_DC_JOSUUSHI)
              || EQ(w->pos[1], JA_DC_FUKUSHIKANOU)))
            continue;
        for (c = 0; c < sizeof DIGIT_CLASSES / sizeof *DIGIT_CLASSES; c++)
            if (in_class(w, &DIGIT_CLASSES[c].cls)) {
                convert_digit_pron(&DIGIT_CLASSES[c].conv, p);
                break;
            }
        for (c = 0; c < sizeof NUMERATIVE_CLASSES / sizeof *NUMERATIVE_CLASSES;
             c++)
            if (in_class(w, &NUMERATIVE_CLASSES[c].cls)) {
                convert_numerative_pron(a, &NUMERATIVE_CLASSES[c].conv, p, w);
                break;
            }
        p->chain_flag = 0;
        w->chain_flag = 1;
    }

    /*
     * Pass 4, and it has to be a SECOND pass rather than an else-branch of the
     * one above.  Upstream runs these as two loops, so the digit-adjacency
     * rule is applied after every counter has had its say and overrides it:
     * in 1250円 the counter pass sets 十 to 0 because 円 follows it, and then
     * this pass sets 十 back to 1 because 五 precedes it.  Folded into one
     * loop the order reverses, because the 五/十 pair is reached before the
     * 十/円 pair, and 十 ends up on the wrong side of a phrase boundary.
     */
    for (k = 1; k < out.n; k++) {
        ja_word *w = &out.w[k], *p = &out.w[k - 1];
        tbl l4 = T(numeral_list4), l5 = T(numeral_list5);
        tbl l6 = T(numeral_list6), l7 = T(numeral_list7);
        tbl l8 = T(numeral_list8), l9 = T(numeral_list9);
        tbl l10 = T(numeral_list10), l11 = T(numeral_list11);
        if (!EQ(p->pos[1], JA_DC_KAZU))
            continue;
        if (EQ(w->pos[1], JA_DC_KAZU) && !EQ(p->string, STAR)
            && !EQ(w->string, STAR)) {
            /* two adjacent numbers: 十五 chains, 五十 does not */
            if (in_tbl(p->string, &l4)) {
                if (in_tbl(w->string, &l5)) {
                    p->chain_flag = 0;
                    w->chain_flag = 1;
                }
            } else if (in_tbl(p->string, &l5)) {
                if (in_tbl(w->string, &l4))
                    w->chain_flag = 0;
            }
        }
        if (in_class(w, &l8))
            convert_digit_pron(&l9, p);
        if (in_class(w, &l10))
            convert_digit_pron(&l11, p);
        if (in_class(w, &l6))
            convert_numerative_pron(a, &l7, p, w);
    }

    if (set_counters(a, &out) != 0)
        goto done;

    /* swap the expanded list in */
    free(v->w);
    *v = out;
    memset(&out, 0, sizeof out);
    rc = 0;

done:
    ja_vec_free(&out);
    return rc;
}
