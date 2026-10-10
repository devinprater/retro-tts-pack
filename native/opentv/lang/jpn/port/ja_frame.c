/*
 * Morae -> the engine's 22-track parameter frames.
 *
 * A translation of build/Japanese_test/jp_speak.py, and a deliberately literal
 * one.  The Python's structure is kept even where C would suggest otherwise,
 * because the test is that the two produce the same bytes and a tidier
 * arrangement is a chance for them not to.  Where the Python has a comment
 * explaining a number or a decision, the comment is here too: the reasoning is
 * the expensive part of this file and leaving it on the other side of a
 * language boundary would make this code look arbitrary.
 *
 * THE ARITHMETIC IS COPIED, NOT RE-DERIVED.  Python's round() is round-half-
 * to-even and C's round() is half-away-from-zero, so ja_round() below is
 * CPython's own algorithm; int() truncates toward zero, which a C cast does
 * too; and everything that is a float in the Python is a double here.  Those
 * three are not pedantry -- each of them changes frames.
 *
 * ONE DIFFERENCE, ON PURPOSE.  The Python carries two dozen feature flags
 * (PHI, FINAL_N, BRIDGE_F3, SMOOTH_VC, GLOTTAL, ...) so that each change can
 * be listened to against its predecessor.  Every one of them is on, and this
 * side bakes them in: the flags exist for A/B listening, which happens in the
 * Python, and the C is the shipping path.  Turning one off here would make the
 * oracle fail, which is the right behaviour for a switch nobody should touch
 * without re-doing the listening.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ja.h"
#include "ja_devoice_tab.h"

/* ---- timing and the measured constants ---------------------------------- */

#define MORA_FRAMES  14         /* ~7 morae/s, the rate the rules assume */
#define V_DEFAULT     9         /* frames for the mean vowel, 68.8 ms */
#define VOWEL_TAIL_MS 35.0
#define MIN_VOWEL     2         /* frames of steady vowel never spent */
#define VOWEL_FLOOR   5
#define TRANS         3         /* Morikawa: the vocalic transition is ~26 ms */
#define OFFGLIDE      3         /* frames of glide from a vowel INTO one */
#define SLEW        200.0       /* Hz of F2 a formant will move in one frame */
#define TRANS_MAX     8
#define FRIC_GROWN    4
/* A geminate before a fricative is noise, not silence: see the geminate
 * branch in ja_build.  The Python calls this FRIC_GEMINATE too. */
#define FRIC_GEMINATE 1
/*
 * Frames of amplitude ramp at the very end of an utterance.  The engine's own
 * output ends at zero -- English and Spanish both measure 0 for the last
 * sample -- because their frames decay; these stopped dead, so the last sample
 * sat wherever the waveform was, up to 40% of the loudest one in the word, and
 * the step to silence was heard as a click.  Worst on the single letters and
 * cursor announcements a screen reader says constantly.  Three frames is
 * 30 ms, the same fade jp_speak.render has always applied to its WAVs.
 */
#define FINAL_FADE      6
/*
 * Decibels of fall per frame.  Tracks 0, 1 and 2 are about a decibel a unit,
 * so this is SUBTRACTED from them: a linear decibel slope.  Scaling them
 * towards zero instead is -20 dB a frame, four times Spanish's slope, and the
 * ear called that a cut even though it reached silence exactly.  Measured as
 * an envelope in 10 ms cells, dB below the word's loudest:
 *
 *     es hola      -7  -11  -20  -29  -30  -30  -29  -inf
 *     ja before    -7  -20  -28  -43  -inf
 *     ja after     -4   -9  -14  -19  -22  -26  -30  -34  -inf
 */
#define FINAL_FADE_STEP 5
#define FINAL_FADE_MIN  6
/*
 * And silent frames after it, because the ramp alone changed nothing: the
 * engine renders the frames it is given and stops, so the filter is still
 * ringing when the samples run out.  Five is where the last sample reaches
 * exactly zero for every word measured; with the ramp but no padding it sat
 * at up to 58% of the loudest sample in the word, and that step is the click.
 */
#define FINAL_PAD       6
#define NASAL_BW     60

/* Idemaru & Guion 2008: a geminate is not just a longer closure.  The
 * preceding vowel lengthens 27%, the following shortens 17%, the F0 fall is
 * about 30 Hz greater, and V1 becomes louder than V2.  Ranked by how well each
 * classifies the contrast alone: F0 ~77%, V1 duration, intensity ~66%, V2
 * duration -- and voice quality at chance, so there is no voice-quality term. */
#define Q_V1_LONGER   1.27
#define Q_V2_SHORTER  0.83
#define Q_INTENSITY   2         /* dB, V1 above V2 */

/* Kitazawa & Kiriyama: a phrase-initial vowel after a vowel is glottalized,
 * and where the two vowels are the SAME there is no formant movement left to
 * mark the boundary at all.  The depth is anchored on the two published
 * examples whose vowel is identified; the per-vowel scaling is why a|a -- much
 * the commonest hiatus -- comes out subtle, which is what he reports. */
#define GLOTTAL_FRAMES 3        /* ~7 periods at their speaker's 250 Hz */
#define GLOTTAL_DIP    8        /* dB; creak is quieter as well as lower */
#define HIATUS_RATIO      1.7
#define HIATUS_RATIO_EMPH 0.76

/* ---- Python's arithmetic ------------------------------------------------- */

/* ja_round() -- CPython's rounding, not C's -- lives in ja_tables.c, because
 * ja_pitch.c needs it too and two copies is one too many.
 *
 * The Python's _rnd and _cl. */
static int rnd(double x)
{
    return (int)(x + 0.5);      /* int() truncates toward zero, as a cast does */
}

static int cl(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ---- a growable frame array --------------------------------------------- */

typedef struct {
    uint8_t *f;
    int      n, cap;
    int      oom;
} fvec;

static void fv_init(fvec *v)
{
    memset(v, 0, sizeof *v);
}

static void fv_free(fvec *v)
{
    free(v->f);
    memset(v, 0, sizeof *v);
}

static uint8_t *fv_grow(fvec *v, int extra)
{
    if (v->n + extra > v->cap) {
        int cap = v->cap ? v->cap * 2 : 256;
        uint8_t *p;

        while (cap < v->n + extra)
            cap *= 2;
        p = (uint8_t *)realloc(v->f, (size_t)cap * JA_NTRACK);
        if (p == NULL) {
            v->oom = 1;
            return NULL;
        }
        v->f = p;
        v->cap = cap;
    }
    return v->f + (size_t)v->n * JA_NTRACK;
}

static uint8_t *fv_at(fvec *v, int i)
{
    return v->f + (size_t)i * JA_NTRACK;
}

/* ---- the build context -------------------------------------------------- */

typedef struct {
    int    sr;
    int    engine_voice, c, v, release_headroom;
    int    trim_for, aspir_for;
    double rate_scale;
} bctx;

static int mora_frames(const bctx *B)
{
    return (int)ja_round(MORA_FRAMES * B->rate_scale);
}

static int v_default(const bctx *B)
{
    return (int)ja_round(V_DEFAULT * B->rate_scale);
}

/*
 * The whole voiced vowel, in frames, from its duration target in ms.
 *
 * Quantisation is real and worth knowing: at 10 ms a frame the five vowels'
 * 11 ms spread is barely one frame wide, so this can express two levels, not
 * five.  The data happens to fall into two groups -- the high vowels /i/ and
 * /u/ at 63 ms, the other three at 70-74 -- so the distinction that survives
 * the quantiser is the one that is actually there.
 *
 * The target is the whole voiced vowel and not the steady part.  Setting the
 * steady part alone inverts the result, because the transition into the vowel
 * is voiced and at full amplitude so the measurement counts it: after /b/ that
 * is 5 frames for /i/ and 3 for /u/, since /i/'s F2 has 843 Hz to cover and
 * /u/'s has 135.  Give both the same steady count and /i/ comes out longer
 * than /u/, which is backwards -- they are the two SHORT vowels in the data.
 *
 * VOWEL_TAIL_MS is the amount the measurement loses at the end, where the
 * taper into the following consonant decays below the threshold standing in
 * for Yazawa & Kondo's hand-placed boundary.  It is calibrated against the
 * very detector under investigation, so it is provisional compensation rather
 * than an independently established acoustic tail; if the detector is later
 * found to under-read, this moves with it.
 */
static int vowel_frames(const bctx *B, int v, int long_v)
{
    double t;

    if (v < 0 || v >= JA_NV)
        v = JA_V_A;
    t = ja_V_DUR[v][long_v ? 1 : 0] * B->rate_scale;
    return imax(MIN_VOWEL, (int)ja_round((t + VOWEL_TAIL_MS) / 10.0));
}

/* Frames the length mark adds: the difference between the two targets, which
 * is what the second mora is worth acoustically. */
static int long_extra(const bctx *B, int v)
{
    return imax(1, vowel_frames(B, v, 1) - vowel_frames(B, v, 0));
}

/* ---- one frame ----------------------------------------------------------- */

/*
 * F1 = v*4, F2 = v*8 + 500, F3/F4 = v*16, B1-B3 = v*2.  Track 0 is amplitude,
 * 17 is pitch (written later by ja_pitch), and 16/18/19 are the fixed source
 * settings this voice uses throughout.
 */
static void mkframe(const bctx *B, const ja_post sp, const ja_src *src,
                    uint8_t *t)
{
    int k;

    memset(t, 0, JA_NTRACK);
    t[0] = 60; t[16] = 14; t[17] = 50; t[18] = 16; t[19] = 8;
    t[9]  = (uint8_t)cl(rnd(sp[0] / 4.0), 1, 255);
    t[10] = (uint8_t)cl(rnd((sp[1] - 500.0) / 8.0), 0, 255);
    t[11] = (uint8_t)cl(rnd(sp[2] / 16.0), 1, 255);
    t[12] = (uint8_t)cl(rnd(sp[3] / 16.0), 1, 255);
    t[13] = (uint8_t)cl(rnd(sp[4] / 2.0), 1, 204);
    t[14] = (uint8_t)cl(rnd(sp[5] / 2.0), 1, 204);
    t[15] = (uint8_t)cl(rnd(sp[6] / 2.0), 1, 204);
    if (src != NULL)
        for (k = 0; k < JA_NTRACK; k++)
            if (src->mask & (1u << k))
                t[k] = (uint8_t)cl(src->val[k], 0, 255);
    /*
     * Rate compensation for the noise branches.  Our settings were fitted at
     * 16 kHz and come out louder at 11025, by an amount that grows with the
     * resonance track 6 actually drives -- F5, which the engine derives and
     * which no input track sets.  See ja_tables.c for why the offsets are
     * searched for rather than computed: a track unit is not a decibel, and
     * track 6 saturates above about 66.
     *
     * Track 8 is NOT trimmed; it bypasses the resonators and measures
     * +0.05 dB across the two rates.
     */
    if (B->sr != 16000) {
        if (B->trim_for && (t[4] || t[5] || t[6]))
            for (k = 4; k <= 6; k++)
                if (t[k] > 0)
                    t[k] = (uint8_t)imax(0, (int)t[k] - B->trim_for);
        if (B->aspir_for && t[2] > 0)
            t[2] = (uint8_t)imax(0, (int)t[2] - B->aspir_for);
    }
    ja_voice_noise(t, B->engine_voice, B->sr, B->c, B->v);
    if (B->release_headroom && t[2])
        t[2] = (uint8_t)imax(0, (int)t[2] - B->release_headroom);
}

static int push(const bctx *B, fvec *v, const ja_post sp, const ja_src *src)
{
    uint8_t *t = fv_grow(v, 1);

    if (t == NULL)
        return -1;
    mkframe(B, sp, src, t);
    v->n++;
    return 0;
}

static int push_n(const bctx *B, fvec *v, const ja_post sp, const ja_src *src,
                  int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (push(B, v, sp, src) != 0)
            return -1;
    return 0;
}

/* ---- posture helpers ---------------------------------------------------- */

static void pcopy(ja_post d, const ja_post s)
{
    memcpy(d, s, sizeof(ja_post));
}

static void lerp(ja_post d, const ja_post a, const ja_post b, double f)
{
    int k;

    for (k = 0; k < 7; k++)
        d[k] = a[k] + (b[k] - a[k]) * f;
}

/*
 * How many frames a move of this size needs.
 *
 * A fixed count is wrong at both ends: /a/ to /k/ moves F2 by 272 Hz and wants
 * three frames, /i/ to /w/ moves it by 1367 and crammed into four that is
 * 340 Hz a frame, which is heard as a step.  Articulators have a speed.
 *
 * F3 counts too, below F2: a sibilant carries its own front-cavity resonance,
 * so /sh/ at 3000 into a vowel at 2275 is a real move even when F2 barely
 * shifts.  And the division rounds UP, not to nearest -- rounded to nearest
 * this was an average rather than a limit, and every consonant in the
 * inventory duly sat at 208-248 Hz a frame.
 */
/*
 * Rule 1's /masu/ and /desu/, as far as a mora string can tell.  The analyser
 * also requires the SU to end a verb, auxiliary or interjection; nothing here
 * has a part of speech, so this takes the two preceding morae and accepts the
 * false positives that follow from that.
 *
 * "unless a question mark or a long vowel follows, where the question has to
 * rise on it".  A long vowel is its own mora, so one following already means
 * this is not phrase-final and the caller never asks; the question mark is not
 * a mora at all and arrives in ja_opts.
 */
static int polite_su(const ja_mora *m, int n_morae, int n, int question)
{
    if (m[n].kind != JA_M_CV || m[n].c != JA_C_S || m[n].v != JA_V_U)
        return 0;
    if (n == 0 || m[n - 1].kind != JA_M_CV)
        return 0;
    if (!((m[n - 1].c == JA_C_M && m[n - 1].v == JA_V_A) ||
          (m[n - 1].c == JA_C_D && m[n - 1].v == JA_V_E)))
        return 0;
    if (question && n + 1 >= n_morae)
        return 0;
    return 1;
}

static int glide_len(const ja_post a, const ja_post b, int base)
{
    double d = fabs(a[1] - b[1]);
    double d1 = fabs(a[0] - b[0]) * 2;
    double d2 = fabs(a[2] - b[2]) * 0.7;

    if (d1 > d) d = d1;
    if (d2 > d) d = d2;
    return imax(base, imin(TRANS_MAX, (int)ceil(d / SLEW)));
}

/*
 * Yamakawa & Amano 2015: a fricative's intensity envelope is a trapezoid -- a
 * rise, a steady part, then a decay -- fitted by three straight lines.  Their
 * Fig. 2 puts the rise at about 40% of the frication for both manners (/s/ 55
 * of 145 ms, /ts/ 32 of 74), so the shape scales and only the total length
 * distinguishes a fricative from an affricate.  A flat switch on and off,
 * which is what this did first, is the one shape the measurement rules out.
 */
/*
 * A geminate splits one trapezoid across two morae, so each end can be asked
 * for separately: the geminate raises the noise and holds it, the mora after
 * it holds and lets it go.  A fraction of 0 drops that end.
 */
static void fric_env_ex(int n, int peak, double rise_frac, double fall_frac,
                        int *out)
{
    int nr = rise_frac > 0 ? imax(1, (int)(n * rise_frac + 0.5)) : 0;
    int nd = fall_frac > 0 ? imax(1, (int)(n * fall_frac + 0.5)) : 0;
    int i;
    double depth = 18.0;        /* dB; track 8 is roughly a decibel a unit */

    for (i = 0; i < n; i++) {
        double g;

        if (nr && i < nr)
            g = peak - depth * (1.0 - (i + 1) / (double)nr);
        else if (nd && i >= n - nd)
            g = peak - depth * 0.45 * ((i - (n - nd) + 1) / (double)nd);
        else
            g = peak;
        out[i] = (int)(g + 0.5);
    }
}

static void fric_env(int n, int peak, int *out)
{
    fric_env_ex(n, peak, 0.40, 0.22, out);
}

/*
 * The source and track a fricative onset raises.  One place, because a
 * geminate before a fricative has to raise exactly the noise the fricative
 * itself will: the two are one continuous sound and any disagreement between
 * them is a step in the middle of it.
 */
static void fric_noise(int c, int voiced, ja_src *src, int *key)
{
    if (c == JA_C_S)
        *src = ja_SRC_SIBIL_S;
    else if (c == JA_C_SH)
        *src = ja_SRC_SIBIL;
    else if (c == JA_C_H || c == JA_C_HY)
        *src = ja_SRC_ASPIR;
    else if (voiced)
        *src = ja_SRC_SIBIL_Z;      /* /z/: noise on track 8, not on F3 */
    else {
        *src = ja_SRC_SIBIL;
        ja_src_set(src, 0, 40);
    }
    *key = (c == JA_C_H || c == JA_C_HY) ? 2 : 8;
}

/*
 * Bring the vowel's level down into a closure instead of cutting it off.
 *
 * Kashino 1992 section 4.1: the amplitude envelope either side of the closure
 * cues the consonant's manner and voicing.  A vowel running at full level
 * until the instant of closure is the one envelope that cannot happen -- the
 * mouth is already closing through the end of it.  A voiced stop keeps some
 * voicing into the closure, so it tapers less far.
 */
static void taper(fvec *v, int n, int floor_, int voiced_through)
{
    int i, drop = voiced_through ? 6 : 14;      /* dB; track 0 is ~1 dB a unit */

    n = imin(n, imax(0, v->n - imax(1, floor_)));
    if (n <= 0)
        return;
    for (i = 0; i < n; i++) {
        uint8_t *t = fv_at(v, v->n - n + i);

        if (t[0] == 0)
            continue;
        t[0] = (uint8_t)imax(16, (int)(t[0] - drop * (i + 1) / (double)n + 0.5));
    }
}

/*
 * Arai's AVd and AVg: a short V in voicing amplitude across the flap's
 * contact.  Without it the identical formant movement is heard as an
 * approximant -- a legitimate Japanese /r/, but not the flap, which is the
 * typical intervocalic phone.  "The intensity dip induces a sensation of the
 * flap sound."  The dip straddles the apex, so it is applied after the
 * transition out of the flap has been written rather than inside it.
 */
static void dip(fvec *v, int center, int n, int depth)
{
    int half, i;

    if (n <= 0 || depth <= 0)
        return;
    half = n / 2;
    for (i = -half; i < n - half; i++) {
        int idx = center + i;
        uint8_t *t;
        double f;

        if (idx < 0 || idx >= v->n)
            continue;
        t = fv_at(v, idx);
        if (t[0] == 0)
            continue;
        f = 1.0 - abs(i) / (double)(half + 1);
        t[0] = (uint8_t)imax(16, (int)(t[0] - depth * f + 0.5));
    }
}

/*
 * Bend the tail of the vowel already written toward the consonant coming.
 *
 * Without this every V-C boundary is a step -- on `sakura`, F2 moved 616 Hz in
 * a single frame going into the flap.  It is not only a smoothness matter: the
 * formant transition in the PRECEDING vowel is the main cue to where a
 * consonant is articulated, so leaving it out makes place depend on the burst
 * alone.
 *
 * The lerp lands ON the target, not one step short of it, and it never reaches
 * back past `floor_`: the frames before that are the previous mora's own
 * glide, and bending those reverses a move already in flight -- measured on
 * `shashin`, that turned a 171 Hz-a-frame descent into a 504 Hz step.
 */
static int offglide(const bctx *B, fvec *v, const ja_post prev_sp,
                    const ja_post osp, int n, int floor_)
{
    int avail = imax(0, v->n - imax(1, floor_));
    int use = imin(n, avail);
    int extra = n - use, i, base, have_keep = 0;
    ja_src keep;

    if (use <= 0 && extra <= 0)
        return 0;
    base = v->n - use;
    ja_src_clear(&keep);
    for (i = 0; i < use; i++) {
        int idx = base + i;
        uint8_t *t = fv_at(v, idx);
        ja_post q;
        int k;

        if (t[0] == 0)
            continue;           /* already silent; nothing to bend */
        ja_src_clear(&keep);
        for (k = 0; k <= 6; k++)
            ja_src_set(&keep, k, t[k]);
        ja_src_set(&keep, 8, t[8]);
        have_keep = 1;
        lerp(q, prev_sp, osp, (i + 1) / (double)n);
        mkframe(B, q, &keep, t);
    }
    /* If the vowel was too short to hold the whole move, the move takes longer
     * rather than going faster: /i/ to /w/ travels 1367 Hz of F2, and squeezing
     * that into whatever frames happened to be free is what left a 304 Hz
     * step. */
    for (i = use; i < n; i++) {
        ja_post q;

        if (!have_keep)
            break;
        lerp(q, prev_sp, osp, (i + 1) / (double)n);
        if (push(B, v, q, &keep) != 0)
            return -1;
    }
    return 0;
}

/* ---- the mora span bookkeeping ------------------------------------------ */

typedef struct {
    int start, steady, len, head, nominal, give;
    int has_steady, adjustable, budget_break;
    uint8_t steady_frame[JA_NTRACK];
} span;

static int span_len(const span *sp, int n_sp, int total, int i)
{
    return ((i + 1 < n_sp) ? sp[i + 1].start : total) - sp[i].start;
}

/* ---- noise-branch trim scoping ------------------------------------------ */

static void trim_set(bctx *B, int parallel_c, int aspir_c, int aspir_v)
{
    B->trim_for = (parallel_c > 0) ? ja_NOISE_TRIM_UNITS[parallel_c] : 0;
    if (aspir_v >= 0)
        B->aspir_for = ja_ASPIR_TRIM_V[aspir_v];
    else
        B->aspir_for = (aspir_c > 0) ? ja_ASPIR_TRIM_C[aspir_c] : 0;
}

static void trim_clear(bctx *B)
{
    B->trim_for = B->aspir_for = 0;
}

/* ---- voiceless obstruents ----------------------------------------------- */

static int voiceless(int c)
{
    switch (c) {
    case JA_C_K: case JA_C_KY: case JA_C_T: case JA_C_P: case JA_C_PY:
    case JA_C_S: case JA_C_SH: case JA_C_H: case JA_C_HY: case JA_C_F:
    case JA_C_CH: case JA_C_TS:
        return 1;
    default:
        return 0;
    }
}

/* ---- Homma's word-level compensation ------------------------------------ */

/*
 * Homma 1981 reports compensation within a word: /papa/ at 260 ms against
 * /gaga/ at 267, despite a 37-ms difference in their first syllables. This
 * motivates sharing a nominal budget across morae, but does not establish
 * exact isochrony. Redistribution here remains a modelling approximation.
 *
 * Lexical-word boundaries are not available here. At least keep the budget
 * within a continuous stretch between explicit pauses, so a later phrase
 * cannot change an earlier phrase's segment allocations.
 *
 * Only unchanged steady vowel is adjustable. Protect offglides and tapers
 * already written into its tail, glottal heads, and the explicit long-vowel
 * increment (like geminate closure). An infeasible budget may overrun.
 */
static int compensate(fvec *out, span *sp, int n_sp,
                      const int *q_sp, const int *q_rel, int n_q,
                      double *ends, double *q_out)
{
    int delta, i, idx, n_adj = 0, tot, err, j, first, last;
    int *adj, *cap, *give, *pos;
    fvec nv;

    for (i = 0; i < n_sp; i++)
        if (sp[i].adjustable)
            n_adj++;
    if (n_adj == 0) {
        for (i = 0; i < n_sp; i++)
            ends[i] = (sp[i].start + span_len(sp, n_sp, out->n, i)) / 100.0;
        for (i = 0; i < n_q; i++)
            q_out[i] = (sp[q_sp[i]].start + q_rel[i]) / 100.0;
        return 0;
    }
    adj = (int *)malloc((size_t)n_adj * sizeof *adj);
    cap = (int *)malloc((size_t)n_adj * sizeof *cap);
    give = (int *)malloc((size_t)n_adj * sizeof *give);
    pos = (int *)malloc((size_t)n_sp * sizeof *pos);
    if (adj == NULL || cap == NULL || give == NULL || pos == NULL) {
        free(adj); free(cap); free(give); free(pos);
        return -1;
    }
    first = 0;
    for (last = 0; last <= n_sp; last++) {
        int end, target = 0, want;

        if (last < n_sp && !sp[last].budget_break)
            continue;
        if (first == last) {
            first = last + 1;
            continue;
        }
        end = last < n_sp ? sp[last].start : out->n;
        n_adj = 0;
        for (i = first; i < last; i++) {
            target += sp[i].nominal;
            if (sp[i].adjustable)
                adj[n_adj++] = i;
        }
        delta = target - (end - sp[first].start);
        tot = 0;
        for (i = 0; i < n_adj; i++) {
            const span *s = &sp[adj[i]];
            int at = s->steady + s->head, stable = 0, k;

            for (k = at; k < s->steady + s->len; k++) {
                if (memcmp(fv_at(out, k), s->steady_frame, JA_NTRACK) != 0)
                    break;
                stable++;
            }
            cap[i] = imax(0, stable - (delta < 0 ? MIN_VOWEL : 0));
            tot += cap[i];
        }
        if (tot > 0 && delta) {
            want = imax(delta, -tot);
            err = want;
            for (i = 0; i < n_adj; i++) {
                give[i] = (int)ja_round(want * (cap[i] / (double)tot));
                err -= give[i];
            }
            /* Preserve the exact feasible budget despite frame rounding. */
            while (err) {
                int step = err > 0 ? 1 : -1;
                j = -1;
                for (i = 0; i < n_adj; i++) {
                    if (!cap[i] ||
                        (err < 0 && give[i] <= (want < 0 ? -cap[i] : 0)) ||
                        (err > 0 && want < 0 && give[i] >= 0))
                        continue;
                    if (j < 0 || cap[i] > cap[j]) j = i;
                }
                give[j] += step;
                err -= step;
            }
            for (i = 0; i < n_adj; i++)
                sp[adj[i]].give = give[i];
        }
        first = last + 1;
    }

    fv_init(&nv);
    for (idx = 0; idx < n_sp; idx++) {
        int len = span_len(sp, n_sp, out->n, idx);
        int g = sp[idx].give, at = 0, k, need;
        uint8_t *seg = out->f + (size_t)sp[idx].start * JA_NTRACK;

        pos[idx] = nv.n;
        if (g && sp[idx].has_steady) {
            at = sp[idx].steady - sp[idx].start + sp[idx].head;
            need = len + g;
        } else {
            g = 0;
            need = len;
        }
        /* A span with nothing in it is ordinary: an accent-phrase boundary is
         * a pitch event and emits no frames at all.  Asking for room for zero
         * frames must not be mistaken for running out of it. */
        if (need > 0 && fv_grow(&nv, need) == NULL)
            break;
        if (at > 0) {
            memcpy(fv_at(&nv, nv.n), seg, (size_t)at * JA_NTRACK);
            nv.n += at;
        }
        for (k = 0; k < g; k++) {
            memcpy(fv_at(&nv, nv.n), seg + (size_t)at * JA_NTRACK, JA_NTRACK);
            nv.n++;
        }
        {
            int from = at + (g < 0 ? -g : 0);
            int rest = len - from;

            if (rest > 0) {
                memcpy(fv_at(&nv, nv.n), seg + (size_t)from * JA_NTRACK,
                       (size_t)rest * JA_NTRACK);
                nv.n += rest;
            }
        }
        ends[idx] = nv.n / 100.0;
    }
    if (nv.oom) {
        fv_free(&nv);
        free(adj); free(cap); free(give); free(pos);
        return -1;
    }
    /*
     * Write the new positions BACK into the spans.  This did not happen in the
     * Python for a long time, so every caller reading a span's start after
     * compensation got the PRE-compensation index -- 1 to 2 frames early,
     * which is 10-20 ms, and which quietly invalidated the vowel-tail
     * calibration measured through it.  Found by Astra while auditing the
     * region bookkeeping.
     */
    for (idx = 0; idx < n_sp; idx++) {
        if (sp[idx].has_steady)
            sp[idx].steady = pos[idx] + (sp[idx].steady - sp[idx].start);
        sp[idx].start = pos[idx];
    }
    for (i = 0; i < n_q; i++) {
        span *s = &sp[q_sp[i]];
        int off = q_rel[i];

        if (q_rel[i] >= s->steady - s->start + s->head)
            off += s->give;
        q_out[i] = (pos[q_sp[i]] + off) / 100.0;
    }
    free(out->f);
    out->f = nv.f;
    out->n = nv.n;
    out->cap = nv.cap;
    free(adj); free(cap); free(give); free(pos);
    return 0;
}

/* ---- build --------------------------------------------------------------- */

void ja_opts_default(ja_opts *o)
{
    o->sr = JA_SR_DEFAULT;
    o->engine_voice = 0;
    o->rate_scale = 1.0;
    o->fb = 72.0;
    o->question = 0;
    o->accent = 0;
    o->accents = NULL;
    o->n_accents = 0;
    o->devoiced = NULL;
    o->n_devoiced = 0;
}

void ja_utt_free(ja_utt *u)
{
    free(u->frames);
    free(u->ends);
    free(u->q_ends);
    memset(u, 0, sizeof *u);
}

int ja_build(const ja_mora *morae, int n_morae, const ja_opts *o, ja_utt *u)
{
    bctx B;
    fvec out;
    span *sp = NULL;
    double *ends = NULL, *q_out = NULL, *same_v1 = NULL, *same_v2 = NULL;
    int *q_sp = NULL, *q_rel = NULL;
    unsigned char *before_q = NULL, *after_q = NULL, *glottal = NULL,
                  *nasal_v1 = NULL;
    int n_q = 0, n, i, steady_at = 0, prev_v = JA_V_NONE, fail = 0;
    int q_fric = 0, from_q = 0;   /* a geminate raised the next noise already */
    ja_post prev_sp;

    memset(u, 0, sizeof *u);
    if (morae == NULL || n_morae <= 0)
        return -1;
    B.sr = o->sr;
    B.engine_voice = o->engine_voice;
    B.c = JA_C_NONE;
    B.v = JA_V_NONE;
    B.release_headroom = 0;
    B.rate_scale = o->rate_scale;
    B.trim_for = B.aspir_for = 0;
    fv_init(&out);

    sp = (span *)calloc((size_t)n_morae, sizeof *sp);
    ends = (double *)calloc((size_t)n_morae, sizeof *ends);
    q_out = (double *)calloc((size_t)n_morae + 1, sizeof *q_out);
    q_sp = (int *)calloc((size_t)n_morae + 1, sizeof *q_sp);
    q_rel = (int *)calloc((size_t)n_morae + 1, sizeof *q_rel);
    same_v1 = (double *)calloc((size_t)n_morae, sizeof *same_v1);
    same_v2 = (double *)calloc((size_t)n_morae, sizeof *same_v2);
    before_q = (unsigned char *)calloc((size_t)n_morae, 1);
    after_q = (unsigned char *)calloc((size_t)n_morae, 1);
    glottal = (unsigned char *)calloc((size_t)n_morae, 1);
    nasal_v1 = (unsigned char *)calloc((size_t)n_morae, 1);
    if (sp == NULL || ends == NULL || q_out == NULL || q_sp == NULL ||
        q_rel == NULL || same_v1 == NULL || same_v2 == NULL ||
        before_q == NULL || after_q == NULL || glottal == NULL ||
        nasal_v1 == NULL) {
        fail = 1;
        goto done;
    }
    for (i = 0; i < n_morae; i++)
        same_v1[i] = same_v2[i] = 1.0;

    /* Which morae sit either side of a geminate, so the vowels can be given
     * the durations and levels Idemaru & Guion measured. */
    for (i = 0; i < n_morae; i++) {
        if (morae[i].kind != JA_M_Q)
            continue;
        for (n = i - 1; n >= 0 && morae[n].kind == JA_M_SP; n--)
            ;
        if (n >= 0)
            before_q[n] = 1;
        if (i + 1 < n_morae)
            after_q[i + 1] = 1;
    }
    /*
     * Kitazawa: an accentual-phrase boundary where a vowel meets a vowel.  The
     * phrase-final mora lengthens and the phrase-initial one shortens, keeping
     * the pair at about two morae; the phrase-initial vowel is glottalized.
     */
    for (i = 0; i < n_morae; i++) {
        int v1, c2, v2;

        if (morae[i].kind != JA_M_SP || i == 0 || i + 1 >= n_morae)
            continue;
        v1 = (morae[i - 1].kind == JA_M_CV) ? morae[i - 1].v : JA_V_NONE;
        c2 = (morae[i + 1].kind == JA_M_CV) ? morae[i + 1].c : -1;
        v2 = (morae[i + 1].kind == JA_M_CV) ? morae[i + 1].v : JA_V_NONE;
        if (v1 < 0 || c2 != JA_C_NONE || v2 < 0)
            continue;           /* needs a vowel then a BARE vowel */
        glottal[i + 1] = 1;
        /*
         * Which cue marks the boundary follows from the particle, and his own
         * grouping gives it away: "ga|a, ni|i, no|o" show the +/-nasal
         * contrast, while "wa|a, shika|a, te|e, to|o" are glottalized.  The
         * first group is exactly the particles whose consonant is NASAL -- ni
         * and no have /n/, and ga is [Na] in Tokyo Japanese -- so the vowel
         * before the boundary is nasalized by its own consonant and the one
         * after it is not.  The two cues mostly co-occur rather than compete.
         */
        switch (morae[i - 1].c) {
        case JA_C_N: case JA_C_M: case JA_C_NY: case JA_C_MY:
            nasal_v1[i - 1] = 1;
            break;
        default:
            break;
        }
        if (v1 == v2) {         /* the case with no formant cue left at all */
            double r = HIATUS_RATIO;

            same_v1[i - 1] = 2.0 * r / (1.0 + r);
            same_v2[i + 1] = 2.0 / (1.0 + r);
        }
    }

    pcopy(prev_sp, ja_V[JA_V_A]);

    for (n = 0; n < n_morae; n++) {
        int c, v, mora_start = out.n;
        span *rec = &sp[n];
        ja_src s0, sa;
        int man, voiced, hold, vot;
        ja_post vsp, osp, tsp, bsp, csp;
        int has_bridge = 0, tsp_distinct = 0, longv;
        double bridge[2] = { 0.0, 0.0 };
        int nxt_c, fin, devoiced, grown, want, nv_, ntr = 0, apex = 0;
        double qv;
        int amp;

        rec->start = mora_start;
        rec->steady = 0; rec->has_steady = 0;
        rec->len = 0; rec->head = 0; rec->nominal = 0; rec->give = 0;
        rec->budget_break = morae[n].kind == JA_M_BAR || morae[n].kind == JA_M_BARBAR;

        if (morae[n].kind == JA_M_SP) {
            /* An accent phrase boundary is a PITCH event, not a silence: the
             * articulation runs straight through it.  It gets an entry in
             * `ends` so the mora times stay parallel to the morae, and no
             * frames. */
            ends[n] = out.n / 100.0;
            continue;
        }
        if (morae[n].kind == JA_M_BAR || morae[n].kind == JA_M_BARBAR) {
            /* a major phrase boundary: a pause */
            double pause = (morae[n].kind == JA_M_BARBAR) ? 0.300 : 0.100;
            int np = (int)ja_round(pause * 100.0 * B.rate_scale);

            taper(&out, 3, steady_at, 0);
            ja_src_clear(&s0);
            ja_src_set(&s0, 0, 0);
            if (push_n(&B, &out, prev_sp, &s0, np) != 0) { fail = 1; break; }
            rec->nominal = out.n - mora_start;   /* a pause is not spent */
            ends[n] = out.n / 100.0;
            steady_at = out.n;
            continue;
        }
        if (morae[n].kind == JA_M_LONG) {        /* long vowel: hold it */
            int extra = long_extra(&B, prev_v);

            steady_at = out.n;
            if (push_n(&B, &out, prev_sp, NULL, extra) != 0) { fail = 1; break; }
            rec->steady = steady_at; rec->has_steady = 1;
            rec->len = extra; rec->nominal = extra;
            ends[n] = out.n / 100.0;
            continue;
        }
        if (morae[n].kind == JA_M_Q) {           /* geminate: a closure */
            int nc = (n + 1 < n_morae) ? ja_onset_of(&morae[n + 1])
                                       : JA_C_NOMORA;
            int have_csp = ja_coda_post(nc, prev_v, csp);
            int nq, nman;

            /*
             * The vowel before a geminate still has an off-glide toward the
             * consonant being doubled, and it matters: Yanagisawa & Arai found
             * the 50% "geminate" crossover moves from 184 ms of closure WITH
             * the transition to 240 ms without -- the transition alone is worth
             * 56 ms of closure -- and without it even a 380 ms closure only
             * reached 80% identification.
             */
            if (have_csp &&
                offglide(&B, &out, prev_sp, csp,
                         glide_len(prev_sp, csp, OFFGLIDE), steady_at) != 0) {
                fail = 1; break;
            }
            taper(&out, 3, steady_at, 0);
            steady_at = out.n;
            nq = mora_frames(&B);
            nman = (nc > JA_C_NONE) ? ja_MANNER[nc].manner : JA_MAN_NONE;
            if (FRIC_GEMINATE && nman == JA_MAN_FRIC) {
                /*
                 * A GEMINATED FRICATIVE IS LONG FRICATION, NOT A CLOSURE.
                 * There is no closure phase in a long fricative at all; the
                 * noise simply runs for about twice as long.  Filling this
                 * with silence and letting the mora after it raise a
                 * singleton's worth of noise builds the one thing a fricative
                 * is not: silence followed by a short fricative is an
                 * AFFRICATE, which is what the ear reported -- スラッシュ
                 * arriving as "surachu".  Yamakawa & Amano, whose trapezoid
                 * fric_env is, put it plainly: the shape scales and only the
                 * total length distinguishes a fricative from an affricate.
                 *
                 * So the trapezoid is split across the two morae instead:
                 * this one raises the noise and holds it, the next holds it
                 * and lets it go.  No duration changes; only what fills it.
                 */
                int nv_ = morae[n + 1].v;
                int qvoiced = ja_MANNER[nc].voiced;
                int qkey, k, *qenv;
                ja_src qsrc;
                ja_post qsp;

                qenv = (int *)malloc((size_t)imax(1, nq) * sizeof *qenv);
                if (qenv == NULL) { fail = 1; break; }
                if (nc == JA_C_F) {
                    int qmode = ja_phi_mode(nv_, JA_C_NONE, 0);

                    ja_phi_post(nv_, qsp);
                    fric_env_ex(nq, ja_PHI[qmode][0], 0.40, 0.0, qenv);
                    trim_set(&B, 0, JA_C_F, -1);
                    for (k = 0; k < nq; k++) {
                        ja_src fs;

                        ja_src_clear(&fs);
                        ja_src_set(&fs, 2, qenv[k]);
                        ja_src_set(&fs, 0, 0);
                        if (push(&B, &out, qsp, &fs) != 0) {
                            fail = 1;
                            break;
                        }
                    }
                } else {
                    fric_noise(nc, qvoiced, &qsrc, &qkey);
                    if (!ja_noise_post(nc, nv_, qsp))
                        ja_onset_post(nc, nv_, qsp);
                    fric_env_ex(nq, qsrc.val[qkey], 0.40, 0.0, qenv);
                    trim_set(&B, nc, nc, -1);
                    for (k = 0; k < nq; k++) {
                        ja_src fq = qsrc;

                        ja_src_set(&fq, qkey, qenv[k]);
                        if (push(&B, &out, qsp, &fq) != 0) {
                            fail = 1;
                            break;
                        }
                    }
                }
                trim_clear(&B);
                free(qenv);
                if (fail) break;
                q_fric = 1;
            } else {
                ja_src_clear(&s0);
                ja_src_set(&s0, 0, 0);
                if (push_n(&B, &out, have_csp ? csp : prev_sp, &s0,
                           nq) != 0) { fail = 1; break; }
            }
            /* The geminate closure is the length cue itself, so it is budgeted
             * but never spent against -- compensating here would eat the
             * contrast the closure exists to carry. */
            rec->nominal = out.n - mora_start;
            ends[n] = out.n / 100.0;
            continue;
        }
        if (morae[n].kind == JA_M_N) {           /* the moraic nasal */
            int nc = (n + 1 < n_morae) ? ja_onset_of(&morae[n + 1])
                                       : JA_C_NOMORA;
            /* Final before a pause as well as at the end of the utterance: a
             * phrase boundary is a pause, and the nasal closes into it. */
            int fin = (nc == JA_C_NOMORA || nc == JA_C_BOUND);
            int place = ja_moraic_n(nc, fin);
            ja_post nsp;
            int have_csp;

            ja_onset_post(place == JA_C_N_M ? JA_C_M : JA_C_MN, JA_V_A, nsp);
            have_csp = place ? ja_oral_post(place, prev_v, csp) : 0;
            if (place == JA_C_N_NAS && have_csp) {
                /*
                 * No closure, so the murmur is not a nasal-cavity resonance
                 * with an F2 of its own: the oral tract is still open in the
                 * vowel's shape.  The posture ja_oral_post gives for this
                 * place IS the murmur -- F1 down to it, the vowel's F2 and F3
                 * kept.  A CLOSED place keeps the measured N2 instead, 1000 Hz
                 * alveolar and 1150 bilabial, which is what the ear passed.
                 */
                pcopy(nsp, csp);
            }
            /* The transition out of the preceding vowel takes the place the
             * nasal assimilates to, not always the alveolar one.  Where there
             * is no oral closure -- before a vowel, before /h/, utterance
             * finally -- there is no locus and the vowel keeps its posture. */
            if (have_csp) {
                if (offglide(&B, &out, prev_sp, csp,
                             glide_len(prev_sp, csp, OFFGLIDE),
                             steady_at) != 0) { fail = 1; break; }
            } else {
                nsp[0] = prev_sp[0]; nsp[1] = prev_sp[1];
                nsp[2] = prev_sp[2]; nsp[3] = prev_sp[3];
                nsp[4] = prev_sp[4] + 80; nsp[5] = prev_sp[5] + 80;
                nsp[6] = prev_sp[6] + 80;
            }
            steady_at = out.n;
            if (push_n(&B, &out, nsp, &ja_SRC_NASAL_A,
                       mora_frames(&B)) != 0) { fail = 1; break; }
            rec->nominal = out.n - mora_start;
            ends[n] = out.n / 100.0;
            pcopy(prev_sp, nsp);
            continue;
        }

        c = morae[n].c;
        v = morae[n].v;
        B.c = c;
        B.v = v;
        man = ja_MANNER[c].manner;
        voiced = ja_MANNER[c].voiced;
        hold = ja_MANNER[c].hold;
        vot = ja_MANNER[c].vot;
        /* Homma: VOT is far shorter word-medially than word-initially -- the
         * voiceless mean falls from 37 ms to 16.  MANNER holds the initial
         * figure, so medial takes the other table. */
        if (man == JA_MAN_STOP && ja_VOT_MEDIAL[c] && n > 0 &&
            !ja_is_boundary(&morae[n - 1])) {
            vot = ja_VOT_MEDIAL[c];
            hold = ja_CLOSURE_MEDIAL[voiced];
        }
        /* Arai: "phrase-initial /r/ is often produced as a plosive-like
         * sound".  Every /r/ here was the intervocalic flap wherever it fell,
         * including at the start of an utterance where there is no preceding
         * vowel for the flap to interrupt -- the dip had nothing to dip into
         * and the first half of its transition did not exist. */
        if (man == JA_MAN_FLAP && (n == 0 || ja_is_boundary(&morae[n - 1]))) {
            man = JA_MAN_STOP;
            hold = 3;
            vot = 2;
        }
        /*
         * Speaking rate, and the one part of it that is authored rather than
         * measured.  The vowel targets and the mora budget scale from the
         * figures the papers give, so those carry the rate; a consonant's
         * closure and VOT have no rate model in anything this project has
         * read, and leaving them fixed made the rate control saturate -- at
         * 200 and at 253 words per minute the utterance came out the same
         * length, because what was left to shorten was already at its floor.
         *
         * So they scale proportionally, with a floor of one frame.  It is
         * monotone, but not a calibrated Japanese rate law: rate sensitivity
         * differs among consonant classes (Katsuda & Kang 2026), so a general
         * claim that all consonants shorten less than vowels is unwarranted.
         * At the default rate the factor is exactly 1.0 and both values are
         * unchanged. Perceptual validation across rates remains separate.
         */
        if (hold > 0)
            hold = imax(1, (int)ja_round(hold * B.rate_scale));
        if (vot > 0)
            vot = imax(1, (int)ja_round(vot * B.rate_scale));
        /* A long vowel is one gesture of double length, so BOTH its morae take
         * the peripheral target, not just the held half -- which needs a look
         * at the next mora, since the mark that says so comes afterwards. */
        longv = (n + 1 < n_morae && morae[n + 1].kind == JA_M_LONG);
        if (v >= 0)
            ja_vowel_post(v, c, longv, vsp);
        else
            pcopy(vsp, prev_sp);
        if (!ja_onset_post(c, v, osp))
            pcopy(osp, vsp);
        if (c && prev_v >= 0 &&
            (man == JA_MAN_GLIDE || man == JA_MAN_FLAP ||
             ((man == JA_MAN_FRIC || man == JA_MAN_AFFRIC) && voiced))) {
            /*
             * F3 and F4 are not place cues for a voiced continuant.  Nothing
             * in LOCUS specifies them -- that table is F2 only -- so the onset
             * takes the FOLLOWING vowel's and the coda the PRECEDING one's.
             * For a stop that is invisible, because the closure between them
             * is silent.  For a glide, a flap or a voiced fricative the frames
             * are voiced and continuous, so the disagreement lands as a step
             * on the consonant's first frame: /iwa/ moved F3 672 Hz in 10 ms,
             * /uri/, /iyu/ and /iryu/ 720, /izu/ 720.
             *
             * Which of the two vowels is right?  Neither: the consonant has no
             * F3 of its own to be right about.  The midpoint splits the move in
             * half and hands each half to the offglide and the transition.
             */
            const double *t0 = ja_V[prev_v >= 0 ? prev_v : JA_V_A];
            const double *t1 = ja_V[v >= 0 ? v : JA_V_A];

            bridge[0] = (t0[2] + t1[2]) / 2.0;
            bridge[1] = (t0[3] + t1[3]) / 2.0;
            has_bridge = 1;
            osp[2] = bridge[0];
            osp[3] = bridge[1];
        }
        /* Where the transition into the vowel STARTS.  For every other manner
         * that is the consonant's own posture, but a nasal's posture is its
         * MURMUR -- a nasal-cavity resonance measured with the oral tract shut
         * -- and the release is an oral event.  The step from the murmur to the
         * oral locus is the release itself and belongs there. */
        if (man == JA_MAN_NASAL && ja_oral_post(c, v, tsp))
            tsp_distinct = 1;
        else
            pcopy(tsp, osp);
        /* The burst posture, settled once so the closure, the burst and the
         * transition out of it all use the same one. */
        if (!(man == JA_MAN_STOP && ja_burst_post(c, v, bsp)))
            pcopy(bsp, osp);
        if (man == JA_MAN_STOP && voiced) {
            /* Kochetov: a voiced stop makes less contact than its voiceless
             * partner, so its release is weaker and its cavity resonance less
             * sharply defined.  Pulling the posture halfway to the locus says
             * that, and it halves a step nothing can mask -- a voiced stop is
             * voiced throughout, where a voiceless one has a silent closure to
             * hide behind.  For /d/ the raw posture is the 4000/4080 ceiling
             * that only exists to place noise, so it is dropped entirely. */
            if (ja_has_burst_src(c))
                pcopy(bsp, osp);
            else {
                ja_post q;

                lerp(q, bsp, osp, 0.5);
                pcopy(bsp, q);
            }
            /* A voiced stop has one frame of VOT -- Homma's medial figure --
             * so there is no room between the burst and the vowel for the
             * formants to travel, and starting the glide at the locus left a
             * step.  Starting it AT the burst posture makes the path
             * continuous. */
            pcopy(tsp, bsp);
            tsp_distinct = 0;
        }
        if (nasal_v1[n]) {
            /* A vowel nasalized by its own nasal consonant, which is what
             * makes the contrast with the un-nasalized vowel across the
             * boundary.  Shows as "relatively lower high frequency energy", so
             * it is the bandwidths that widen. */
            vsp[4] += NASAL_BW; vsp[5] += NASAL_BW; vsp[6] += NASAL_BW;
        }
        if (c) {
            /* Kashino: the VC transition carries the consonant's place, so it
             * aims at a locus keyed on the vowel BEFORE it, not the one
             * after. */
            int og;

            if (!ja_coda_post(c, prev_v, csp))
                pcopy(csp, osp);
            if (has_bridge) {           /* the offglide aims at the same F3/F4 */
                csp[2] = bridge[0];
                csp[3] = bridge[1];
            }
            /*
             * The coda keys F2 on the PRECEDING vowel and the onset on the
             * following one, so the two disagree and the difference lands as a
             * step on the consonant's first frame.  For a stop that
             * disagreement is right and is Kashino's whole point: there IS a
             * closure between them and the articulator really does travel
             * through it.  A glide, a flap or a fricative has no closure to
             * travel through -- it is one gesture with one posture -- so for
             * those the vowel should simply aim at the posture the consonant
             * actually holds.  /iwa/ aimed at 1013 and held 887.
             */
            if (man == JA_MAN_GLIDE || man == JA_MAN_FLAP ||
                man == JA_MAN_FRIC || man == JA_MAN_AFFRIC)
                csp[1] = osp[1];
            /* F1 is not a place cue; it tracks how open the tract is, and a
             * voiced closure is shut.  The closure frames force F1 to 200
             * through the voice bar, but the coda was aiming the vowel at 480,
             * so F1 glided down to ~540 and then dropped 344 Hz in one frame
             * as the bar took over.  Aim at where it lands. */
            if (man == JA_MAN_STOP && voiced)
                csp[0] = 200;
            /* Arai measures Fg as the WHOLE transition, in and out, so half of
             * it belongs to the preceding vowel.  Only a floor: a bigger vowel
             * move still gets the frames the slew limit says it needs. */
            og = glide_len(prev_sp, csp, OFFGLIDE);
            if (man == JA_MAN_FLAP)
                og = imax(og, JA_FLAP_FG / 2);
            if (offglide(&B, &out, prev_sp, csp, og, steady_at) != 0) {
                fail = 1; break;
            }
        }

        /*
         * High vowels devoice between voiceless consonants, or finally after
         * one -- desu as [des].  njd_set_unvoiced_vowel is the same rule.
         *
         * A following BARE VOWEL is not one of those environments, and this
         * devoiced before one for years: the Python's split('a') is ('', 'a'),
         * so an arm testing the onset against the empty string fired on every
         * vowel-initial mora.  It was presumably meant to catch a phrase
         * boundary, but split(' ') is (' ', ''), so boundaries never reached it
         * and bare vowels always did -- /fuan/ devoiced its /u/, and so did
         * /shiai/ and /suugaku/ their high vowels.  Boundaries are listed
         * explicitly, which also makes a phrase-final high vowel devoice, as
         * "finally after one" intends.
         */
        /*
         * PHRASE-FINALLY the rule is not "devoice any eligible vowel".
         * Upstream's own apply_unvoice_rule opens with `if nxt is None:
         * return 0`, so with nothing following, only rule 1 (/masu/, /desu/)
         * and rule 2 (/shi/, which needs a part of speech) devoice at all.
         * Den & Koiso measure the same shape in 107 spontaneous narratives --
         * masu 81.33%, desu 79.54% against /shi/ 1.81% and /ku/ 1.66% -- so
         * devoicing every eligible final vowel was wrong twice over, and it
         * made romaji `sushi` and the kanji 寿司 two different words.
         * The MEDIAL arm is the ordinary environment and is unchanged.
         */
        nxt_c = (n + 1 < n_morae) ? ja_onset_of(&morae[n + 1]) : JA_C_NOMORA;
        fin = (nxt_c == JA_C_NOMORA || nxt_c == JA_C_BOUND);
        if (fin) {
            devoiced = polite_su(morae, n_morae, n, o->question);
        } else {
            /*
             * MEDIALLY it is upstream's rule 5: the mora's candidate class
             * decides, and each class has its own list of morae it devoices
             * before.  Testing the next onset for voicelessness instead --
             * which is what this did -- devoices /su/ before /shi/, where
             * upstream's list for that class has no s-row in it at all.
             */
            int cls = (morae[n].kind == JA_M_CV && morae[n].c > JA_C_NONE &&
                       morae[n].v >= 0)
                    ? ja_dv_cand[morae[n].c][morae[n].v] : 0;
            const ja_mora *nx = &morae[n + 1];

            devoiced = (cls > 0 && nx->kind == JA_M_CV &&
                        nx->c > JA_C_NONE && nx->v >= 0 &&
                        (ja_dv_follow[nx->c][nx->v] >> (cls - 1)) & 1);
        }
        /*
         * The rule above is a generalisation over the mora string, and it is
         * measurably coarser than the lexicon: njd_set_unvoiced_vowel blocks
         * two adjacent devoiced morae and protects the one carrying the accent
         * nucleus, and naist-jdic marks devoicing outright for the words it
         * knows.  Of the 179,679 morae this devoices over all 486,646
         * pronounced naist-jdic entries, 11.2% are the first of an adjacent
         * pair and 11.3% are accent nuclei.  So when
         * the caller has been through the analyser it passes the answer in and
         * this is not consulted.  Without it -- romaji input, or the oracle --
         * this rule decides, which is why its phrase-final arm is conditioned
         * above rather than firing on every eligible vowel.
         */
        if (o->devoiced != NULL && n < o->n_devoiced)
            devoiced = o->devoiced[n] != 0;

        /* These COMPOSE rather than one winning.  A vowel can be both
         * phrase-initial, which shortens it, and pre-geminate, which lengthens
         * it -- `ni itte` is exactly that -- and both effects are measured, so
         * neither has a claim to override the other. */
        qv = 1.0;
        if (before_q[n]) qv *= Q_V1_LONGER;
        if (after_q[n])  qv *= Q_V2_SHORTER;
        qv *= same_v1[n] * same_v2[n];
        amp = before_q[n] ? 60 + Q_INTENSITY
            : after_q[n]  ? 60 - Q_INTENSITY : 60;
        ntr = c ? glide_len(tsp, vsp, TRANS) : 0;
        if (!c && n > 0 && !ja_is_boundary(&morae[n - 1]) &&
            morae[n - 1].kind != JA_M_Q) {
            /*
             * A vowel-initial mora had NO transition at all, because the
             * transition loop keyed on there being a consonant.  Where the
             * mora before it also ended voiced -- another vowel, a long vowel,
             * or the moraic nasal -- that is simply wrong: nothing interrupts
             * the voicing, so the tongue travels continuously from one target
             * to the next and there is no boundary for a step to hide at.
             * `aoi` stepped F2 1228 -> 860 -> 2068, 1208 Hz in a single frame,
             * and `shiai`, `kaeru`, `iu` and the rest did the same.  Japanese
             * is full of these.
             *
             * /N/ before a vowel is the same case for a different reason --
             * utterance-medial /N/ there is a nasalised vowel or uvular, not an
             * oral closure that releases, so again there is nothing to break.
             * Only a pause or a geminate's silence licenses a step.
             */
            pcopy(tsp, prev_sp);
            ntr = glide_len(tsp, vsp, TRANS);
        }
        if (man == JA_MAN_FLAP)
            ntr = imax(ntr, JA_FLAP_FG - JA_FLAP_FG / 2);
        /* When the vowel devoices there is no voiced part left, so the
         * frication fills the mora instead.  Neither Yamakawa paper measured a
         * devoiced vowel -- both chose undevoiced ones -- so this is sized to
         * the mora, not to a measurement. */
        /* Set by a geminate that has already raised this consonant's noise,
         * so the fricative below holds it rather than ramping up again. */
        from_q = q_fric;
        q_fric = 0;

        grown = (devoiced && man == JA_MAN_FRIC) ? FRIC_GROWN
              : (devoiced && man == JA_MAN_AFFRIC) ? 2 : 0;
        /*
         * The vowel asks for its own duration instead of taking whatever the
         * mora has left over.  That residual is why every vowel came out the
         * same length: nothing in the chain knew which vowel it was.
         *
         * A mora is measured vowel-onset to vowel-onset, so a SILENT closure
         * belongs to the boundary rather than to the mora after it; counting
         * it here made every stop-initial mora 50 ms too long and held the
         * whole utterance to 6.2 morae/s against the papers' 7.  Frication and
         * nasal murmur do sound, so those stay inside the budget.
         */
        want = (v >= 0) ? vowel_frames(&B, v, 0) : VOWEL_FLOOR + TRANS;
        nv_ = imax(MIN_VOWEL, want - ntr);
        /* the paper measures the whole vowel, so the factor scales the glide
         * and the steady part together, not the steady part alone */
        nv_ = imax(1, (int)ja_round((nv_ + ntr) * qv) - ntr);

        if (man == JA_MAN_STOP) {
            const ja_src *after, *src;
            int nb, rest, k;

            /* A one-frame /ky/ release switches straight from its narrow
             * burst filter into voicing. At 11 kHz Kenta's stored noise tail
             * can then exceed full scale even when its steady RMS is matched.
             * Nine additional source units clear the aQkyua transition probe
             * (300 wpm) without changing any vowel amplitude or duration.
             * This transient allowance is separate from the steady table. */
            B.release_headroom = (B.engine_voice == 2 && B.sr == 11025 &&
                                  c == JA_C_KY && vot == 1) ? 9 : 0;
            /* Sidney's /kyu/ likewise rings into the devoiced vowel at
             * 300 wpm. Five units leave headroom (peak 26392 in the probe)
             * through this release without attenuating the vowel itself. */
            if (B.engine_voice == 1 && B.sr == 11025 &&
                c == JA_C_KY && v == JA_V_U)
                B.release_headroom = 5;
            /* Preserving the full vowel transition changes the filter state
             * reached after a one-frame palatal release. At the API maximum
             * of 253 wpm, apyua (Kenta/Keiko) and akyea (Keiko) need these
             * additional release-only allowances at 11 kHz. The measured
             * peaks are 25408, 23712 and 24472 respectively. */
            if (B.sr == 11025 && vot == 1) {
                if (B.engine_voice == 2 && c == JA_C_PY)
                    B.release_headroom = 8;
                if (B.engine_voice == 9 && c == JA_C_KY)
                    B.release_headroom = 9;
                if (B.engine_voice == 9 && c == JA_C_PY)
                    B.release_headroom = 7;
            }

            /* The closure takes the locus posture (that is where the formants
             * were heading as the vowel before it ended); the burst takes its
             * own, which is a cavity resonance and a different thing. */
            taper(&out, 3, steady_at, voiced);
            if (voiced) {
                /*
                 * Prevoicing: a voiced stop's closure carries a voice bar,
                 * where a voiceless one is silent.  Both were silent here.
                 *
                 * The bar takes the CODA posture, not the onset's.  During the
                 * closure the tongue is where it was when it closed, so the
                 * posture has to be continuous with the vowel that just ended;
                 * taking the onset posture, which is keyed on the FOLLOWING
                 * vowel, made the bar jump -- `age` moved F2 624 Hz on a
                 * voiced frame.  And it GLIDES across the closure rather than
                 * sitting still: the tongue travels from where it closed to
                 * where it releases, and with every frame voiced a step
                 * anywhere in here is heard.
                 */
                const double *base = c ? csp : osp;

                for (k = 0; k < hold; k++) {
                    ja_post q, vb;

                    lerp(q, base, bsp, (k + 1) / (double)(hold + 1));
                    ja_voice_bar_post(q, vb);
                    if (push(&B, &out, vb, &ja_SRC_VOICE_BAR) != 0) {
                        fail = 1; break;
                    }
                }
                if (fail) break;
            } else {
                ja_src_clear(&s0);
                ja_src_set(&s0, 0, 0);
                if (push_n(&B, &out, osp, &s0, hold) != 0) { fail = 1; break; }
            }
            ja_src_clear(&sa);
            ja_src_set(&sa, 0, 40);
            after = voiced ? &sa : &ja_SRC_ASPIR;
            src = ja_burst_src(c);
            if (src == NULL)
                src = after;
            /*
             * The high alveolar source belongs to the BURST ALONE.  Kitazawa &
             * Doshita measure the burst spectrum, which Morikawa takes over the
             * first 12.8 ms; the aspiration that follows is glottal noise
             * shaped by the tract, a different thing.  Putting the sibilant
             * source on every VOT frame gave /ta/ 30 ms of /s/-type noise and
             * no aspiration at all -- 43% of the frication of the affricate
             * /ts/, at a similar level -- so /ta/ was heard as [tsa].
             *
             * The floor here was 2 frames, which silently overrode any VOT
             * below 20 ms, so Homma's medial /p/ at 7 ms came out the same as
             * /t/ and /k/ and the position effect was invisible.  One frame is
             * still an audible burst, and it is about the 12.8 ms the burst
             * spectrum is measured over.
             */
            nb = ja_has_burst_src(c) ? 1 : imax(1, vot / 2);
            if (push_n(&B, &out, bsp, src, nb) != 0) { fail = 1; break; }
            /*
             * Then the formants leave the burst for the locus -- except where
             * the burst posture is not a vocal-tract state.  /t/ and /d/ have
             * F3 and F4 pinned at the 4000/4080 ceiling purely to place the
             * 5.3 kHz band-6 noise, which is a noise-shaping device exactly
             * like the sibilant postures, and gliding voiced frames out of it
             * is the same mistake as starting a transition from a fricative's
             * frication F2.  Measured on `ata`, F3 ran 2272 -> 4000 -> 3136 ->
             * 2272, a 1728 Hz excursion over 20 ms.  For those the post-burst
             * frames go straight to the locus; the step from the burst to it
             * lands on a noise frame, where it is masked.
             */
            rest = vot - nb;
            for (k = 0; k < imax(0, rest); k++) {
                if (ja_has_burst_src(c)) {
                    if (push(&B, &out, osp, after) != 0) { fail = 1; break; }
                } else {
                    ja_post q;

                    lerp(q, bsp, osp, (k + 1) / (double)(rest + 1));
                    if (push(&B, &out, q, src) != 0) { fail = 1; break; }
                }
            }
            if (fail) break;
        } else if (man == JA_MAN_AFFRIC) {
            ja_post nsp;
            ja_src sib;
            int nf, *env, k;

            if (!ja_noise_post(c, v, nsp))
                pcopy(nsp, osp);
            if (has_bridge)             /* the bridge has to reach these too */
                nsp[2] = bridge[0];
            taper(&out, 3, steady_at, voiced);
            ja_src_clear(&s0);
            ja_src_set(&s0, 0, 0);
            if (push_n(&B, &out, osp, &s0, hold) != 0) { fail = 1; break; }
            sib = voiced ? ja_SRC_SIBIL_Z : ja_SRC_SIBIL;
            nf = vot + grown;           /* the same count the budget reserved */
            env = (int *)malloc((size_t)imax(1, nf) * sizeof *env);
            if (env == NULL) { fail = 1; break; }
            fric_env(nf, sib.val[8], env);
            trim_set(&B, c, 0, -1);
            for (k = 0; k < nf; k++) {
                ja_src f8 = sib;

                ja_src_set(&f8, 8, env[k]);
                if (push(&B, &out, nsp, &f8) != 0) { fail = 1; break; }
            }
            trim_clear(&B);
            free(env);
            if (fail) break;
        } else if (man == JA_MAN_FRIC && c == JA_C_F) {
            /* Ruddell: /f/ is not one sound but four, and which one appears is
             * decided by what follows it.  See ja_tables.c. */
            int nx = (n + 1 < n_morae) ? ja_onset_of(&morae[n + 1])
                                       : JA_C_NOMORA;
            int nc, mode, peak, b2_on, b2_hold, b3, nf, nr, *env, k;
            ja_post q;

            if (nx == JA_C_NOMORA || nx == JA_C_BOUND || nx == JA_C_LONGM ||
                nx == JA_C_QMARK)
                nc = JA_C_NONE;         /* a boundary, a geminate, or /fu:/ */
            else
                nc = nx;
            mode = ja_phi_mode(v, nc, devoiced);
            peak = ja_PHI[mode][0];
            b2_on = ja_PHI[mode][1];
            b2_hold = ja_PHI[mode][2];
            b3 = ja_PHI[mode][3];
            ja_phi_post(v, q);
            nf = hold + grown;
            /* after a geminate the lips have already arrived and the noise
             * is already up, so there is no second ramp */
            nr = from_q ? 0 : imax(1, (int)(nf * 0.40 + 0.5));
            env = (int *)malloc((size_t)imax(1, nf) * sizeof *env);
            if (env == NULL) { fail = 1; break; }
            fric_env_ex(nf, peak, from_q ? 0.0 : 0.40, 0.22, env);
            trim_set(&B, 0, JA_C_F, -1);
            for (k = 0; k < nf; k++) {
                /* The lips arrive as the noise reaches level: it is one
                 * gesture, so the band sharpens on the same ramp the amplitude
                 * opens on.  F2 is held still throughout -- a moving F2 here
                 * would be heard as a transition, which is the opposite of
                 * what he describes. */
                double f = (nr && k < nr) ? (k + 1) / (double)nr : 1.0;
                double b2 = b2_on + (b2_hold - b2_on) * f;
                ja_post fp;
                ja_src fs;

                fp[0] = q[0]; fp[1] = q[1]; fp[2] = q[2]; fp[3] = q[3];
                fp[4] = q[4]; fp[5] = (int)(b2 + 0.5); fp[6] = b3;
                ja_src_clear(&fs);
                ja_src_set(&fs, 2, env[k]);
                ja_src_set(&fs, 0, 0);
                if (push(&B, &out, fp, &fs) != 0) { fail = 1; break; }
            }
            trim_clear(&B);
            free(env);
            if (fail) break;
        } else if (man == JA_MAN_FRIC) {
            ja_src src;
            ja_post nsp;
            int key, nf, *env, k;

            fric_noise(c, voiced, &src, &key);
            /* the frication is shaped by the noise posture; the glide after it
             * starts from the articulatory locus, which is a different place */
            if (!ja_noise_post(c, v, nsp))
                pcopy(nsp, osp);
            if (has_bridge)
                nsp[2] = bridge[0];
            nf = hold + grown;
            env = (int *)malloc((size_t)imax(1, nf) * sizeof *env);
            if (env == NULL) { fail = 1; break; }
            fric_env_ex(nf, src.val[key], from_q ? 0.0 : 0.40, 0.22, env);
            trim_set(&B, c, c, -1);
            for (k = 0; k < nf; k++) {
                ja_src fk = src;

                ja_src_set(&fk, key, env[k]);
                if (push(&B, &out, nsp, &fk) != 0) { fail = 1; break; }
            }
            trim_clear(&B);
            free(env);
            if (fail) break;
        } else if (man == JA_MAN_FLAP) {
            /* Arai's F2 track is a triangle: the contact is instantaneous and
             * there is no steady top, so the apex is a single frame. */
            if (push_n(&B, &out, osp, NULL, hold) != 0) { fail = 1; break; }
            apex = out.n - 1;
        } else if (man == JA_MAN_NASAL || man == JA_MAN_GLIDE) {
            const ja_src *src = (man == JA_MAN_NASAL) ? &ja_SRC_NASAL_A : NULL;
            int rel = (man == JA_MAN_NASAL && hold > 1 && tsp_distinct);

            if (push_n(&B, &out, osp, src, rel ? hold - 1 : hold) != 0) {
                fail = 1; break;
            }
            if (rel) {
                /*
                 * The release.  The murmur's resonances belong to the nasal
                 * cavity and the vowel's to the oral tract, so one does not
                 * glide into the other -- they swap, and that step is real.
                 * What keeps it from being heard as a click in speech is that
                 * it happens while the mouth is still nearly shut and the
                 * sound is weak.  So the step is placed AT MURMUR AMPLITUDE,
                 * one frame before the amplitude recovers, so the frame
                 * carrying the formant step carries no amplitude step and vice
                 * versa: the two discontinuities land on different frames
                 * instead of stacking.  Taken out of the murmur's own frames
                 * so the mora keeps its length.
                 */
                if (push(&B, &out, tsp, &ja_SRC_NASAL_A) != 0) {
                    fail = 1; break;
                }
            }
        }

        B.release_headroom = 0;
        B.c = JA_C_NONE;  /* transition/devoicing uses the vowel calibration */
        trim_set(&B, 0, 0, (devoiced && v >= 0) ? v : -1);
        for (i = 0; i < ntr; i++) {     /* 0 unless a transition is wanted */
            ja_post q;

            lerp(q, tsp, vsp, (i + 1) / (double)(ntr + 1));
            if (push(&B, &out, q, devoiced ? &ja_SRC_DEVOICED : NULL) != 0) {
                fail = 1; break;
            }
        }
        if (fail) break;
        if (man == JA_MAN_FLAP)
            dip(&out, apex, JA_FLAP_AVG, JA_FLAP_AVD);
        steady_at = out.n;
        {
            const ja_src *vsrc = NULL;
            ja_src va;

            if (devoiced)
                vsrc = &ja_SRC_DEVOICED;
            else if (amp != 60) {
                ja_src_clear(&va);
                ja_src_set(&va, 0, amp);
                vsrc = &va;
            }
            if (after_q[n]) {
                /* recorded relative to the mora, because the redistribution
                 * below moves every absolute index */
                q_sp[n_q] = n;
                q_rel[n_q] = out.n - ntr - mora_start;
                n_q++;
            }
            if (push_n(&B, &out, vsp, vsrc, imax(nv_, 1)) != 0) {
                fail = 1; break;
            }
        }
        trim_clear(&B);
        /*
         * The budget shifts with the vowel too, or compensation undoes the
         * whole thing: with the nominal fixed at MORA_FRAMES, a mora holding a
         * short /i/ underruns it and the redistribution inflates the vowel
         * back proportionally, flattening exactly the distinction being drawn.
         * Homma's rule is preserved -- the budget is still fixed before the
         * fact, and an expensive consonant is still paid for by the word's
         * vowels -- it is just no longer the same number for every vowel.
         */
        rec->steady = steady_at;
        rec->has_steady = 1;
        rec->adjustable = 1;
        memcpy(rec->steady_frame, fv_at(&out, steady_at), JA_NTRACK);
        rec->len = imax(nv_, 1);
        rec->nominal = mora_frames(&B) + vowel_frames(&B, v, 0) - v_default(&B);
        rec->head = glottal[n] ? GLOTTAL_FRAMES : 0;
        if (glottal[n]) {
            /* Creak is quieter as well as lower.  The F0 half of it is applied
             * in ja_pitch, which owns track 17; this is the amplitude half, at
             * the very start of the phrase-initial vowel. */
            dip(&out, steady_at + GLOTTAL_FRAMES / 2, GLOTTAL_FRAMES,
                GLOTTAL_DIP);
        }
        ends[n] = out.n / 100.0;
        pcopy(prev_sp, vsp);
        if (v >= 0)
            prev_v = v;
    }
    if (out.oom)
        fail = 1;
    if (!fail && compensate(&out, sp, n_morae, q_sp, q_rel, n_q,
                            ends, q_out) != 0)
        fail = 1;

    /* Kenta's /u/ leaves cascade state that can wrap when /s/ switches to
     * its noise posture at 11 kHz. Taper just the final source frame after
     * redistribution has fixed its position. The devoiced sashisuseso probe
     * needs 17 aspiration units at the API's maximum rate; use 18. With the
     * full transition preserved, the voiced 90-wpm probe needs five voicing
     * units (peak 26400). Neither changes duration or the voicing decision. */
    if (!fail && B.engine_voice == 2 && B.sr == 11025) {
        for (n = 0; n + 1 < n_morae; n++) {
            int end = (int)ja_round(ends[n] * 100.0);
            if (morae[n].v == JA_V_U && morae[n + 1].c == JA_C_S &&
                end > 0 && end <= out.n) {
                uint8_t *f = out.f + (end - 1) * JA_NTRACK;
                if (!f[0] && f[2]) f[2] = (uint8_t)imax(0, f[2] - 18);
                else if (f[0]) f[0] = (uint8_t)imax(0, f[0] - 5);
            }
        }
    }

done:
    if (fail) {
        fv_free(&out);
        free(ends);
        free(q_out);
    } else {
        /*
         * Ramp the three amplitude tracks to zero over the last frames.
         * Tracks 0, 1 and 2 are voicing, frication and aspiration -- the same
         * three the volume attenuation works on, because they are the three
         * that carry level.  The ramp is linear in track units, which are
         * about a decibel each, so it is heard as a short decay rather than
         * as a cut.  See FINAL_FADE above for why it is here at all.
         */
        if (out.n > 0 && FINAL_FADE > 0 && out.n >= FINAL_FADE_MIN) {
            /* At most half the utterance, so a one-mora word still has a
             * word in it; the ramp is a tail, not the whole thing. */
            int k = imin(FINAL_FADE, imax(1, out.n / 2)), fi;

            for (fi = 0; fi < k; fi++) {
                uint8_t *f = out.f + (size_t)(out.n - k + fi) * JA_NTRACK;
                int sub = FINAL_FADE_STEP * (fi + 1);
                int t;

                for (t = 0; t <= 2; t++)
                    f[t] = (uint8_t)imax(0, (int)f[t] - sub);
            }
        }
        if (out.n > 0 && FINAL_PAD > 0) {
            uint8_t *p = fv_grow(&out, FINAL_PAD);

            /*
             * Out of memory here ships the utterance unpadded rather than
             * failing it: the listener gets the click back, which is what
             * they had before, instead of silence.  Failing in this branch
             * would also skip the cleanup above and hand back a half-set
             * ja_utt.
             */
            if (p != NULL) {
                memset(p, 0, (size_t)FINAL_PAD * JA_NTRACK);
                out.n += FINAL_PAD;
            }
        }
        u->frames = out.f;
        u->n = out.n;
        u->ends = ends;
        u->n_ends = n_morae;
        u->q_ends = q_out;
        u->n_q = n_q;
    }
    free(sp);
    free(q_sp);
    free(q_rel);
    free(same_v1);
    free(same_v2);
    free(before_q);
    free(after_q);
    free(glottal);
    free(nasal_v1);
    return fail ? -1 : 0;
}
