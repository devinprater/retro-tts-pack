/*
 * F0: the Fujisaki command-response model, and Kawai's rules for where the
 * commands go.  A translation of fujisaki.py and jp_speak.pitch().
 *
 *     ln F0(t) = ln Fb
 *              + SUM_i  Api * Gp(t - T0i)                    phrase commands
 *              + SUM_j  Aaj * [ Ga(t - T1j) - Ga(t - T2j) ]  accent commands
 *
 * Both control mechanisms are critically-damped second-order systems; the
 * phrase command is an impulse and the accent command a step (Hirose, Sakata,
 * Osame & Fujisaki, SSW2 1994, p.168).  So
 *
 *     Gp(t) = a^2 * t * exp(-a*t)                      t >= 0, else 0
 *     Ga(t) = min[ 1 - (1 + b*t) * exp(-b*t), theta ]  t >= 0, else 0
 *
 * a and b are the natural angular frequencies of the two mechanisms.  They are
 * not in the papers the project holds -- those reference Fujisaki & Hirose,
 * JASJ(E) 5(4) 233-242 (1984), which it does not have -- and published values
 * run a = 1.7-3.0 /s and b = 20-25 /s with theta = 0.9.  So a is pinned from a
 * figure rather than taken on trust: in SSW2 Fig.1(a) the phrase component
 * turns up at t = 0.78 s and peaks at t = 1.25 s, and since Gp peaks at
 * exactly t = 1/a, that 0.47 s delay gives a = 2.1.  2.0 is used.
 *
 * The one thing in this file that is not exact arithmetic is exp() and log().
 * Those are the platform's, and Python's are too, so a last-bit disagreement
 * between the two libms would show up as a one-unit difference in track 17 on
 * the odd frame.  tests/ja_check.c would catch it and name the frame; see
 * lang/jpn/research/open_questions.md for what was actually measured.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ja.h"

#define ALPHA 2.0               /* phrase mechanism, /s; 1/ALPHA is the peak */
#define BETA  20.0              /* accent mechanism, /s */
#define THETA 0.9               /* accent ceiling */

/* Kawai, Hirose & Fujisaki 1994, section 5.2 for the phrase symbols and 5.3
 * for the six accent ones, "at a speech rate of about 7 morae/s".  P1
 * re-establishes the phrase component at a SENTENCE head, P2 adds one at a
 * CLAUSE head, P3 between and within ICRLBs, and P0 resets it. */
#define P1 0.35
#define P2 0.25
#define P3 0.15
#define P0 (-0.50)

enum { G_FH = 0, G_FM, G_FL, G_AH, G_AM, G_AL };
static const double accent_amp[6] = { 0.50, 0.25, 0.10,     /* heiban */
                                      0.50, 0.35, 0.15 };   /* accented */

#define KA_DELAY  0.070         /* Hirose: /ka/'s command onset, delayed 70 ms */
#define Q_F0_FALL 21.0          /* Hz of extra fall across a geminate closure */
#define GLOTTAL_FRAMES 3
#define DOWNSTEP  1.0

/*
 * Downstep is no longer a fitted multiplier.  It was one -- 0.80, fitted to
 * Kubozono's Table 22 inter-peak ratio of 0.898 -- but that only ever had his
 * accented-to-accented case to answer to, and it got his accented-to-heiban
 * case badly wrong: 0.904 against a measured 0.799.  Kawai's accent sandhi
 * covers BOTH from a rule rather than a fit, because only the FIRST accented
 * word takes the full AH and a later heiban word takes FM rather than FH:
 *
 *                                accented->accented   accented->heiban
 *     Kubozono measured                 0.898               0.799
 *     fitted DOWNSTEP 0.80              0.871               0.904
 *     Kawai sandhi, no downstep         0.838               0.790
 *
 * Mean absolute error halves and the remaining gap is speaker difference.
 * Stacking a multiplier on top of the grading moves BOTH further off, so this
 * stays at 1.0 -- chaining over three or more phrases still happens, through
 * the decay of the phrase component, which is where Fujisaki puts it.
 */

/* Kitazawa: the phrase-initial vowel of a V-V hiatus is glottalized and F0
 * falls with the open quotient.  The neutral pair is the 2006 paper's own
 * example (a 22% dip recovering to 96%); the emphasised pair is 2004's, which
 * that paper describes as focused and emphasised.  The per-vowel scaling is
 * his Table 1 spread, and it is why a|a -- which is ga|a and wa|a, much the
 * commonest hiatus -- comes out subtle, as he says it is. */
static const double GLOTTAL_F0[2]      = { 0.895, 0.980 };
static const double GLOTTAL_F0_EMPH[2] = { 0.706, 0.821 };
static const double GLOTTAL_VOWEL[JA_NV] = { 0.38, 1.26, 1.0, 1.0, 1.36 };

static double Gp(double t)
{
    return t < 0.0 ? 0.0 : ALPHA * ALPHA * t * exp(-ALPHA * t);
}

static double Ga(double t)
{
    double g;

    if (t < 0.0)
        return 0.0;
    g = 1.0 - (1.0 + BETA * t) * exp(-BETA * t);
    return g < THETA ? g : THETA;
}

/*
 * Kawai section 5.2: a phrase command's magnitude is NOT a fixed number.
 * "In the processing that actually generates phrase commands, P1 does not use
 * a fixed value; the magnitude used is whatever value achieves the peak level
 * equivalent to that of a phrase command of Ap = 0.35 occurring in isolation."
 *
 * His reason is the case where a weaker symbol stands at the sentence head and
 * a stronger one follows it: without the rule the second command lands on the
 * first's residue and pushes the phrase component well above the normal
 * sentence-head level.  That is exactly the overshoot this code hit -- a
 * second major phrase peaking ABOVE the first -- and this is his remedy, which
 * is better than thresholding on phrase length because it holds at any
 * spacing.
 */
static double phrase_amp(double nominal, double T0,
                         const double *t0s, const double *amps, int n)
{
    double gain = Gp(1.0 / ALPHA);          /* Gp at its own peak */
    double peak_t = T0 + 1.0 / ALPHA;
    double residue = 0.0, v;
    int i;

    for (i = 0; i < n; i++)
        residue += amps[i] * Gp(peak_t - t0s[i]);
    v = (nominal * gain - residue) / gain;
    return v > 0.0 ? v : 0.0;           /* fmax is C99; msvcrt has no fmax */
}

/*
 * Kawai section 7.1, accent sandhi: which of the six accent symbols each
 * prosodic word takes.  D is an accented word, F a heiban one.
 *
 * Only the first D takes the full amplitude.  His worked example 7 settles it
 * beyond the OCR -- six words, accent types D D D D D D with -Emph on the
 * fourth and sixth, giving AH AM AM AL AM AL -- so:
 *
 *   (3)       the FIRST D takes AH, or AM if it is word 1 and -Emph
 *   (4)       every later D takes AH at +Emph, AM at 0Emph, AL at -Emph
 *   (1a)(2a)  a heiban word 1 takes FM, or FL at -Emph
 *   (5)       a later F takes FM, or FL at -Emph or when what follows is weak
 *
 * +Emph also demotes every following 0Emph to -Emph, which is how Kawai gets a
 * focused word to stand out: the focus is not raised so much as everything
 * after it is lowered.
 *
 * `emph` may be NULL, which is no emphasis anywhere.  Nothing in the text API
 * can express emphasis yet -- that needs the clause parser Kawai's rules
 * (1)-(6) assume and nobody has written -- so NULL is what the shipping path
 * passes.  The rules are here in full anyway: half a rule set is harder to
 * complete later than a whole one.
 */
static void sandhi(const int *acc, int n, const signed char *emph, int *out)
{
    signed char *e;
    int i, seen_d = 0;

    if (n <= 0)
        return;
    e = (signed char *)calloc((size_t)n + 1, 1);
    if (e == NULL)
        return;
    for (i = 0; i < n; i++)
        e[i] = emph ? emph[i] : 0;
    for (i = 0; i < n; i++)             /* +Emph suppresses what follows */
        if (e[i] > 0) {
            int j;

            for (j = i + 1; j < n; j++)
                if (e[j] == 0)
                    e[j] = -1;
        }
    for (i = 0; i < n; i++) {
        if (acc[i] != 0) {
            if (!seen_d) {
                out[i] = (i == 0 && e[i] < 0) ? G_AM : G_AH;
                seen_d = 1;
            } else {
                out[i] = e[i] > 0 ? G_AH : (e[i] < 0 ? G_AL : G_AM);
            }
        } else {
            out[i] = e[i] < 0 ? G_FL : G_FM;
        }
    }
    for (i = 0; i < n - 1; i++)         /* rule (5) lookahead */
        if (out[i] == G_FM && i > 0 &&
            (out[i + 1] == G_AL || out[i + 1] == G_FL))
            out[i] = G_FL;
    free(e);
}

/* ---- the contour -------------------------------------------------------- */

static void contour(int n_frames, double Fb,
                    const double *pt, const double *pa, int n_p,
                    const double *t1, const double *t2, const double *aa,
                    int n_a, double *out)
{
    int k, i, seen_pos = 0, zeroed = 0;

    for (k = 0; k < n_frames; k++) {
        double t = k * 0.010;
        double ph = 0.0, lf;

        for (i = 0; i < n_p; i++)
            ph += pa[i] * Gp(t - pt[i]);
        /*
         * Kawai section 5.2: "after the phrase component reaches zero through
         * the negative phrase command corresponding to P0, the influence of
         * that command and of every preceding phrase command is nullified --
         * it is held at zero."  Without this the final lowering keeps pulling
         * and F0 is dragged below the baseline, which the model does not allow.
         */
        if (ph > 0.0)
            seen_pos = 1;
        else if (seen_pos)
            zeroed = 1;
        if (zeroed)
            ph = 0.0;
        lf = log(Fb) + ph;
        for (i = 0; i < n_a; i++)
            lf += aa[i] * (Ga(t - t1[i]) - Ga(t - t2[i]));
        out[k] = exp(lf);
    }
}

/* ---- accent phrases ----------------------------------------------------- */

/*
 * One group per accent phrase: the major-phrase index it belongs to, and the
 * (start, end) of every mora in it.  Boundary morae carry no time of their own
 * and split the list; a bar also increments the major index, which is what
 * resets both the phrase command and downstep.
 */
typedef struct {
    int    major;
    int    first, last;        /* mora indices, inclusive */
} group;

int ja_pitch(ja_utt *u, const ja_mora *morae, int n_morae, const ja_opts *o,
             double *hz_out)
{
    group *g = NULL;
    double *starts = NULL, *hz = NULL;
    double *pt = NULL, *pa = NULL, *t1 = NULL, *t2 = NULL, *aa = NULL;
    int *acc = NULL, *grades = NULL, *mark = NULL;
    int n_g = 0, n_p = 0, n_a = 0, i, gi, last_major = -1, rc = -1;
    int cur_first = -1, major = 0, n_mark = 1;
    double ds;

    if (u == NULL || u->n <= 0 || n_morae <= 0)
        return -1;
    g = (group *)calloc((size_t)n_morae + 1, sizeof *g);
    starts = (double *)calloc((size_t)n_morae + 1, sizeof *starts);
    hz = (double *)calloc((size_t)u->n, sizeof *hz);
    pt = (double *)calloc((size_t)n_morae + 2, sizeof *pt);
    pa = (double *)calloc((size_t)n_morae + 2, sizeof *pa);
    t1 = (double *)calloc((size_t)n_morae + 2, sizeof *t1);
    t2 = (double *)calloc((size_t)n_morae + 2, sizeof *t2);
    aa = (double *)calloc((size_t)n_morae + 2, sizeof *aa);
    acc = (int *)calloc((size_t)n_morae + 1, sizeof *acc);
    grades = (int *)calloc((size_t)n_morae + 1, sizeof *grades);
    mark = (int *)calloc((size_t)n_morae + 2, sizeof *mark);
    if (g == NULL || starts == NULL || hz == NULL || pt == NULL ||
        pa == NULL || t1 == NULL || t2 == NULL || aa == NULL ||
        acc == NULL || grades == NULL || mark == NULL)
        goto done;

    starts[0] = 0.0;
    for (i = 1; i < n_morae; i++)
        starts[i] = u->ends[i - 1];

    /* which boundary mark opened each major phrase; the first opens nothing */
    mark[0] = 0;
    for (i = 0; i < n_morae; i++)
        if (morae[i].kind == JA_M_BAR || morae[i].kind == JA_M_BARBAR)
            mark[n_mark++] = morae[i].kind;

    for (i = 0; i < n_morae; i++) {
        if (ja_is_boundary(&morae[i])) {
            if (cur_first >= 0) {
                g[n_g].major = major;
                g[n_g].first = cur_first;
                g[n_g].last = i - 1;
                n_g++;
            }
            cur_first = -1;
            if (morae[i].kind != JA_M_SP)
                major++;
            continue;
        }
        if (cur_first < 0)
            cur_first = i;
    }
    if (cur_first >= 0) {
        g[n_g].major = major;
        g[n_g].first = cur_first;
        g[n_g].last = n_morae - 1;
        n_g++;
    }
    if (n_g == 0)
        goto done;

    /*
     * One accent type PER ACCENT PHRASE when the analyser supplied them, and
     * that is the whole point of having a dictionary: the accent type of a
     * Japanese word is lexical, so 箸が, 橋が and 端が are the same three
     * morae with the fall in three different places.
     *
     * Without the analyser -- romaji input, or no dictionary file -- there is
     * one accent type for the whole utterance, applied to its first phrase,
     * and everything after it is heiban.  That is as much as text alone can
     * say, and heiban is the right default to be wrong with: it is the
     * commonest pattern by a wide margin, and a wrong nucleus is heard as a
     * different word where a missing one is heard as a flat reading of the
     * right one.
     *
     * A phrase past the end of the list stays heiban rather than repeating
     * the last one, because a missing accent is a flat phrase and a guessed
     * one is a wrong word.
     */
    if (o->accents != NULL && o->n_accents > 0) {
        for (gi = 0; gi < n_g && gi < o->n_accents; gi++)
            acc[gi] = o->accents[gi];
    } else
        acc[0] = o->accent;
    sandhi(acc, n_g, NULL, grades);

    ds = 1.0;
    for (gi = 0; gi < n_g; gi++) {
        int a = acc[gi];
        double T1v, T2v, Aa;
        int last;

        if (g[gi].major != last_major) {
            /*
             * Kawai: a phrase command is an impulse shortly BEFORE the phrase
             * it lifts, so the response has risen by the time voicing starts.
             * His rules grade the boundary: a sentence boundary takes P1, a
             * clause boundary P2, an ICRLB boundary P3.  So the start of the
             * utterance is P1, '||' is a clause and '|' the weaker ICRLB.
             *
             * Giving every later major phrase P2 made the second one peak
             * ABOVE the first: at ALPHA = 2.0 the phrase response does not
             * peak until 0.5 s after its impulse, so a second command fired
             * half a second in lands on a first that has not begun to decay.
             */
            double nominal = (last_major < 0) ? P1
                           : (mark[g[gi].major] == JA_M_BARBAR ? P2 : P3);
            double T0 = starts[g[gi].first] - 0.25;
            double amp = phrase_amp(nominal, T0, pt, pa, n_p);

            if (amp > 1e-6) {
                pt[n_p] = T0;
                pa[n_p] = amp;
                n_p++;
                ds = 1.0;
            }
        }
        last_major = g[gi].major;
        /* Kawai 7.2: the accent command rises before mora 1 if atamadaka and
         * after it otherwise; it falls at the nucleus, or after the final mora
         * when the word is heiban. */
        T1v = (a == 1) ? starts[g[gi].first] : u->ends[g[gi].first];
        if (a == 0)
            T2v = u->ends[g[gi].last];
        else {
            int n_ms = g[gi].last - g[gi].first + 1;
            int at = (a < n_ms ? a : n_ms) - 1;

            T2v = u->ends[g[gi].first + at];
        }
        /*
         * Hirose, Sakata, Osame & Fujisaki section 3: "in the case of
         * declarative sentences, the accent command for the predicate phrase
         * usually takes a reduced amplitude toward the end of an utterance."
         * Kawai's symbol set already grades for this -- FM/AM against FH/AH --
         * so the last phrase of a multi-phrase declarative takes the M grade.
         *
         * This, not an unbounded P0, is what brings a sentence down at the
         * end: Kawai clamps the phrase component at zero, so the final fall
         * has to come from the accent command, which is where they say it
         * comes from.  Hirose's reduction is "in the case of declarative
         * sentences", so a question does not get it -- it rises instead.
         */
        last = (!o->question && gi == n_g - 1 && n_g > 1);
        if (n_g > 1) {
            int gr = grades[gi];

            if (last && (gr == G_AH || gr == G_FH))
                gr = (gr == G_AH) ? G_AM : G_FM;
            Aa = accent_amp[gr];
        } else {
            Aa = accent_amp[(a == 0 ? G_FH : G_AH)];
        }
        t1[n_a] = T1v;
        t2[n_a] = T2v;
        aa[n_a] = Aa * ds;
        n_a++;
        /*
         * Kubozono 1987 ch. 5: downstep is ACCENT-induced -- a word is lowered
         * when it follows an ACCENTED word, not merely another word -- it
         * chains within the major phrase, and it is a shift of pitch RANGE, so
         * a downstepped phrase can still be phonetically higher than the one
         * that lowered it.
         */
        if (a != 0)
            ds *= DOWNSTEP;
    }
    /*
     * Hirose et al. section 4: "the command for the interrogative particle
     * /ka/ is also assigned a large value (i.e., DH = 0.6), and its onset
     * timing is delayed by 70 msec as compared with the onset timing of the
     * command for the lexical accent."  0.6 is their DIALOGUE value -- they
     * say the dialogue rules raise every command, FH from 0.50 to 0.6 -- so a
     * reader takes the reading-style equivalent, Kawai's AH.  It is an EXTRA
     * command on the final mora, on top of the phrase's own lexical accent,
     * which is why a question rises where a statement falls.
     */
    if (o->question) {
        int li = g[n_g - 1].last;

        t1[n_a] = starts[li] + KA_DELAY;
        t2[n_a] = u->ends[li];
        aa[n_a] = accent_amp[G_AH];
        n_a++;
    }
    /* Kawai rule (1): a sentence boundary takes P1, but at the END of the text
     * only P0 -- which is NEGATIVE, -0.50, and is the sentence-final lowering.
     * Nothing was emitting it, so every utterance ended as high as its last
     * accent left it. */
    pt[n_p] = u->ends[g[n_g - 1].last] - 0.25;
    pa[n_p] = P0;
    n_p++;

    contour(u->n, o->fb, pt, pa, n_p, t1, t2, aa, n_a, hz);

    /* Idemaru & Guion: F0 falls about 30 Hz further across a geminate than a
     * singleton, and this was their strongest secondary cue (~77% on its
     * own).  Applied as a step down at the closure that recovers over ~0.3 s. */
    for (i = 0; i < u->n_q; i++) {
        int k0 = (int)(u->q_ends[i] * 100.0);
        int k;

        if (k0 < 0)
            k0 = 0;
        for (k = k0; k < u->n; k++) {
            double d = (k - k0) / 25.0;

            if (d > 1.0)
                break;
            hz[k] -= Q_F0_FALL * (1.0 - d);
        }
    }
    /* Kitazawa: the phrase-initial vowel of a V-V hiatus is glottalized, and
     * F0 falls with the open quotient.  Where the two vowels are the same
     * there is no formant movement at all, so this and the duration asymmetry
     * are the only things marking the boundary. */
    for (i = 0; i < n_morae; i++) {
        int k0, j, v2;
        double base, lo, back, sc;

        if (morae[i].kind != JA_M_SP || i == 0 || i + 1 >= n_morae)
            continue;
        if (morae[i - 1].kind != JA_M_CV || morae[i - 1].v < 0)
            continue;
        if (morae[i + 1].kind != JA_M_CV || morae[i + 1].c != JA_C_NONE)
            continue;
        v2 = morae[i + 1].v;
        if (v2 < 0)
            continue;
        k0 = (int)ja_round(u->ends[i] * 100.0);
        if (k0 <= 0 || k0 > u->n)
            continue;
        base = hz[k0 - 1];
        /*
         * Prominent where the following phrase is emphasised, which is what he
         * observed, and scaled by vowel, which is why a|a is subtle.
         *
         * Nothing in the text API can mark a phrase as emphasised yet, so the
         * emphasised pair is never selected.  It stays because the measurement
         * is real and because deleting it would mean going back to the paper
         * to put it in again.
         */
        {
            int emphasised = 0;
            const double *pair = emphasised ? GLOTTAL_F0_EMPH : GLOTTAL_F0;

            lo = pair[0];
            back = pair[1];
        }
        sc = GLOTTAL_VOWEL[v2];
        lo = 1.0 - (1.0 - lo) * sc;
        back = 1.0 - (1.0 - back) * sc;
        for (j = 0; j < GLOTTAL_FRAMES; j++) {
            int k = k0 + j;
            double f = j / (double)(GLOTTAL_FRAMES - 1 > 1
                                    ? GLOTTAL_FRAMES - 1 : 1);

            if (k >= u->n)
                break;
            hz[k] = base * (lo + (back - lo) * f);
        }
    }
    for (i = 0; i < u->n; i++) {
        double f = hz[i] > 40.0 ? hz[i] : 40.0;
        int t = (int)(f / 2.0 + 0.5);

        u->frames[(size_t)i * JA_NTRACK + 17] =
            (uint8_t)(t < 1 ? 1 : (t > 255 ? 255 : t));
    }
    if (hz_out != NULL)
        memcpy(hz_out, hz, (size_t)u->n * sizeof *hz);
    rc = 0;

done:
    free(g); free(starts); free(hz);
    free(pt); free(pa); free(t1); free(t2); free(aa);
    free(acc); free(grades); free(mark);
    return rc;
}
