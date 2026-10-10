/* Japanese source calibration for unmodified stock engine voice blocks.
 *
 * The reference is Peter's Japanese noise level at the SAME output rate,
 * not English phoneme timing or a claim about native Japanese loudness.
 * tools/ja_kenta_calibrate.py measures steady, isolated sources and searches
 * integer track offsets; the amplitude tables saturate, so offsets cannot
 * be inferred by treating a track unit as a decibel.
 *
 * Apply after the frontend's sample-rate compensation. Track 0 (voicing),
 * track 8 (bypass noise), postures, bandwidths and timing stay intact. The
 * vowel row covers aspiration in vowel-shaped postures; velar releases have
 * their own rows. These are an initial engineering calibration, to be judged
 * in listening as well as in the running-speech overflow checks.
 */
#include "ja.h"

static void kenta_noise(uint8_t *t, int voice, int sr, int c, int v)
{
    /* Rows: 11025, 16000. Columns: a i u e o. */
    static const unsigned char vowel[2][JA_NV] = {
        {12,22,11,15,9}, {10,17,9,12,10}
    };
    static const unsigned char velar[2][JA_NV] = {
        {11,20,10,19,9}, {10,15,9,16,9}
    };
    static const unsigned char palatal[2][JA_NV] = {
        {13,20,21,20,15}, {10,15,16,15,11}
    };
    static const unsigned char labial[2][JA_NV] = {
        {9,13,10,9,9}, {8,9,8,8,8}
    };
    static const unsigned char palatal_labial[2][JA_NV] = {
        {10,13,19,13,9}, {8,9,12,9,8}
    };
    /* Track 5 / track 6; keyed by the consonant owning the noise. */
    static const unsigned char parallel[2][JA_NC][2] = {
        { [JA_C_S]={0,16}, [JA_C_SH]={16,7}, [JA_C_Z]={0,17},
          [JA_C_J]={0,7}, [JA_C_CH]={16,9}, [JA_C_TS]={13,14},
          [JA_C_T]={0,17}, [JA_C_D]={0,14} },
        { [JA_C_S]={0,17}, [JA_C_SH]={32,0}, [JA_C_Z]={0,17},
          [JA_C_J]={0,0}, [JA_C_CH]={32,0}, [JA_C_TS]={35,23},
          [JA_C_T]={0,9}, [JA_C_D]={0,8} }
    };
    int row, k, trim;
    if (voice != 2 || (sr != 11025 && sr != 16000)) return;
    row = sr == 16000;
    if (v >= 0 && v < JA_NV && t[2]) {
        trim = c == JA_C_K ? velar[row][v]
             : c == JA_C_KY ? palatal[row][v]
             : c == JA_C_P ? labial[row][v]
             : c == JA_C_PY ? palatal_labial[row][v] : vowel[row][v];
        t[2] = (uint8_t)(t[2] > trim ? t[2] - trim : 0);
    }
    if (c >= 0 && c < JA_NC) {
        for (k = 5; k <= 6; k++) {
            trim = parallel[row][c][k - 5];
            t[k] = (uint8_t)(t[k] > trim ? t[k] - trim : 0);
        }
    }
}

void ja_voice_noise(uint8_t *t, int voice, int sr, int c, int v)
{
    /* Profiles: Sidney, Wanda, Julia. Each has 11/16 kHz rows, containing
     * vowel-shaped aspiration, k, ky, p, py; vowels are a i u e o.
     * Reproduce with ja_kenta_calibrate.py --voice 1|8|9.
     */
    static const unsigned char aspiration[3][2][5][JA_NV] = {
        { {{18,11,17,16,17}, {16,5,17,12,17}, {15,5,4,5,13},
           {16,15,16,16,16}, {16,15,8,15,16}},
          {{19,16,17,19,18}, {17,14,17,17,17}, {16,14,14,14,16},
           {16,16,16,16,16}, {16,16,16,16,16}} },
        { {{16,14,17,17,17}, {16,15,17,13,16}, {16,15,15,15,16},
           {16,15,16,16,16}, {16,15,12,15,16}},
          {{18,18,16,19,18}, {17,21,17,17,17}, {17,21,21,21,18},
           {16,16,16,16,16}, {16,16,17,16,16}} },
        { {{16,22,15,18,14}, {15,24,15,20,14}, {17,24,26,24,19},
           {14,16,14,14,14}, {14,16,21,16,14}},
          {{14,16,14,15,14}, {14,18,14,16,14}, {14,18,20,18,15},
           {14,14,14,14,14}, {14,14,15,14,14}} }
    };
    /* s, sh, z, j, ch, ts, t, d; each pair trims tracks 5 and 6.
     * Zero is intentional when the stock voice's high resonator is already
     * quieter than the reference (or bypassed above Nyquist). No attempt is
     * made to force an unreachable RMS match by boosting or moving it.
     */
    static const unsigned char parallel[3][2][8][2] = {
        { {{0,0},{3,0},{0,0},{0,0},{0,0},{1,0},{0,0},{0,0}},
          {{0,18},{0,35},{0,18},{0,25},{0,35},{0,26},{0,12},{0,11}} },
        { {{0,0},{22,0},{0,0},{0,0},{22,0},{19,0},{0,0},{0,0}},
          {{0,21},{0,35},{0,21},{0,25},{0,35},{39,29},{0,15},{0,15}} },
        { {{0,0},{20,10},{0,0},{0,10},{0,12},{13,0},{0,0},{0,0}},
          {{0,16},{0,0},{0,16},{0,0},{0,0},{34,22},{0,8},{0,7}} }
    };
    int profile, row, kind, pc = -1, k, trim;
    if (voice == 2) { kenta_noise(t, voice, sr, c, v); return; }
    profile = voice == 1 ? 0 : voice == 8 ? 1 : voice == 9 ? 2 : -1;
    if (profile < 0 || (sr != 11025 && sr != 16000)) return;
    row = sr == 16000;
    kind = c == JA_C_K ? 1 : c == JA_C_KY ? 2 :
           c == JA_C_P ? 3 : c == JA_C_PY ? 4 : 0;
    if (v >= 0 && v < JA_NV && t[2]) {
        trim = aspiration[profile][row][kind][v];
        t[2] = (uint8_t)(t[2] > trim ? t[2] - trim : 0);
    }
    switch (c) {
    case JA_C_S: pc = 0; break; case JA_C_SH: pc = 1; break;
    case JA_C_Z: pc = 2; break; case JA_C_J: pc = 3; break;
    case JA_C_CH: pc = 4; break; case JA_C_TS: pc = 5; break;
    case JA_C_T: pc = 6; break; case JA_C_D: pc = 7; break;
    }
    if (pc >= 0) for (k = 5; k <= 6; k++) {
        trim = parallel[profile][row][pc][k - 5];
        t[k] = (uint8_t)(t[k] > trim ? t[k] - trim : 0);
    }
}
