/*
 * The Japanese phonetic inventory, and the posture functions that read it.
 *
 * Every number here comes from build/Japanese_test/jp_voice.py, which carries
 * the sourcing note for each one -- which paper, how many speakers, and in
 * several cases which earlier value it replaced and why that one was wrong.
 * Those notes are not repeated here, because a number with two homes acquires
 * two histories; what IS repeated is the marker, so it is visible at a glance
 * how much weight a row can carry:
 *
 *   [M]  Mokhtari & Tanaka 2000 -- long/doubled vowels, F1-F4 and B1-B3
 *   [T]  Tanaka ICPhS 2023 -- F2 of release/frication noise; onset proxies
 *   [Y]  Yazawa & Kondo 2019 -- vowel duration, 16 speakers, released data
 *   [L]  the general literature
 *   [A]  Arai LabPhon 14 -- the flap
 *   [Mo] Morikawa 1997 -- burst spectra
 *   [K]  Kochetov 2014 (EPG), Kariyasu 2003 (glides), Kitazawa (bursts)
 *   [E]  estimated from place of articulation; no source found
 *
 * tests/japanese_formant_test.py compares the vowel postures with the Python
 * and checks the source-derived anchors separately from the frame oracle.
 */
#include <math.h>
#include <string.h>

#include "ja.h"

/* ---- vowels -------------------------------------------------------------- */

/* [M] Steady long/doubled reference: F1 F2 F3 F4 B1 B2 B3; a i u e o. */
const double ja_V[JA_NV][7] = {
    { 737, 1225, 2275, 3304, 170,  99, 180 },   /* a */
    { 298, 2067, 2951, 3455,  61, 108, 126 },   /* i */
    { 356, 1293, 2224, 3282,  52, 111, 106 },   /* u */
    { 481, 1873, 2406, 3381,  54,  88, 222 },   /* e */
    { 456,  856, 2343, 3246,  57, 101, 107 }    /* o */
};

/* [M] Appendix A context medians; unseen related onsets are extrapolations.
 * No onset or an unrepresented place retains ja_V[U][1]. */
#define U_FRONT 1318
#define U_BACK  1087

static const unsigned char fronting[JA_NC] = {
    0,
    0, 1, 0, 1,                 /* k ky g gy */
    0, 0,                       /* t d */
    0, 1, 0, 1, 0,              /* p py b by v */
    1, 1, 1, 1, 1, 1,           /* s sh z j ch ts */
    0, 1, 0,                    /* h hy f */
    0, 1, 0, 1,                 /* n ny m my */
    0, 1,                       /* r ry */
    0, 1,                       /* w y */
    0, 0, 0, 0, 0
};

static const unsigned char backing[JA_NC] = {
    [JA_C_K] = 1, [JA_C_G] = 1, [JA_C_P] = 1, [JA_C_B] = 1,
    [JA_C_M] = 1, [JA_C_F] = 1, [JA_C_V] = 1
};

/* [Y] Male means pooled over BOTH positions, long minus short (F1-F3).
 * Subtract from [M]'s sustained reference for short vowels. Cross-corpus
 * transfer is a modelling approximation, not measured short-vowel targets. */
const double ja_LONG_DELTA[JA_NV][3] = {
    {  57,  -46,  56 },         /* a */
    {   5,  139, 145 },         /* i */
    {   4,    7, -35 },         /* u */
    {  17,   95,  72 },         /* e */
    {  -7, -136,  84 }          /* o */
};

/* [Y] short, long, in milliseconds; male speakers, embedded position. */
const double ja_V_DUR[JA_NV][2] = {
    { 74.4, 158.0 },            /* a */
    { 63.2, 141.5 },            /* i */
    { 63.7, 143.4 },            /* u */
    { 72.2, 148.4 },            /* e */
    { 70.6, 147.5 }             /* o */
};

/* ---- loci ---------------------------------------------------------------- */

/*
 * F2 as voicing begins -- the articulatory locus, which is NOT the frication's
 * own F2 for a fricative nor the murmur's N2 for a nasal.  Confusing those is
 * the single commonest error in this project's history and jp_voice.py names
 * nine instances of it.
 *
 * /r/ and /ry/ are computed in the Python by set_flap(), from Arai's one
 * measured point (1225 -> 1500 for /a/) through an alveolar locus of 1800 and
 * a palatalised one of 2150, with k = 0.478.  They are written out here rather
 * than recomputed, because the fit is a property of the chosen sub-phoneme and
 * not of anything this code decides; check_ja_tables.py verifies them against
 * a live set_flap().
 */
const short ja_LOCUS[JA_NC][JA_NV] = {
    {    0,    0,    0,    0,    0 },   /* -- no onset */
    { 1530, 2280, 1500, 2150, 1350 },   /* k   [T] */
    { 2200, 2280, 2300, 2280, 2220 },   /* ky  [T] */
    { 1484, 2248, 1469, 2108, 1276 },   /* g   [K] + [T] */
    { 2200, 2280, 2300, 2280, 2220 },   /* gy  [T] via /ky/ */
    { 1400, 1900, 1450, 1750, 1250 },   /* t   [Y] + [E], attained not virtual */
    { 1313, 1984, 1372, 1812, 1053 },   /* d   [K] + [Y], lenited */
    {  921, 1224,  946, 1154,  788 },   /* p   [L] labial locus + [T] k */
    { 2080, 2250, 2110, 2250, 2150 },   /* py  [T] */
    {  921, 1224,  946, 1154,  788 },   /* b   via /p/ */
    { 2080, 2250, 2110, 2250, 2150 },   /* by  [T] via /py/ */
    {  921, 1224,  946, 1154,  788 },   /* v   = /b/ */
    { 2100, 2450, 2120, 2140, 2130 },   /* s   [T] */
    { 2420, 2450, 2580, 2450, 2520 },   /* sh  [T] /sj/ */
    { 2100, 2450, 2120, 2140, 2130 },   /* z   [T] via /s/ */
    { 2420, 2450, 2580, 2450, 2520 },   /* j   [T] via /sj/ */
    { 2420, 2450, 2580, 2450, 2520 },   /* ch  [T] via /sj/ */
    { 1800, 2000, 1800, 1800, 1700 },   /* ts  [E] alveolar */
    { 1680, 2380, 1950, 2120, 1650 },   /* h   [T] */
    { 2200, 2380, 2180, 2380, 2300 },   /* hy  [T] /hj/ */
    {  921, 1224,  946, 1154,  788 },   /* f   via /p/; [P] is bilabial */
    { 1000, 1100, 1000, 1000,  950 },   /* n   [L] */
    { 1300, 1400, 1350, 1350, 1300 },   /* ny  [E] palatal */
    { 1150, 1250, 1100, 1150, 1050 },   /* m   [L] */
    { 1400, 1450, 1400, 1400, 1350 },   /* my  [E] */
    { 1500, 1939, 1535, 1838, 1307 },   /* r   [A] set_flap('flap') */
    { 1667, 2107, 1716, 2005, 1475 },   /* ry  [A] + [T] palatal anchor */
    {  887, 1013,  887,  950,  887 },   /* w   [K] Kariyasu's F1/F2 ratio */
    { 2100, 2100, 2100, 2100, 2100 },   /* y   [E] palatal */
    { 1000, 1100, 1000, 1000,  950 },   /* N   [L] */
    {    0,    0,    0,    0,    0 },   /* N_n -- oral(), not a locus */
    {    0,    0,    0,    0,    0 },   /* N_m */
    {    0,    0,    0,    0,    0 },   /* N_k */
    {    0,    0,    0,    0,    0 }    /* N_q */
};

/* ---- manner, voicing, and how many frames the consonant takes ----------- */

/*
 * hold is the closure or the frication; vot is what follows the release.  The
 * stop figures are word-INITIAL VOT (Journal of Phonetics, 13 speakers: /p/
 * 30.0, /t/ 28.5, /k/ 56.7 ms); ja_VOT_MEDIAL below replaces them elsewhere,
 * which is much the commoner position.  The fricative lengths carry Yamakawa &
 * Amano's finding that /s/ and [C] are about 1.8 times /ts/ and [tC].
 */
#define MAN(m, vc, h, vt) { JA_MAN_##m, vc, h, vt }
const ja_manner ja_MANNER[JA_NC] = {
    MAN(NONE,   1,  0, 0),      /* -- a bare vowel */
    MAN(STOP,   0,  5, 6),      /* k   velar VOT is nearly twice the others */
    MAN(STOP,   0,  5, 6),      /* ky */
    MAN(STOP,   1,  4, 1),      /* g */
    MAN(STOP,   1,  4, 1),      /* gy */
    MAN(STOP,   0,  5, 3),      /* t */
    MAN(STOP,   1,  4, 1),      /* d */
    MAN(STOP,   0,  5, 3),      /* p */
    MAN(STOP,   0,  5, 3),      /* py */
    MAN(STOP,   1,  4, 1),      /* b */
    MAN(STOP,   1,  4, 1),      /* by */
    MAN(STOP,   1,  4, 1),      /* v   = /b/ */
    MAN(FRIC,   0, 10, 0),      /* s */
    MAN(FRIC,   0, 10, 0),      /* sh */
    MAN(FRIC,   1,  5, 0),      /* z */
    MAN(AFFRIC, 1,  4, 5),      /* j */
    MAN(AFFRIC, 0,  4, 6),      /* ch */
    MAN(AFFRIC, 0,  4, 5),      /* ts */
    MAN(FRIC,   0,  5, 0),      /* h */
    MAN(FRIC,   0,  6, 0),      /* hy */
    MAN(FRIC,   0,  6, 0),      /* f */
    MAN(NASAL,  1,  5, 0),      /* n */
    MAN(NASAL,  1,  5, 0),      /* ny */
    MAN(NASAL,  1,  5, 0),      /* m */
    MAN(NASAL,  1,  5, 0),      /* my */
    MAN(FLAP,   1,  1, 0),      /* r */
    MAN(FLAP,   1,  1, 0),      /* ry */
    MAN(GLIDE,  1,  4, 0),      /* w */
    MAN(GLIDE,  1,  4, 0),      /* y */
    MAN(NASAL,  1,  0, 0),      /* N   the moraic nasal; build() times it */
    MAN(NONE,   1,  0, 0),      /* N_n these four are places, not onsets, so */
    MAN(NONE,   1,  0, 0),      /* N_m they take the dict default the Python */
    MAN(NONE,   1,  0, 0),      /* N_k MANNER.get() would have given them */
    MAN(NONE,   1,  0, 0)       /* N_q */
};

/*
 * Homma 1981 Table III: VOT depends on position in the word, and strongly --
 * the voiceless mean falls from 37 ms word-initially to 16 ms medially.  A
 * single figure taken from initial measurements ran every medial stop two to
 * four times long, and medial is the commoner position.  0 means "no entry",
 * which is what /v/ has: the alias copied MANNER and LOCUS and not this.
 */
const short ja_VOT_MEDIAL[JA_NC] = {
    0,
    2, 2, 1, 1,                 /* k ky g gy */
    2, 1,                       /* t d */
    1, 1, 1, 1, 0,              /* p py b by v */
    0, 0, 0, 0, 0, 0,
    0, 0, 0,
    0, 0, 0, 0,
    0, 0,
    0, 0,
    0, 0, 0, 0, 0
};

/* Homma Table II, medial single-stop closure: voiceless 67 ms, voiced 44. */
const short ja_CLOSURE_MEDIAL[2] = { 7, 4 };

/* ---- stop bursts -------------------------------------------------------- */

/* Centre frequency of the release burst, per following vowel.  [T] + [Mo];
 * /t/ and /d/ are flat across 0-5 kHz, which is Morikawa's own figure. */
const short ja_BURST_PEAK[JA_NC][JA_NV] = {
    { 0, 0, 0, 0, 0 },
    { 1300, 3100, 1100, 2100,  800 },   /* k */
    { 2500, 3100, 3200, 3100, 2600 },   /* ky */
    { 1300, 3100, 1100, 2100,  800 },   /* g */
    { 2500, 3100, 3200, 3100, 2600 },   /* gy */
    { 3000, 3000, 3000, 3000, 3000 },   /* t  flat  [Mo] */
    { 3000, 3000, 3000, 3000, 3000 },   /* d */
    {  600, 1200, 1000,  700,  600 },   /* p */
    { 1000, 1200, 1600, 1200,  950 },   /* py */
    {  600, 1200, 1000,  700,  600 },   /* b */
    { 1000, 1200, 1600, 1200,  950 },   /* by */
    { 0, 0, 0, 0, 0 },                  /* v  -- no row, as in the Python */
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }
};

/* How sharp that peak is.  /k/ is the resonant one, /t/ flat, /p/ falling. */
const short ja_BURST_BW[JA_NC] = {
    0,
    110, 110, 110, 110,         /* k ky g gy */
    520, 520,                   /* t d */
    380, 380, 380, 380, 0,      /* p py b by v */
    0, 0, 0, 0, 0, 0,
    0, 0, 0,
    0, 0, 0, 0,
    0, 0,
    0, 0,
    0, 0, 0, 0, 0
};

/* ---- noise postures ----------------------------------------------------- */

/*
 * What shapes a sibilant's frication.  F3 and F4 here are a device for placing
 * noise and not a vocal-tract state, which is why ja_coda_post and
 * ja_onset_post never start a voiced transition from them.
 *
 * Track 6 drives F5, which the engine derives as max(3970, effF4 + 400) and
 * which no input track sets, so the /s/ row's noise lands at 4720 Hz -- above
 * the 4080 the input byte can express.  See jp_voice.effective_resonators.
 */
const double ja_SIB_POST[JA_NC][7] = {
    { 0 },
    { 0 }, { 0 }, { 0 }, { 0 },
    { 0 }, { 0 },
    { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
    { 300, 2100, 4000, 4080, 110, 150, 210 },   /* s   peak ~4650 */
    { 300, 2450, 3000, 3700, 120, 180, 240 },   /* sh  peak ~3770 */
    { 300, 2100, 4000, 4080, 110, 150, 210 },   /* z */
    { 300, 2450, 3000, 3700, 120, 180, 240 },   /* j */
    { 300, 2450, 3000, 3700, 120, 180, 240 },   /* ch */
    { 300, 1800, 4000, 4080, 110, 150, 210 },   /* ts */
    { 0 }, { 0 }, { 0 },
    { 0 }, { 0 }, { 0 }, { 0 },
    { 0 }, { 0 },
    { 0 }, { 0 },
    { 0 }, { 0 }, { 0 }, { 0 }, { 0 }
};

/*
 * The F2 a VOICED transition may start from where LOCUS holds a frication
 * measurement.  [E], locus theory.  /sh/, /ch/ and /j/ really are palatalised
 * in Japanese, so the glide belongs on those; only the plain alveolars had to
 * be pulled back from Tanaka's noise F2.
 */
const short ja_VOICED_F2[JA_NC][JA_NV] = {
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 1750, 2000, 1700, 1850, 1650 },   /* s */
    { 2000, 2200, 2050, 2100, 1950 },   /* sh */
    { 1750, 2000, 1700, 1850, 1650 },   /* z */
    { 2000, 2200, 2050, 2100, 1950 },   /* j */
    { 2000, 2200, 2050, 2100, 1950 },   /* ch */
    { 1750, 2000, 1700, 1850, 1650 },   /* ts */
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }
};

/*
 * A nasal's ORAL place, for the transitions either side -- not its murmur.
 * Fujimura's N2 ~1000 Hz is a resonance of the nasal cavity measured with the
 * oral tract shut, and taking it for the transition's locus made /ne/ sweep F2
 * from 1004 to 1876 in 50 ms, which is heard as an extra segment.  A nasal
 * shares its oral place with the homorganic stop, so these are those rows:
 * /n/ and /N/ take /t/, /ny/ takes /ky/, /my/ takes /py/, and /m/ takes the
 * labial locus -- NOT /p/'s burst-derived row, which would give /mo/ a 924 Hz
 * fall out of a bilabial where labials barely move F2 into a back vowel.
 */
const short ja_NASAL_ORAL[JA_NC][JA_NV] = {
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 },
    { 1400, 1900, 1450, 1750, 1250 },   /* n  -> /t/ */
    { 2200, 2280, 2300, 2280, 2220 },   /* ny -> /ky/ */
    {  921, 1224,  946, 1154,  788 },   /* m  -> the labial locus */
    { 2080, 2250, 2110, 2250, 2150 },   /* my -> /py/ */
    { 0, 0, 0, 0, 0 },                  /* r */
    { 0, 0, 0, 0, 0 },                  /* ry */
    { 0, 0, 0, 0, 0 },                  /* w */
    { 0, 0, 0, 0, 0 },                  /* y */
    { 1400, 1900, 1450, 1750, 1250 },   /* N  -> /t/ */
    { 1400, 1900, 1450, 1750, 1250 },   /* N_n -> /t/ */
    {  921, 1224,  946, 1154,  788 },   /* N_m -> the labial locus */
    { 1530, 2280, 1500, 2150, 1350 },   /* N_k -> /k/ */
    { 0, 0, 0, 0, 0 }                   /* N_q -- handled without a locus */
};

/* ---- /f/ = [P], Ruddell 2011 -------------------------------------------- */

/*
 * Four realizations, not two: [f], [P], [P/h] and [h].  Each row is the
 * track-2 peak, B2 at onset, B2 held, and B3.  The bandwidths carry his main
 * finding, which is a shape in time rather than a level: a band at 1.7-1.8 kHz
 * is absent from the start of [P] (33 of 36 tokens) and present from the first
 * instant of [P/h] (31 of 33).  A constriction still closing damps its own
 * resonance, so that is a bandwidth -- and it has to be, because moving F2
 * instead would be heard as a formant transition, i.e. as a stray phoneme.
 */
const short ja_PHI[4][4] = {
    { 82, 120, 120, 250 },      /* [f]   loanword, bilingual; unreachable */
    { 80, 400, 120, 320 },      /* [P]   the band fades in */
    { 80, 120, 120, 320 },      /* [P/h] steady lips */
    { 76, 140, 140, 400 }       /* [h]   "little to no frication besides" */
};

/* ---- noise sources ------------------------------------------------------ */

#define SRC1(a, av)                   { (1u << (a)), { [a] = av } }
#define SRC2(a, av, b, bv)            { (1u << (a)) | (1u << (b)), \
                                        { [a] = av, [b] = bv } }
#define SRC4(a, av, b, bv, c, cv, d, dv)                                      \
    { (1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)),                  \
      { [a] = av, [b] = bv, [c] = cv, [d] = dv } }
#define SRC5(a, av, b, bv, c, cv, d, dv, e, ev)                               \
    { (1u << (a)) | (1u << (b)) | (1u << (c)) | (1u << (d)) | (1u << (e)),    \
      { [a] = av, [b] = bv, [c] = cv, [d] = dv, [e] = ev } }

/* Track 2 is aspiration through the cascade; track 8 is raw 4-8 kHz noise,
 * and the parallel bands 4-6 put noise on the resonators. */
const ja_src ja_SRC_ASPIR     = SRC2(2, 80, 0, 0);
const ja_src ja_SRC_NASAL_A   = SRC1(0, 52);
const ja_src ja_SRC_DEVOICED  = SRC2(0, 0, 2, 76);
/* A voiced stop's closure is not silent: Japanese /b d g/ are prevoiced and
 * the low frequencies radiate through the walls as a voice bar.  Both closures
 * used to be silent here, so VOT alone separated /b/ from /p/ -- where in
 * Japanese prevoicing is the stronger cue of the two. */
const ja_src ja_SRC_VOICE_BAR = SRC1(0, 32);
const ja_src ja_SRC_SIBIL     = SRC5(1, 80, 8, 36, 5, 80, 6, 80, 0, 0);
const ja_src ja_SRC_SIBIL_S   = SRC4(1, 80, 8, 44, 6, 70, 0, 0);
/* /z/ and /j/: the noise goes on track 8, which is not tied to a formant, so
 * the voicing is not left driving an F3 pushed to the ceiling. */
const ja_src ja_SRC_SIBIL_Z   = SRC4(1, 80, 8, 40, 6, 70, 0, 40);
/* An alveolar burst peaks at 5.3 kHz (Kitazawa & Doshita), above what the
 * cascade reaches, so it is carried the way /s/ is -- and /d/ keeps its
 * VOICING through the release, which it did not when this was first added. */
const ja_src ja_SRC_BURST_T   = SRC4(1, 80, 8, 40, 6, 66, 0, 0);
const ja_src ja_SRC_BURST_D   = SRC4(1, 80, 8, 30, 6, 56, 0, 40);

/*
 * C's round(), written out.  The 32-bit build is freestanding against
 * msvcrt.dll, which predates C99 and exports no round() -- and borrowing
 * floor(x + 0.5) instead would be wrong, not merely different: for the double
 * just below a half, 0.49999999999999994, the addition rounds up to exactly
 * 1.0 and floor gives 1 where round gives 0.
 *
 * Below 2^52 a double's fractional part is exact, so comparing it against 0.5
 * decides the case with no rounding of its own.
 */
static double c_round(double x)
{
    double a = x < 0.0 ? -x : x;
    double r;

    if (a >= 4503599627370496.0)        /* 2^52: already an integer */
        return x;
    r = floor(a);
    if (a - r >= 0.5)
        r += 1.0;
    return x < 0.0 ? -r : r;
}

double ja_round(double x)
{
    double r = c_round(x);

    if (fabs(x - r) == 0.5)
        r = 2.0 * c_round(x / 2.0);
    return r;
}

int ja_has_burst_src(int c)
{
    return c == JA_C_T || c == JA_C_D;
}

const ja_src *ja_burst_src(int c)
{
    if (c == JA_C_T)
        return &ja_SRC_BURST_T;
    if (c == JA_C_D)
        return &ja_SRC_BURST_D;
    return NULL;
}

void ja_src_clear(ja_src *s)
{
    memset(s, 0, sizeof *s);
}

void ja_src_set(ja_src *s, int track, int value)
{
    if (track >= 0 && track < JA_NTRACK) {
        s->mask |= 1u << track;
        s->val[track] = (int16_t)value;
    }
}

/* ---- rate compensation for the noise branches --------------------------- */

/*
 * Our noise settings were fitted at 16 kHz and come out louder at 11025,
 * because the resonators are normalised at zero frequency and so do not
 * preserve noise RMS across sample rates.  These are the unit offsets that
 * level-match each posture at the two rates, FOUND BY SEARCH rather than
 * derived: a track unit is not a decibel, and track 6 saturates above about
 * 66, so the first four units of a cut can do nothing at all.
 */
const short ja_NOISE_TRIM_UNITS[JA_NC] = {
    0,
    0, 0, 0, 0,
    0, 0,
    0, 0, 0, 0, 0,
    20, 26, 13, 14, 17, 30,     /* s sh z j ch ts */
    0, 0, 0,
    0, 0, 0, 0,
    0, 0,
    0, 0,
    0, 0, 0, 0, 0
};

/* Track 2 goes through the cascade, so its penalty varies with the posture's
 * own formants -- +8.65 dB on /i/ against +0.74 on /o/ -- and needs one entry
 * per posture rather than one number.  This matters more than its size
 * suggests: track 2 carries every devoiced vowel, and over all 486,646
 * pronounced naist-jdic entries those are 10.1% of CV morae and appear in
 * 30.5% of entries. */
const short ja_ASPIR_TRIM_C[JA_NC] = {
    0,
    0, 0, 0, 0,
    0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    4, 6, 2,                    /* h hy f */
    0, 0, 0, 0,
    0, 0,
    0, 0,
    0, 0, 0, 0, 0
};

const short ja_ASPIR_TRIM_V[JA_NV] = { 4, 9, 3, 4, 1 };   /* a i u e o */

/* ---- the posture functions ---------------------------------------------- */

static void post_set(ja_post p, double f1, double f2, double f3, double f4,
                     double b1, double b2, double b3)
{
    p[0] = f1; p[1] = f2; p[2] = f3; p[3] = f4;
    p[4] = b1; p[5] = b2; p[6] = b3;
}

static double locus_f2(int c, int v)
{
    if (c > 0 && c < JA_NC && v >= 0 && v < JA_NV && ja_LOCUS[c][v] != 0)
        return (double)ja_LOCUS[c][v];
    return 1500.0;              /* the Python's LOCUS.get(c, {}).get(v, 1500) */
}

static int has_locus(int c, int v)
{
    return c > 0 && c < JA_NC && v >= 0 && v < JA_NV && ja_LOCUS[c][v] != 0;
}

static int voiced_f2(int c, int v, double *out)
{
    if (c > 0 && c < JA_NC && v >= 0 && v < JA_NV && ja_VOICED_F2[c][v] != 0) {
        *out = (double)ja_VOICED_F2[c][v];
        return 1;
    }
    return 0;
}

/* Long vowels retain the sustained reference, short vowels subtract delta. */
int ja_vowel_post(int v, int ctx_c, int longv, ja_post out)
{
    int k;

    if (v < 0 || v >= JA_NV)
        return 0;
    for (k = 0; k < 7; k++)
        out[k] = ja_V[v][k];
    if (v == JA_V_U && ctx_c > 0 && ctx_c < JA_NC) {
        if (fronting[ctx_c]) out[1] = U_FRONT;
        else if (backing[ctx_c]) out[1] = U_BACK;
    }
    if (!longv) {
        out[0] -= ja_LONG_DELTA[v][0];
        out[1] -= ja_LONG_DELTA[v][1];
        out[2] -= ja_LONG_DELTA[v][2];
    }
    return 1;
}

/*
 * The consonant's posture before vowel v: its locus with the vowel's upper
 * formants, which is what a locus means.
 */
int ja_onset_post(int c, int v, ja_post out)
{
    double f2, f1;
    int man;
    const double *tgt;

    if (c <= 0 || c >= JA_NC)
        return 0;
    f2 = locus_f2(c, v);
    man = ja_MANNER[c].manner;
    if (man == JA_MAN_NASAL) {
        post_set(out, 270, f2, 2400, 3300, 140, 220, 280);   /* [L] N1 250-300 */
        return 1;
    }
    if (c == JA_C_H) {
        /* [h] is glottal: no supralaryngeal constriction, so the tract is
         * already in the shape of the vowel and [h] is a voiceless version of
         * it.  Tanaka's frication F2 for /ho/ is 1650 against the vowel's 856,
         * and that 794 Hz gap is heard as an extra phoneme in "nihon".  His
         * number is not wrong; it is a property of the noise.  /hy/ = [C] and
         * /f/ = [P] keep their own postures: those have a real constriction. */
        tgt = ja_V[(v >= 0 && v < JA_NV) ? v : JA_V_A];
        post_set(out, tgt[0], tgt[1], tgt[2], tgt[3],
                 tgt[4] + 50, tgt[5] + 50, tgt[6] + 50);
        return 1;
    }
    voiced_f2(c, v, &f2);
    tgt = ja_V[(v >= 0 && v < JA_NV) ? v : JA_V_A];
    f1 = (man == JA_MAN_FLAP) ? (double)JA_FLAP_F1
       : (man == JA_MAN_FRIC || man == JA_MAN_AFFRIC || man == JA_MAN_GLIDE)
         ? 300.0 : 400.0;
    if (man == JA_MAN_FLAP && tgt[0] < f1)
        f1 = tgt[0];            /* the flap is not a full closure */
    post_set(out, f1, f2, tgt[2], tgt[3], 110, 150, 210);
    return 1;
}

/*
 * Where the preceding vowel's formants head as the consonant closes.  Kashino
 * 1990: with the burst and most of the CV transition replaced by noise, 82% of
 * intervocalic stops were still identified when this was intact against 41%
 * without -- so it carries about as much place information as the burst, and
 * it has to carry the RIGHT place.  Hence keyed on the PRECEDING vowel.
 */
int ja_coda_post(int c, int pv, ja_post out)
{
    double f2, f1, lim;
    int man;
    const double *t;

    if (c == JA_C_H || pv < 0 || pv >= JA_NV)
        return 0;               /* glottal: nothing to head toward */
    if (!has_locus(c, pv))
        return 0;
    f2 = (double)ja_LOCUS[c][pv];
    voiced_f2(c, pv, &f2);
    man = (c > 0 && c < JA_NC) ? ja_MANNER[c].manner : JA_MAN_NONE;
    if (man == JA_MAN_NASAL)
        return ja_oral_post(c, pv, out);  /* toward the closure, not the murmur */
    t = ja_V[pv];
    /* A closure pulls F1 down whatever its place, but it cannot pull it below
     * where the vowel already sits: /a/ goes 750 -> 500, /i/ starts at 298. */
    lim = (man == JA_MAN_FLAP) ? (double)JA_FLAP_F1
        : (man == JA_MAN_FRIC || man == JA_MAN_AFFRIC || man == JA_MAN_GLIDE)
          ? 300.0 : 480.0;
    f1 = t[0] < lim ? t[0] : lim;
    post_set(out, f1, f2, t[2], t[3], 110, 150, 210);
    return 1;
}

/*
 * A nasal's oral posture: where the formants sit as the closure makes or
 * breaks, as against the murmur that sounds while it is held.
 */
int ja_oral_post(int c, int v, ja_post out)
{
    const double *t;
    double f2;

    if (v < 0 || v >= JA_NV)
        return 0;
    t = ja_V[v];
    if (c == JA_C_N_Q || c == JA_C_N_NAS) {
        /* Utterance-final /N/, which the literature calls uvular.  The closure
         * lowers F1, so that part is kept.  F2 carries place and there is no
         * measured uvular locus to aim at -- borrowing the velar row points
         * the WRONG WAY, because the velar locus for /a/ is 1530, above /a/'s
         * own 1225, so a velar pinch raises F2 where a uvular must lower it.
         * And final /N/ has no place contrast to signal: it is the only nasal
         * that occurs there, so nothing is lost by declining to guess. */
        post_set(out, t[0] < 300.0 ? t[0] : 300.0, t[1], t[2], t[3],
                 110, 150, 210);
        return 1;
    }
    if (c <= 0 || c >= JA_NC || ja_NASAL_ORAL[c][v] == 0)
        return 0;
    f2 = (double)ja_NASAL_ORAL[c][v];
    /* the oral tract is shut, so F1 is as low as the vowel will let it go */
    post_set(out, t[0] < 300.0 ? t[0] : 300.0, f2, t[2], t[3], 110, 150, 210);
    return 1;
}

/*
 * The posture for the release burst -- not the locus, which is where the
 * formants are heading afterwards.  A burst is a cavity resonance.
 *
 * The peak goes on whichever track can reach it: F2 stops at 2540 Hz in this
 * engine, so a velar burst before /i/ at 3100 has to sit on F3.  Whatever does
 * not carry the peak is damped hard, or it forms a competing lump.
 */
int ja_burst_post(int c, int v, ja_post out)
{
    double f, bw;

    if (c <= 0 || c >= JA_NC || v < 0 || v >= JA_NV)
        return 0;
    if (ja_BURST_PEAK[c][v] == 0)
        return 0;
    f = (double)ja_BURST_PEAK[c][v];
    bw = ja_BURST_BW[c] ? (double)ja_BURST_BW[c] : 300.0;
    if (c == JA_C_T || c == JA_C_D) {
        /* Kitazawa & Doshita, 28 speakers: [t] is the high-rising one, peaking
         * at 5.3 kHz.  That is above the 4080 Hz F4 reaches, exactly as /s/'s
         * cavity is, so the burst cannot be made in the cascade at all. */
        post_set(out, 300, 1800, 4000, 4080, 110, 150, 210);
        return 1;
    }
    if (c == JA_C_P || c == JA_C_PY || c == JA_C_B || c == JA_C_BY) {
        /* Two Japanese sources disagree here and neither is obviously wrong:
         * Morikawa measures diffuse-falling, Kitazawa & Doshita flat.  He
         * offers the reconciliation himself -- falling needs the low-frequency
         * energy only careful speech has, and Morikawa's are read CV
         * syllables.  Left on Morikawa. */
        if (f < 600.0)
            f = 600.0;
        else if (f > 1800.0)
            f = 1800.0;
        post_set(out, 280, f, f + 1400, f + 2200, 260, bw, 700);
        return 1;
    }
    /* Velar: one sharp resonance.  There is no B4 track, so F4 cannot be
     * damped -- it always rings wherever it is put.  Setting it just above the
     * peak makes it reinforce the peak instead of forming a second lump. */
    if (f <= 2400.0) {
        post_set(out, 350, f, f + 700, f + 1200, 420, bw, 700);
        return 1;
    }
    post_set(out, 350, (f - 1400.0) > 600.0 ? f - 1400.0 : 600.0, f,
             (f + 700.0) < 4080.0 ? f + 700.0 : 4080.0, 460, 900, bw);
    return 1;
}

/*
 * The posture used only while frication is sounding, to place the noise peak.
 *
 * "Only while frication is sounding" was the whole safety argument, and it
 * held only because every consonant using this table was voiceless.  /z/ and
 * /j/ are voiced and their frication frames carry track 0, so on those a
 * 4000 Hz F3 is a resonance the voice is driving: in /uza/ that put F3 at
 * 2224 -> 4000 -> 2272 over five voiced frames.  So a voiced sibilant keeps
 * the tract's own F3 and places its noise with ja_SRC_SIBIL_Z instead.
 */
int ja_noise_post(int c, int v, ja_post out)
{
    const double *q;
    double f2;
    int k;

    if (c <= 0 || c >= JA_NC || ja_SIB_POST[c][0] == 0.0)
        return 0;
    q = ja_SIB_POST[c];
    for (k = 0; k < 7; k++)
        out[k] = q[k];
    f2 = has_locus(c, v) ? (double)ja_LOCUS[c][v] : q[1];
    if (ja_MANNER[c].voiced) {
        voiced_f2(c, v, &f2);
        out[1] = f2;
        /* F3 from the tract; F4 left at the ceiling for band 6 */
        out[2] = ja_V[(v >= 0 && v < JA_NV) ? v : JA_V_A][2];
        return 1;
    }
    out[1] = f2;
    return 1;
}

/*
 * The posture that shapes /f/'s frication.  Before /u/ this is Ruddell's
 * measured band; before the other four there is no measurement, so it takes
 * the VOWEL's own F2 -- a bilabial slit has no front cavity of its own, so the
 * noise is excited into a tract already shaped for the vowel.  Not the band:
 * he ties that to rounding for a following /u/, and before /a/ it would push
 * the noise 475 Hz ABOVE the vowel, where a lip constriction can only pull F2
 * down.
 */
int ja_phi_post(int v, ja_post out)
{
    const double *t = ja_V[(v >= 0 && v < JA_NV) ? v : JA_V_A];

    post_set(out, 300, v == JA_V_U ? (double)JA_PHI_BAND : t[1], t[2], t[3],
             200, 150, 320);
    return 1;
}

/*
 * Which of Ruddell's four sounds appears before vowel `v`, where `nxt_c` is
 * the consonant beginning the following mora.  His Fig. 3 ordering, read as a
 * decision table.  [f] is deliberately unreachable: it is the one realization
 * he attributes to bilingualism rather than to Japanese.
 */
int ja_phi_mode(int v, int nxt_c, int devoiced)
{
    if (v != JA_V_U)
        return JA_PHI_P;                /* loanword /fa fi fe fo/ */
    if (nxt_c == JA_C_N || nxt_c == JA_C_NY || nxt_c == JA_C_M ||
        nxt_c == JA_C_MY || nxt_c == JA_C_MN || nxt_c == JA_C_NONE) {
        /* Saito via Ruddell 2.6: "[P] always appears when followed by a
         * voiceless vowel".  It is a secondary citation, and taken as
         * "devoicing forces [P]" it erases the velar rule below -- /fuku/ is
         * exactly a devoicing environment and Ruddell's own modal answer there
         * is [P/h].  What he writes is that [P] AND [P/h] occurred more often
         * where devoicing did, so devoicing argues against [h] and nothing
         * else.  Applied that way it blocks [h] alone. */
        return devoiced ? JA_PHI_P : JA_PHI_H;      /* fune, fuan, fuufu */
    }
    if (nxt_c == JA_C_K || nxt_c == JA_C_KY || nxt_c == JA_C_G ||
        nxt_c == JA_C_GY)
        return JA_PHI_PH;               /* fuku, fukadakku */
    return JA_PHI_P;                    /* futon, futarusan, fushigi */
}

/*
 * The moraic nasal takes the place of whatever follows it.  Returns the
 * pseudo-consonant whose locus it borrows, or JA_C_NONE when there is no oral
 * closure to borrow one from.
 */
int ja_moraic_n(int nc, int final_)
{
    switch (nc) {
    case JA_C_M: case JA_C_B: case JA_C_P:
    case JA_C_MY: case JA_C_BY: case JA_C_PY:
    case JA_C_V:
        /* /v/ was missing.  Both readers send the kana to /b/, so a moraic
         * nasal before it is bilabial like any other; without the row it fell
         * through to the no-closure branch. */
        return JA_C_N_M;                /* bilabial */
    case JA_C_K: case JA_C_G: case JA_C_KY: case JA_C_GY:
        return JA_C_N_K;                /* velar */
    case JA_C_T: case JA_C_D: case JA_C_N: case JA_C_R:
    case JA_C_NY: case JA_C_RY: case JA_C_TS: case JA_C_Z:
    case JA_C_J: case JA_C_S: case JA_C_SH: case JA_C_CH:
        return JA_C_N_N;                /* alveolar */
    default:
        break;
    }
    /*
     * Before a vowel or before /h/ there is no oral closure: the realisation
     * is a nasalised vowel and the formants stay where the vowel left them.
     *
     * Utterance-finally is NOT that case and was wrongly folded into it.  The
     * final allophone is uvular -- there is a real constriction -- so folded
     * in here, word-final /N/ rendered as the preceding vowel at its own F1
     * and F2, 8 dB quieter with wider bandwidths, which is not a nasal at all.
     * /obasan/ held F1 736 and F2 1228 for the whole mora, and it was reported
     * by ear as "the N sounds like a softer A", which is exactly what that is.
     */
    /*
     * No oral closure to assimilate to: before a vowel, a glide, /h/, or
     * another special mora.  The realisation is a NASALISED VOWEL, and this
     * used to answer "no place at all", after which the frame builder fell
     * back to the preceding vowel's own posture -- which is not a nasal.
     * Measured: /sa N po/ holds F1 272, a murmur, where /sa N yo/ held 736,
     * /a/'s own F1, and it was heard as "san sounds like saa".  Its
     * audibility depended entirely on the vowel before it, which is why it
     * survived so long: /i/ at 298 is already close enough to a murmur.
     */
    return final_ ? JA_C_N_Q : JA_C_N_NAS;
}

/* A shut tract radiates only its lowest resonance, heavily damped. */
void ja_voice_bar_post(const ja_post in, ja_post out)
{
    post_set(out, 200, in[1], in[2], in[3], 250, 250, 250);
}
