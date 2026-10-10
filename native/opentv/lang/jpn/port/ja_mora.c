/*
 * Kana or romaji in, morae out.
 *
 * A mora is the unit Japanese times by, so this counts in morae rather than
 * syllables: kya is one mora, kyaa is two, and the three special morae -- N
 * (the moraic nasal), Q (the first half of a geminate) and the length mark --
 * each take a mora of their own.
 *
 * Structurally this is build/Japanese_test/jp_mora.py, including its two-stage
 * shape: kana are flattened to a ROMAJI STRING and the romaji is then
 * re-tokenised.  That is not the obvious design -- going straight from kana to
 * morae would skip a step -- but it is the design the Python has, and one of
 * its consequences is load-bearing.  Flattening loses the mora boundary at
 * /N/ before a vowel, so /re N a i/ came back as ['re', 'na', 'i'] and the
 * word lost a mora as well as its nasal; the repair is Hepburn's apostrophe,
 * emitted below, which the romaji parser already understood.  Rebuilt as one
 * stage, that bug class would be gone but the two front ends would no longer
 * agree, and agreement is what the oracle tests.
 */
#include <stdlib.h>
#include <string.h>

#include "ja.h"
#include "ja_ojt.h"

/* ---- UTF-8 in -------------------------------------------------------------
 *
 * Invalid bytes are taken as the Latin-1 character of the same value rather
 * than rejected: a caller handing us CP1252 romaji should be understood, not
 * silenced, and nothing in the romaji half of the parser can be reached by a
 * byte above 0x7f anyway.
 */
static int decode_utf8(const char *t, size_t len, uint32_t *cp, int cap)
{
    const unsigned char *p = (const unsigned char *)t;
    size_t i = 0;
    int n = 0;

    while (i < len && n < cap) {
        uint32_t u = p[i];
        int extra, k;

        if (u < 0x80)       { i++; cp[n++] = u; continue; }
        else if (u >= 0xf0) { extra = 3; u &= 0x07; }
        else if (u >= 0xe0) { extra = 2; u &= 0x0f; }
        else if (u >= 0xc0) { extra = 1; u &= 0x1f; }
        else                { i++; cp[n++] = p[i - 1]; continue; }
        if (i + (size_t)extra >= len) {       /* truncated: take it as bytes */
            cp[n++] = p[i];
            i++;
            continue;
        }
        for (k = 1; k <= extra; k++) {
            if ((p[i + (size_t)k] & 0xc0) != 0x80)
                break;
            u = (u << 6) | (uint32_t)(p[i + (size_t)k] & 0x3f);
        }
        if (k <= extra) {                     /* a bad continuation byte */
            cp[n++] = p[i];
            i++;
            continue;
        }
        i += (size_t)extra + 1;
        cp[n++] = u;
    }
    return n;
}

/* ---- the compatibility subset of NFKC ------------------------------------
 *
 * The Python normalises NFKC before reading kana.  Full NFKC is not worth
 * carrying here, and almost none of it is reachable: whatever survives this
 * step is looked up in a table of kana and then filtered to [a-z- '|], so a
 * character that normalises to anything else is dropped either way.  What IS
 * reachable, and does appear in real text, is halfwidth katakana -- legacy
 * data is full of it -- and fullwidth ASCII.  Those are done; the rest is not,
 * and this comment is the honest statement of which.
 */
static const uint16_t halfwidth[0x40] = {
    /* FF61 */ 0x3002, 0x300c, 0x300d, 0x3001, 0x30fb, 0x30f2,
    /* FF67 */ 0x30a1, 0x30a3, 0x30a5, 0x30a7, 0x30a9,
    /* FF6C */ 0x30e3, 0x30e5, 0x30e7, 0x30c3,
    /* FF70 */ 0x30fc,
    /* FF71 */ 0x30a2, 0x30a4, 0x30a6, 0x30a8, 0x30aa,
    /* FF76 */ 0x30ab, 0x30ad, 0x30af, 0x30b1, 0x30b3,
    /* FF7B */ 0x30b5, 0x30b7, 0x30b9, 0x30bb, 0x30bd,
    /* FF80 */ 0x30bf, 0x30c1, 0x30c4, 0x30c6, 0x30c8,
    /* FF85 */ 0x30ca, 0x30cb, 0x30cc, 0x30cd, 0x30ce,
    /* FF8A */ 0x30cf, 0x30d2, 0x30d5, 0x30d8, 0x30db,
    /* FF8F */ 0x30de, 0x30df, 0x30e0, 0x30e1, 0x30e2,
    /* FF94 */ 0x30e4, 0x30e6, 0x30e8,
    /* FF97 */ 0x30e9, 0x30ea, 0x30eb, 0x30ec, 0x30ed,
    /* FF9C */ 0x30ef, 0x30f3,
    /* FF9E */ 0x3099, 0x309a
};

/* Which katakana take a dakuten by adding one to their code point.  NFKC
 * composes the halfwidth mark onto the base, so ｶ + ﾞ really is ガ. */
static int voices(uint32_t k)
{
    switch (k) {
    case 0x30ab: case 0x30ad: case 0x30af: case 0x30b1: case 0x30b3:
    case 0x30b5: case 0x30b7: case 0x30b9: case 0x30bb: case 0x30bd:
    case 0x30bf: case 0x30c1: case 0x30c4: case 0x30c6: case 0x30c8:
    case 0x30cf: case 0x30d2: case 0x30d5: case 0x30d8: case 0x30db:
        return 1;
    default:
        return 0;
    }
}

static int semivoices(uint32_t k)
{
    return k == 0x30cf || k == 0x30d2 || k == 0x30d5 ||
           k == 0x30d8 || k == 0x30db;
}

static int normalize(uint32_t *cp, int n)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        uint32_t u = cp[i];

        if (u >= 0xff61 && u <= 0xff9f) {
            u = halfwidth[u - 0xff61];
            if (i + 1 < n && cp[i + 1] == 0xff9e && voices(u)) {
                u += 1;
                i++;
            } else if (i + 1 < n && cp[i + 1] == 0xff9e && u == 0x30a6) {
                u = 0x30f4;             /* ｳ + ﾞ is ヴ, not ウ plus one */
                i++;
            } else if (i + 1 < n && cp[i + 1] == 0xff9f && semivoices(u)) {
                u += 2;
                i++;
            }
        } else if (u >= 0xff01 && u <= 0xff5e) {
            u -= 0xfee0;                /* fullwidth ASCII */
        } else if (u == 0x3000) {
            u = ' ';                    /* the ideographic space */
        }
        cp[m++] = u;
    }
    return m;
}

/* ---- kana -> romaji ------------------------------------------------------ */

/*
 * The hiragana block, U+3041 upward.  NULL is a character the Python's KANA
 * table has no row for either, and those are dropped -- which is a real thing
 * that happened: katakana VU was missing, so the character was silently thrown
 * away and `violin` came back as "iorin".  It is ゔ here, mapped to /b/, which
 * is what the kana was invented to transcribe and what most speakers say.
 */
static const char *const kana[0x56] = {
    "\x01""a",  "a",                            /* ぁ あ */
    "\x01""i",  "i",                            /* ぃ い */
    "\x01""u",  "u",                            /* ぅ う */
    "\x01""e",  "e",                            /* ぇ え */
    "\x01""o",  "o",                            /* ぉ お */
    "ka", "ga", "ki", "gi", "ku", "gu", "ke", "ge", "ko", "go",
    "sa", "za", "shi", "ji", "su", "zu", "se", "ze", "so", "zo",
    "ta", "da", "chi", "ji", "q", "tsu", "zu", "te", "de", "to", "do",
    "na", "ni", "nu", "ne", "no",
    "ha", "ba", "pa", "hi", "bi", "pi", "fu", "bu", "pu",
    "he", "be", "pe", "ho", "bo", "po",
    "ma", "mi", "mu", "me", "mo",
    "\x02""ya", "ya", "\x02""yu", "yu", "\x02""yo", "yo",   /* ゃ や ... */
    "ra", "ri", "ru", "re", "ro",
    NULL,                                       /* ゎ */
    "wa",
    NULL, NULL,                                 /* ゐ ゑ */
    "o",                                        /* を */
    "n",                                        /* ん */
    "bu",                                       /* ゔ */
    NULL, NULL                                  /* ゕ ゖ */
};

/* The two prefixes above mark the small kana, which modify the one before
 * them rather than standing on their own. */
#define K_SMALLV '\x01'
#define K_YOON   '\x02'

static const char *kana_of(uint32_t u)
{
    if (u >= 0x30a1 && u <= 0x30f6)
        u -= 0x60;                              /* katakana to hiragana */
    if (u == 0x30fc)
        return "-";                             /* the length mark */
    if (u >= 0x3041 && u <= 0x3096)
        return kana[u - 0x3041];
    return NULL;
}

/* The romaji a small kana carries, with its marker stripped. */
static const char *plain(const char *r)
{
    return (r != NULL && (*r == K_SMALLV || *r == K_YOON)) ? r + 1 : r;
}

static int is_small(const char *r, char which)
{
    return r != NULL && *r == which;
}

/* `fu`, `u`, `te`, `de`, `tsu` and `bu` are the kana a small vowel attaches
 * to: ファ, ウィ, ティ, ディ, ツァ, ヴァ. */
static int takes_smallv(const char *r)
{
    return !strcmp(r, "fu") || !strcmp(r, "u") || !strcmp(r, "te") ||
           !strcmp(r, "de") || !strcmp(r, "tsu") || !strcmp(r, "bu");
}

static int kana_to_romaji(const uint32_t *cp, int n, char *out, int cap)
{
    int i, m = 0;

    for (i = 0; i < n; i++) {
        const char *r = kana_of(cp[i]);
        const char *nx = (i + 1 < n) ? kana_of(cp[i + 1]) : NULL;
        char buf[8];
        size_t rl;

        if (r == NULL || *r == K_SMALLV || *r == K_YOON) {
            /* Not a standalone kana.  A small kana reached here has nothing
             * before it to modify, so it is dropped, exactly as the Python's
             * dict lookup drops it. */
            if (cp[i] == ' ' || cp[i] == 0x3000) {
                if (m + 1 < cap)
                    out[m++] = ' ';
            } else if (cp[i] == '|') {
                /*
                 * The phrase bars pass through, which is one thing this does
                 * that the Python does not.  It has to: the Python's callers
                 * write bars in ROMAJI, so its kana reader never sees one and
                 * drops them with everything else that is not kana -- and
                 * tvtts_ja.c rewrites Japanese punctuation INTO bars before
                 * calling this, so on the kana path every full stop and comma
                 * would be swallowed here.  That is exactly what happened:
                 * the pauses were written and then thrown away two functions
                 * later, and the two spellings came out the same length.
                 *
                 * This cannot move the oracle.  None of the 533 reference
                 * words contains a bar.
                 */
                if (m + 1 < cap)
                    out[m++] = '|';
            }
            continue;
        }
        rl = strlen(r);
        if (is_small(nx, K_YOON) && rl >= 2) {
            /* ki + ya -> kya, shi + ya -> sha, chi + ya -> cha, ji + ya -> ja */
            const char *y = plain(nx);
            char v = y[strlen(y) - 1];

            memcpy(buf, r, rl - 1);
            buf[rl - 1] = '\0';
            if (!strcmp(buf, "sh") || !strcmp(buf, "ch") || !strcmp(buf, "j")) {
                buf[rl - 1] = v;
                buf[rl] = '\0';
            } else {
                buf[rl - 1] = 'y';
                buf[rl] = v;
                buf[rl + 1] = '\0';
            }
            r = buf;
            rl = strlen(buf);
            i++;
        } else if (is_small(nx, K_SMALLV) && takes_smallv(r)) {
            memcpy(buf, r, rl - 1);
            buf[rl - 1] = plain(nx)[0];
            buf[rl] = '\0';
            r = buf;
            i++;
        } else if (rl == 1 && *r == 'n' && nx != NULL &&
                   *nx != K_SMALLV && *nx != K_YOON) {
            /* The moraic nasal before a vowel or a y-mora.  Flattening to a
             * string and re-tokenising swallows the boundary without this.
             *
             * A SMALL kana does not count, because the Python looks this one
             * up in KANA and the small kana live in two side tables -- so
             * /N/ before a small ya is left bare there and is left bare here.
             */
            char f = nx[0];

            if (f == 'a' || f == 'i' || f == 'u' || f == 'e' || f == 'o' ||
                f == 'y') {
                r = "n'";
                rl = 2;
            }
        }
        if (m + (int)rl >= cap)
            break;
        memcpy(out + m, r, rl);
        m += (int)rl;
    }
    out[m] = '\0';
    return m;
}

/* ---- romaji -> morae ----------------------------------------------------- */

typedef struct { const char *s; signed char c, v; } cv_row;

/* Longest first, so "sh"/"ch"/"ts" and the y-series win over the bare
 * consonant.  These are whole morae; the split into consonant and vowel is
 * done here rather than by re-reading the string afterwards. */
static const cv_row onsets[] = {
    { "kya", JA_C_KY, JA_V_A }, { "kyu", JA_C_KY, JA_V_U },
    { "kyo", JA_C_KY, JA_V_O },
    { "gya", JA_C_GY, JA_V_A }, { "gyu", JA_C_GY, JA_V_U },
    { "gyo", JA_C_GY, JA_V_O },
    { "sha", JA_C_SH, JA_V_A }, { "shu", JA_C_SH, JA_V_U },
    { "sho", JA_C_SH, JA_V_O }, { "shi", JA_C_SH, JA_V_I },
    { "ja",  JA_C_J,  JA_V_A }, { "ju",  JA_C_J,  JA_V_U },
    { "jo",  JA_C_J,  JA_V_O }, { "ji",  JA_C_J,  JA_V_I },
    { "cha", JA_C_CH, JA_V_A }, { "chu", JA_C_CH, JA_V_U },
    { "cho", JA_C_CH, JA_V_O }, { "chi", JA_C_CH, JA_V_I },
    { "tsu", JA_C_TS, JA_V_U },
    { "nya", JA_C_NY, JA_V_A }, { "nyu", JA_C_NY, JA_V_U },
    { "nyo", JA_C_NY, JA_V_O },
    { "hya", JA_C_HY, JA_V_A }, { "hyu", JA_C_HY, JA_V_U },
    { "hyo", JA_C_HY, JA_V_O },
    { "bya", JA_C_BY, JA_V_A }, { "byu", JA_C_BY, JA_V_U },
    { "byo", JA_C_BY, JA_V_O },
    { "pya", JA_C_PY, JA_V_A }, { "pyu", JA_C_PY, JA_V_U },
    { "pyo", JA_C_PY, JA_V_O },
    { "mya", JA_C_MY, JA_V_A }, { "myu", JA_C_MY, JA_V_U },
    { "myo", JA_C_MY, JA_V_O },
    { "rya", JA_C_RY, JA_V_A }, { "ryu", JA_C_RY, JA_V_U },
    { "ryo", JA_C_RY, JA_V_O },
    { "fu",  JA_C_F,  JA_V_U },
    { NULL, 0, 0 }
};

static const cv_row cons[] = {
    { "ky", JA_C_KY, 0 }, { "gy", JA_C_GY, 0 }, { "sh", JA_C_SH, 0 },
    { "ch", JA_C_CH, 0 }, { "ts", JA_C_TS, 0 }, { "ny", JA_C_NY, 0 },
    { "hy", JA_C_HY, 0 }, { "by", JA_C_BY, 0 }, { "py", JA_C_PY, 0 },
    { "my", JA_C_MY, 0 }, { "ry", JA_C_RY, 0 },
    { "k", JA_C_K, 0 }, { "g", JA_C_G, 0 }, { "s", JA_C_S, 0 },
    { "z", JA_C_Z, 0 }, { "t", JA_C_T, 0 }, { "d", JA_C_D, 0 },
    { "n", JA_C_N, 0 }, { "h", JA_C_H, 0 }, { "b", JA_C_B, 0 },
    { "p", JA_C_P, 0 }, { "m", JA_C_M, 0 }, { "y", JA_C_Y, 0 },
    { "r", JA_C_R, 0 }, { "w", JA_C_W, 0 }, { "f", JA_C_F, 0 },
    { "j", JA_C_J, 0 }, { "v", JA_C_V, 0 },
    { NULL, 0, 0 }
};

static int vowel_index(char c)
{
    switch (c) {
    case 'a': return JA_V_A;
    case 'i': return JA_V_I;
    case 'u': return JA_V_U;
    case 'e': return JA_V_E;
    case 'o': return JA_V_O;
    default:  return JA_V_NONE;
    }
}

static char vowel_char(int v)
{
    static const char s[] = "aiueo";

    return (v >= 0 && v < JA_NV) ? s[v] : 0;
}

static int emit(ja_mora *out, int cap, int n, int kind, int c, int v)
{
    if (n < cap) {
        out[n].kind = (unsigned char)kind;
        out[n].c = (signed char)c;
        out[n].v = (signed char)v;
    }
    return n + 1;
}

/*
 * `skipped`, when it is not NULL, counts the characters this could not place.
 * That number is what decides whether a Latin string is romaji at all: the
 * parser is lenient by design -- it has to be, because it is fed generated
 * romaji from the kana path and should not reject a stray mark -- and that
 * leniency is silent.  Fed an English word it quietly drops the letters
 * Japanese has no mora for, so `hello` came out /he.Q.o/ and `blorf` came out
 * as the single mora /o/.  Reporting the drops lets the caller refuse a
 * reading that lost most of its input instead of speaking it.
 */
static int romaji_to_morae(const char *s, ja_mora *out, int cap, int *skipped)
{
    int i = 0, n = 0, len = (int)strlen(s);
    /*
     * The vowel the previous mora ended on, or 0.  The Python reads
     * out[-1][-1] and excludes 'N', 'Q' and ' ' -- but not ':' or '|', whose
     * last characters are not vowels and so can never match anyway.  Keeping
     * only the vowel reproduces all three tests exactly.
     */
    char prev = 0;

    while (i < len) {
        char ch = s[i];
        const cv_row *r;
        int v;

        /*
         * Two levels of boundary above the mora.  A space is an ACCENT PHRASE
         * boundary: no pause, the articulation runs straight through, but the
         * pitch gets a fresh accent command.  A bar is a MAJOR PHRASE
         * boundary: a pause, a new Fujisaki phrase command, and downstep
         * resets.  Kawai's rules decide where these go from clause and ICRLB
         * boundaries, which needs a parser nobody has written, so the symbols
         * are exposed rather than derived.
         */
        if (ch == '|') {
            if (i + 1 < len && s[i + 1] == '|') {
                n = emit(out, cap, n, JA_M_BARBAR, 0, JA_V_NONE);
                i += 2;
            } else {
                n = emit(out, cap, n, JA_M_BAR, 0, JA_V_NONE);
                i += 1;
            }
            prev = 0;
            continue;
        }
        if (ch == ' ') {
            n = emit(out, cap, n, JA_M_SP, 0, JA_V_NONE);
            i++; prev = 0; continue;
        }
        if (ch == '-') {                        /* chouon */
            n = emit(out, cap, n, JA_M_LONG, 0, JA_V_NONE);
            i++; prev = 0; continue;
        }
        if (ch == '\'') {
            i++; continue;                      /* n' -- just a reading aid */
        }
        if (ch == 'q') {                        /* the kana path writes 'q' */
            n = emit(out, cap, n, JA_M_Q, 0, JA_V_NONE);
            i++; prev = 0; continue;
        }
        /* the moraic nasal: n not followed by a vowel or y */
        if (ch == 'n' && (i + 1 >= len ||
                          (s[i + 1] != 'a' && s[i + 1] != 'i' &&
                           s[i + 1] != 'u' && s[i + 1] != 'e' &&
                           s[i + 1] != 'o' && s[i + 1] != 'y'))) {
            n = emit(out, cap, n, JA_M_N, 0, JA_V_NONE);
            i++; prev = 0; continue;
        }
        /* a doubled consonant is Q plus the consonant */
        if (i + 1 < len && s[i] == s[i + 1] && vowel_index(ch) < 0 &&
            ch != 'n') {
            n = emit(out, cap, n, JA_M_Q, 0, JA_V_NONE);
            i++; prev = 0; continue;
        }
        for (r = onsets; r->s != NULL; r++) {
            if (!strncmp(s + i, r->s, strlen(r->s))) {
                n = emit(out, cap, n, JA_M_CV, r->c, r->v);
                i += (int)strlen(r->s);
                prev = vowel_char(r->v);
                break;
            }
        }
        if (r->s != NULL)
            continue;
        for (r = cons; r->s != NULL; r++) {
            size_t cl = strlen(r->s);

            if (!strncmp(s + i, r->s, cl) && i + (int)cl < len &&
                vowel_index(s[i + cl]) >= 0) {
                int vv = vowel_index(s[i + cl]);

                n = emit(out, cap, n, JA_M_CV, r->c, vv);
                i += (int)cl + 1;
                prev = vowel_char(vv);
                break;
            }
        }
        if (r->s != NULL)
            continue;
        v = vowel_index(ch);
        if (v >= 0) {
            /* a repeated vowel is a long one; so, by convention, are "ou" and
             * "ei", which is how long o and long e are usually written */
            if (prev == ch || (prev == 'o' && ch == 'u') ||
                (prev == 'e' && ch == 'i')) {
                n = emit(out, cap, n, JA_M_LONG, 0, JA_V_NONE);
                prev = 0;
            } else {
                n = emit(out, cap, n, JA_M_CV, JA_C_NONE, v);
                prev = ch;
            }
            i++;
            continue;
        }
        i++;
        if (skipped != NULL)
            (*skipped)++;
    }
    return n;
}

/* ---- the entry point ----------------------------------------------------- */

int ja_to_morae_ex(const char *text, size_t len, ja_mora *out, int cap,
                   int *skipped)
{
    uint32_t *cp;
    char *buf;
    size_t bcap;
    int n, i, kana_path = 0, m;

    if (skipped != NULL)
        *skipped = 0;
    if (text == NULL || out == NULL || cap <= 0 || len == 0)
        return 0;
    /* A code point is never fewer than one byte, and the longest romaji a
     * single kana produces is three characters ("shi", "tsu"), so these
     * bounds cannot be reached rather than merely being generous. */
    cp = (uint32_t *)malloc((len + 1) * sizeof *cp);
    bcap = len * 3 + 4;
    buf = (char *)malloc(bcap);
    if (cp == NULL || buf == NULL) {
        free(cp);
        free(buf);
        return 0;
    }
    n = decode_utf8(text, len, cp, (int)len);
    for (i = 0; i < n; i++)
        if (cp[i] > 0x2000) {
            kana_path = 1;
            break;
        }
    if (kana_path) {
        n = normalize(cp, n);
        kana_to_romaji(cp, n, buf, (int)bcap);
    } else {
        /*
         * Romaji.  Lowercased, with the two macron vowels spelled out, and
         * then everything outside [a-z- '|] dropped -- which is where digits
         * and punctuation go.  Only A-Z and those two macrons can turn INTO a
         * character the filter keeps, so an ASCII fold is the whole of what
         * the Python's full Unicode lower() does that is observable here.
         */
        m = 0;
        for (i = 0; i < n && m + 2 < (int)bcap; i++) {
            uint32_t u = cp[i];

            if (u >= 'A' && u <= 'Z')
                u += 0x20;
            if (u == 0x014c || u == 0x014d) {           /* O with macron */
                buf[m++] = 'o'; buf[m++] = 'o';
                continue;
            }
            if (u == 0x016a || u == 0x016b) {           /* U with macron */
                buf[m++] = 'u'; buf[m++] = 'u';
                continue;
            }
            if ((u >= 'a' && u <= 'z') || u == '-' || u == ' ' ||
                u == '\'' || u == '|')
                buf[m++] = (char)u;
            else if (skipped != NULL)
                (*skipped)++;
        }
        buf[m] = '\0';
    }
    n = romaji_to_morae(buf, out, cap, skipped);
    free(cp);
    free(buf);
    return n < cap ? n : cap;          /* a truncated utterance, not a lie */
}

int ja_to_morae(const char *text, size_t len, ja_mora *out, int cap)
{
    return ja_to_morae_ex(text, len, out, cap, NULL);
}

/* ---- romaji back to katakana -------------------------------------------- */

/*
 * The inverse of the mora table, DERIVED from it rather than written out.
 *
 * ja_MORA is Open JTalk's 159-mora inventory in katakana, and ja_to_morae
 * already maps katakana to a mora, so running one over the other gives the
 * map back.  That matters because a hand-written inverse is 159 rows of
 * katakana typed a second time, which is exactly the transcription this
 * project has been bitten by; this one cannot disagree with the forward
 * direction because it is built out of it.
 *
 * WHICH KATAKANA, when several give the same mora.  The inventory makes
 * distinctions the mora symbols do not -- ヴ and ブ are both /bu/, スィ and
 * ス both /su/, トゥ and ト both /to/, ジ and ヂ both /ji/ -- and taking the
 * first in upstream's order takes the WRONG one, because that order is
 * longest-first for prefix matching and so puts the exotic spelling first.
 * `bukku` came out ヴック.
 *
 * So: fewest characters, then lowest code point.  That picks ブ over ヴ, ス
 * over スィ, ト over トゥ, イ over ウィ and ジ over ヂ, which is the
 * canonical spelling in every case -- and it is a rule over the data rather
 * than a second table to keep in step.
 *
 * It is not cosmetic.  The devoicing stage's candidate lists are keyed by
 * exact katakana, and トゥ is a candidate where ト is not.
 */
static const char *inv[JA_NC + 8][JA_NV + 4];
static const char *inv_special[8];
static int inv_built;

/*
 * The same inverse keyed by the mora's SYMBOL -- 'ka', 'kya', 'N', 'Q', ':'
 * -- which is what a caller holding a list of morae has.
 *
 * It exists because the obvious alternative does not work: serialising the
 * morae back to romaji and re-parsing them re-applies the parser's own
 * conventions, and a repeated vowel or an `ei` becomes a LONG vowel.  So
 * `sakura` read by the rules came out シクアー instead of シクアア and `take`
 * テーク instead of テイク.  A symbol is already resolved; it must not be
 * parsed again.
 */
#define INV_SYM_MAX 256
static struct { char sym[8]; const char *kana; } inv_sym[INV_SYM_MAX];
static int inv_sym_n;

/* Fewer characters first, then the lower code point; see build_inverse. */
static int better(const char *a, const char *b)
{
    if (b == NULL)
        return 1;
    {
        size_t la = strlen(a), lb = strlen(b);
        if (la != lb)
            return la < lb;
    }
    return strcmp(a, b) < 0;
}

static void build_inverse(void)
{
    int i;

    if (inv_built)
        return;
    for (i = 0; i < ja_MORA_N; i++) {
        ja_mora m[4];
        int n = ja_to_morae(ja_MORA[i], strlen(ja_MORA[i]), m, 4);
        if (n != 1)
            continue;
        if (m[0].kind == JA_M_CV) {
            int c = m[0].c + 1, v = m[0].v + 1;   /* +1: JA_V_NONE is -1 */
            if (c >= 0 && c < JA_NC + 8 && v >= 0 && v < JA_NV + 4
                && better(ja_MORA[i], inv[c][v]))
                inv[c][v] = ja_MORA[i];
        } else if (m[0].kind < 8
                   && better(ja_MORA[i], inv_special[m[0].kind]))
            inv_special[m[0].kind] = ja_MORA[i];
        {   /* and by symbol, with the same preference */
            char sym[8];
            int k, at = -1;
            if (ja_mora_symbol(&m[0], sym, sizeof sym) <= 0)
                continue;
            for (k = 0; k < inv_sym_n; k++)
                if (strcmp(inv_sym[k].sym, sym) == 0) {
                    at = k;
                    break;
                }
            if (at < 0) {
                if (inv_sym_n >= INV_SYM_MAX)
                    continue;
                at = inv_sym_n++;
                strcpy(inv_sym[at].sym, sym);
                inv_sym[at].kana = NULL;
            }
            if (better(ja_MORA[i], inv_sym[at].kana))
                inv_sym[at].kana = ja_MORA[i];
        }
    }
    inv_built = 1;
}

const char *ja_mora_kana(const char *sym)
{
    int i;
    build_inverse();
    for (i = 0; i < inv_sym_n; i++)
        if (strcmp(inv_sym[i].sym, sym) == 0)
            return inv_sym[i].kana;
    return NULL;
}

/*
 * Romaji in, katakana out, and the count of characters the parser could not
 * place.  Returns the bytes wanted including the NUL, snprintf-style, or -1
 * if a mora came out that the inventory has no katakana for.
 *
 * This is what lets a Latin token the dictionary does not know be re-read as
 * Japanese: the rest of the pipeline wants a katakana pronunciation, because
 * that is what the mora splitter, the devoicing stage and the mora count all
 * read.
 */
int ja_romaji_to_kana(const char *text, size_t len, char *out, size_t cap,
                      int *skipped)
{
    ja_mora *m;
    int n, i;
    size_t w = 0;

    if (skipped != NULL)
        *skipped = 0;
    if (text == NULL || len == 0)
        return -1;
    build_inverse();
    m = (ja_mora *)malloc((len + 1) * sizeof *m);
    if (m == NULL)
        return -1;
    n = ja_to_morae_ex(text, len, m, (int)len + 1, skipped);
    for (i = 0; i < n; i++) {
        const char *k;
        size_t kl;
        if (m[i].kind == JA_M_CV) {
            int c = m[i].c + 1, v = m[i].v + 1;
            k = (c >= 0 && c < JA_NC + 8 && v >= 0 && v < JA_NV + 4)
                ? inv[c][v] : NULL;
        } else
            k = m[i].kind < 8 ? inv_special[m[i].kind] : NULL;
        if (k == NULL) {
            free(m);
            return -1;              /* no katakana for that mora */
        }
        kl = strlen(k);
        if (w + kl + 1 <= cap)
            memcpy(out + w, k, kl);
        w += kl;
    }
    if (cap != 0)
        out[w < cap ? w : cap - 1] = '\0';
    free(m);
    return (int)w + 1;
}

int ja_onset_of(const ja_mora *m)
{
    switch (m->kind) {
    case JA_M_CV:   return m->c;
    case JA_M_N:    return JA_C_MN;
    case JA_M_Q:    return JA_C_QMARK;
    case JA_M_LONG: return JA_C_LONGM;
    default:        return JA_C_BOUND;
    }
}

int ja_is_boundary(const ja_mora *m)
{
    return m->kind == JA_M_SP || m->kind == JA_M_BAR ||
           m->kind == JA_M_BARBAR;
}

/*
 * A mora back to the symbol the Python front end names it by: 'N', 'Q', ':',
 * ' ', '|', '||' and the CV romaji.  The symbol alphabet is NOT the romaji
 * this file parses -- the parser takes 'n', 'q' and '-' on input -- it is
 * jp_mora.to_morae's output, which is what front_oracle.tsv records and what
 * every diagnostic prints.  So this is the one function that has to agree
 * with the Python's spelling of a mora, and `ja_check front` is where that is
 * checked, over every mora of 1,900 texts.
 *
 * Writes at most cap bytes including the NUL and returns the length it
 * wanted, so a short buffer is detectable.  An unrenderable mora gives 0.
 */
int ja_mora_symbol(const ja_mora *m, char *buf, size_t cap)
{
    char tmp[8];
    size_t n = 0;
    const cv_row *r;

    switch (m->kind) {
    case JA_M_N:      strcpy(tmp, "N");  break;
    case JA_M_Q:      strcpy(tmp, "Q");  break;
    case JA_M_LONG:   strcpy(tmp, ":");  break;
    case JA_M_SP:     strcpy(tmp, " ");  break;
    case JA_M_BAR:    strcpy(tmp, "|");  break;
    case JA_M_BARBAR: strcpy(tmp, "||"); break;
    default:
        /*
         * The irregular spellings are WHOLE MORAE in `onsets` -- shi, tsu,
         * cha, kyo, fu -- and already carry their vowel, so a match there is
         * the answer and nothing is appended.  Everything else is a consonant
         * prefix from `cons` plus its vowel letter: ta, tsa, fe, ni.
         */
        for (r = onsets; r->s != NULL; r++)
            if (r->c == m->c && r->v == m->v) {
                strcpy(tmp, r->s);
                goto got;
            }
        tmp[0] = '\0';
        if (m->c != JA_C_NONE) {
            for (r = cons; r->s != NULL; r++)
                if (r->c == m->c) {
                    strcpy(tmp, r->s);
                    break;
                }
            if (tmp[0] == '\0')
                return 0;
        }
        {
            char v = vowel_char(m->v);
            if (v == 0)
                return 0;
            n = strlen(tmp);
            tmp[n] = v;
            tmp[n + 1] = '\0';
        }
    got:
        break;
    }
    n = strlen(tmp);
    if (cap != 0) {
        size_t k = n < cap - 1 ? n : cap - 1;
        memcpy(buf, tmp, k);
        buf[k] = '\0';
    }
    return (int)n;
}
