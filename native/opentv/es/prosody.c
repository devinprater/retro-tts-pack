/*
 * The parameter list: one frame's worth of synthesiser coefficients.
 *
 * Engine_Step runs this after every synthesis step.  It reads the current
 * byte of all twenty-two parameter tracks, applies the voice's percentage
 * adjustments, works out the pitch and the formants, and writes the result
 * into filt_coef, which is what Synth_Generate then runs the filter from.
 *
 * This is the function the image's one surviving trace string calls ParL --
 * "ParL[P_F0]= %d" -- which is where the name comes from and which says the
 * original called its parameter list ParL and its pitch parameter P_F0.  The
 * trace fires when the pitch comes out below 0x3c, alongside error 0x65, and
 * the pitch is then forced to 0x41.  Both calls are compiled to nothing in
 * the shipping build, but the clamp is not.
 *
 * Track indices worth knowing, from what the code does with them: 0 is the
 * amplitude the whole frame is gated on, 2 is a level clamped to 0x6b, 9
 * through 15 are the formant frequencies and bandwidths the voice table
 * scales, 17 is the pitch, and 18 through 21 pick rows out of the shaping
 * tables.
 *
 * The voice table g_voice_adj has fifteen int32 per row and the row comes
 * from the top nibble of track 21.  Seven of its entries are percentages
 * added to tracks 9 to 15; the rest set or offset other tracks outright.
 *
 * Ten of its 119 basic blocks are not reached by the corpus, and they are
 * worth naming because they are the ones written from the disassembly alone:
 *
 *   - every arm guarded by bit_was_set, which is six of the ten.  The
 *     s3_1fbd bit is never set on any frame the corpus produces, so the
 *     "(x * 5 * 2) & ~6) >> 1" table indexing in the four formants and the
 *     three amplitudes has no evidence behind it beyond the reading.
 *   - the row != 0 arm: no phoneme in the corpus puts anything in the top
 *     nibble of track 21, so every frame uses voice row 0.
 *   - three clamps: t > 0x8b, f2 <= 2, and the first amplitude's i < 0.
 *
 * Everything else is covered, and covered hard -- a single added to the
 * pitch takes the corpus from 205/205 to 12/205.
 */
#include "es_engine.h"

int tv_es_ext_floor = 0;

/* @0x10049910 */
extern const uint32_t g_bit_mask[8];
/* @0x1004c540 */
extern const int32_t g_voice_adj[];
/* @0x1004bfcc */
extern const int32_t g_par0_a[];
/* @0x1004c058 */
extern const int16_t g_par0_b[];
/* @0x1004c8f0 */
extern const int32_t g_voice_c[];
/* @0x1004cb20 */
extern const int32_t g_pitch_a[];
/* @0x1004cb60 */
extern const int32_t g_pitch_b[];
/* @0x1004cba0 */
extern const int32_t g_jitter[64];
/* @0x1004cca0 */
extern const int32_t g_par18_a[];
/* @0x1004cd20 */
extern const int32_t g_par18_b[];
/* @0x1004cf50 */
extern const int32_t g_par18_c[];
/* @0x1004cfd0 */
extern const int32_t g_par19_a[];
/* @0x1004c180 */
extern const int32_t g_form_a[];
/* @0x1004c368 */
extern const int32_t g_form_b[];
/* @0x1004be28 */
extern const int32_t g_amp[];
/* @0x1004bebc */
extern const int32_t g_amp2[];
/* @0x1004d014 */
extern const char g_fmt_parl[];

/* One step of the pitch-jitter generator, returning the low six bits.  The
 * English engine has this as a function of its own; here it is written out
 * at each of its four uses, so it is a helper rather than a hook. */
static int32_t jitter_step(Engine *self)
{
    int32_t x = self->e_206c;
    int32_t c = ((x * 2) ^ x) & 2;

    x = (c << 6) + (x >> 1);
    self->e_206c = x;
    return x & 0x3f;
}

/* @0x1000f090 */
void TV_THISCALL Prosody_Build(Engine *self)
{
    int32_t par[22];
    int32_t hi, row, jit, i;
    const int32_t *adj;
    uint8_t bit_was_set;

    if (!Tracks_Op(self, 3, -1))
        return;
    if (!Synth_Gate(self, 3))
        return;

    /* Tracks 13, 14 and 15 have a floor of 22 in the buffer itself. */
    for (i = 13; i < 16; i++) {
        uint8_t *p = &self->trk_buf[i][self->trk_04 & 0xff];
        if (*p * 2 < 0x2d)
            *p = 0x16;
    }

    /* Two bitmaps indexed by the read cursor: one counts and clears, the
     * other only clears and remembers whether it was set. */
    {
        int32_t k = self->trk_04;
        uint8_t *p = &self->trk_14[(k & 0xf8) >> 3];
        uint32_t m = g_bit_mask[k & 7];
        if ((m & *p) != 0) {
            self->trk_34++;
            *p = (uint8_t)(*p & ~(uint8_t)m);
        }
    }
    {
        int32_t k = self->trk_04;
        uint8_t m = (uint8_t)g_bit_mask[k & 7];
        uint8_t *p = &self->s3_1fbd[(k & 0xf8) >> 3];
        uint8_t c = *p;
        bit_was_set = (uint8_t)(c & m);
        *p = (uint8_t)(~m & c);
    }

    for (i = 0; i < 22; i++)
        par[i] = self->trk_buf[i][self->trk_04 & 0xff];

    row = (par[21] & 0xf0) >> 4;
    par[17] *= 2;
    adj = es_v_adjust(row);
    par[9] += adj[0] * par[9] / 100;
    par[13] += adj[1] * par[13] / 100;
    par[10] += adj[2] * par[10] / 100;
    par[14] += adj[3] * par[14] / 100;
    par[11] += adj[4] * par[11] / 100;
    par[15] += adj[5] * par[15] / 100;
    par[12] += adj[6] * par[12] / 100;
    if (par[12] < par[11] + 0x14)
        par[12] = par[11] + 0x14;
    if (row != 0) {
        par[18] = adj[13];
        par[19] = adj[14];
    }
    par[2] += adj[12];
    if (par[2] > 0x6b)
        par[2] = 0x6b;

    /*
     * OpenTV: breathiness, as a floor rather than an offset -- the same
     * reasoning as the English engine's, and for the same voice.  adj[12] is an
     * offset on a curve that is flat zero low down, so on a vowel, whose track 2
     * is 0, adding to it still asks for silence; a floor is what the parameter
     * means.  Only while the voice is actually sounding: par[0] is the voicing
     * amplitude, so the silence test below still sees a silent frame, and an
     * aspirate that already asks for more keeps its own level.  A stock voice
     * asks for 0 and nothing here runs.
     */
    {
        int32_t asp = es_v_aspir(self->voice);

        if (asp != 0 && par[0] != 0 && par[2] < asp)
            par[2] = asp;
    }

    /* Silence is counted so that the output stage can notice a long one. */
    if (par[0] == 0 && par[1] == 0 && par[2] == 0) {
        if (self->e_2074 == 0)
            self->e_2074 = 1;
        self->e_2070++;
    } else {
        if (self->e_2074 == 1)
            self->e_2074 = 0;
        self->e_2070 = 0;
    }

    if (par[0] == 0)
        par[17] = 0;
    if (par[17] == 0) {
        self->filt_coef[0] = 0;
        self->filt_coef[30] = 0;
        self->filt_coef[31] = 0;
        self->filt_coef[34] = 0;
        self->filt_coef[35] = 0;
    } else {
        int32_t a, b, t;
        int16_t f30;

        if (par[17] == 0x45 || par[17] == 0x4a || par[17] == 0x4f ||
            par[17] == 0x54 || par[17] == 0x5a)
            par[17]++;
        jit = jitter_step(self);
        /*
         * OpenTV: the low-pitch substitution.
         *
         * This engine treats a pitch track under 0x3c as an error, logs it and
         * replaces it with 0x41 -- 65, which is a low C at 65.41 Hz and is
         * audibly where the bottom of the range stops.  The 1997 English engine
         * has no such check anywhere in the same function; everything around
         * this is the same in both, down to the ++ on 0x45, 0x4a, 0x4f, 0x54
         * and 0x5a and the jitter that follows, so the substitution is the one
         * thing added rather than a difference of design.
         *
         * Nothing needs it.  par[17] is twice the node's b15, and Stage2_Adjust
         * has already clamped b15 to 0x19, so the track cannot come in under
         * 0x32 -- exactly the floor English works down to, and a period of 220
         * samples at 11 kHz, which the same division handles there.  Lifting it
         * gives Jorge his bottom octave back; he and Sidney are the lowest
         * voices of their engines and both sit at pitch 50, but only one of them
         * could reach it.
         *
         * With the extension off the substitution stands, error and trace and
         * all, which is what the corpus checks.
         */
        if (par[17] < 0x3c && !tv_es_ext_floor) {
            Engine_Error(self, 0x65);
            Engine_Trace(self, g_fmt_parl, par[17]);
            par[17] = 0x41;
        }
        a = Synth_MulQ15(par[17], g_pitch_a[adj[10]]);
        b = Synth_MulQ15(a, g_jitter[jit]);
        self->filt_coef[30] = (int16_t)((int16_t)par[17] + b * 8);
        jit = jitter_step(self);
        b = Synth_MulQ15(a, g_jitter[jit]);
        self->filt_coef[31] = (int16_t)((int16_t)par[17] + b * 8);

        self->filt_coef[30] =
            (int16_t)(2 * (int16_t)self->sample_rate / self->filt_coef[30] / 2);
        f30 = self->filt_coef[30];
        self->filt_coef[31] =
            (int16_t)(2 * (int16_t)self->sample_rate / self->filt_coef[31] / 2);
        self->filt_coef[0] = (int16_t)par[18];

        t = Synth_MulQ15(f30, par[19] << 0xb);
        t = Synth_MulQ15(t, g_par18_a[par[18]]);
        if (t > 0x8b)
            t = 0x8b;
        self->filt_coef[1] = (int16_t)g_par18_b[t];

        self->filt_coef[34] = g_par0_b[par[0] * 2];
        t = Synth_MulQ15(self->filt_coef[34], g_par18_c[par[18]]);
        self->filt_coef[34] = (int16_t)t;
        t = Synth_MulQ15((int16_t)t, g_par19_a[par[19]]);
        t >>= 3;
        self->filt_coef[34] = (int16_t)t;
        {
            int16_t m = (int16_t)(self->filt_coef[30] >> 2);
            m = (int16_t)(m * (int16_t)t);
            self->filt_coef[34] = m;
            self->filt_coef[35] = m;
            t = Synth_MulQ15(m, g_pitch_b[adj[11]]);
        }
        t = Synth_MulQ15(t, g_jitter[jit]);
        self->filt_coef[34] = (int16_t)(self->filt_coef[34] + (int16_t)(t << 3));
        jit = jitter_step(self);
        t = Synth_MulQ15(self->filt_coef[35], g_pitch_b[adj[11]]);
        t = Synth_MulQ15(t, g_jitter[jit]);
        self->filt_coef[35] = (int16_t)(self->filt_coef[35] + (int16_t)(t << 3));
    }

    if (self->mute == 1)
        *(uint8_t *)&self->filt_coef[0] |= 0x80;
    if (self->e_2070 > 2) {
        *(uint8_t *)&self->filt_coef[0] |= 0x80;
        self->e_2070 = 2;
    }

    /* OpenTV: and a custom voice's source level; a stock voice multiplies by
     * nothing, so the path is what it was. */
    self->filt_coef[16] =
        (int16_t)es_v_gain(self->voice, g_par0_a[par[2]]);
    self->filt_coef[38] = (int16_t)((const int32_t *)self->syn_tab[0])[0];
    self->filt_coef[39] = (int16_t)((const int32_t *)self->syn_tab[1])[0];
    self->filt_coef[37] = (int16_t)((const int32_t *)self->syn_tab[2])[0];
    self->filt_coef[4] = (int16_t)((const int32_t *)self->syn_tab[3])[0];
    self->filt_coef[5] = (int16_t)((const int32_t *)self->syn_tab[4])[0];
    self->filt_coef[21] = (int16_t)((const int32_t *)self->syn_tab[5])[0];

    /* Track 12 is capped at the half-rate the filter can represent. */
    {
        int32_t lim = (int16_t)((int16_t)self->sample_rate / 2);
        if (par[12] * 16 >= lim)
            par[12] = (lim - 0xa) >> 4;
    }

    /* Four formants, each one a pair from the sample-rate tables scaled by
     * the track and a bandwidth taken out of the same pair. */
    {
        const int32_t *t6 = (const int32_t *)self->syn_tab[6];
        const int32_t *t7 = (const int32_t *)self->syn_tab[7];
        const int32_t *t8 = (const int32_t *)self->syn_tab[8];
        int32_t k, bw, s1, s2, s3;

        k = adj[7] & ~3;
        bw = *(const int32_t *)((const char *)t7 + k);
        hi = 0;
        self->filt_coef[6] = (int16_t)Synth_MulShr12(
            *(const int32_t *)((const char *)t6 + k),
            *(const int32_t *)((const char *)t8 + par[12] * 8), &hi);
        self->filt_coef[7] = (int16_t)(bw << 2);
        s1 = bw - hi + 0x2000;
        self->filt_coef[23] = (int16_t)s1;

        k = (par[15] & ~1) * 2;
        bw = *(const int32_t *)((const char *)t7 + k);
        self->filt_coef[8] = (int16_t)Synth_MulShr12(
            *(const int32_t *)((const char *)t6 + k),
            bit_was_set ? *(const int32_t *)((const char *)t8 +
                                             (((par[11] * 5 * 2) & ~6) >> 1))
                        : *(const int32_t *)((const char *)t8 + par[11] * 8),
            &hi);
        self->filt_coef[9] = (int16_t)(bw << 2);
        s2 = bw - hi + 0x2000;
        self->filt_coef[25] = (int16_t)s2;

        k = (par[14] & ~1) * 2;
        bw = *(const int32_t *)((const char *)t7 + k);
        self->filt_coef[10] = (int16_t)Synth_MulShr12(
            *(const int32_t *)((const char *)t6 + k),
            bit_was_set ? *(const int32_t *)((const char *)t8 +
                                             (((par[10] * 5 * 2) & ~6) >> 1))
                        : *(const int32_t *)((const char *)t8 + par[10] * 4 + 0xfc),
            &hi);
        self->filt_coef[11] = (int16_t)(bw << 2);
        s3 = bw - hi + 0x2000;
        self->filt_coef[27] = (int16_t)s3;

        k = (par[13] & ~1) * 2;
        bw = *(const int32_t *)((const char *)t7 + k);
        self->filt_coef[14] = (int16_t)Synth_MulShr12(
            *(const int32_t *)((const char *)t6 + k),
            bit_was_set ? *(const int32_t *)((const char *)t8 +
                                             (((par[9] * 5 * 2) & ~6) >> 1))
                        : *(const int32_t *)((const char *)t8 + (par[9] & ~1) * 2),
            &hi);
        self->filt_coef[15] = (int16_t)(bw << 2);
        {
            int32_t s4 = bw - hi + 0x2000;
            if (s4 > 0x7ff)
                s4 = 0x7ff;
            self->filt_coef[36] = (int16_t)((int16_t)s4 << 4);
        }
        self->filt_coef[17] = (int16_t)es_v_voice_c(row);
        {
            const int32_t *t8b = (const int32_t *)self->syn_tab[8];
            int32_t q = ((((par[16] & 0xfe) << 2) + 0xc0) & ~6) >> 1;
            hi = 0;
            self->filt_coef[32] = (int16_t)Synth_MulShr12(
                self->syn_2038, *(const int32_t *)((const char *)t8b + q), &hi);
        }
        self->filt_coef[29] = (int16_t)(
            (int16_t)(*(const int32_t *)((const char *)self->syn_tab[9] +
                                         (par[16] & ~3))) * 2);

        if (par[1] <= 0) {
            self->filt_coef[18] = 0;
            self->filt_coef[20] = 0;
            self->filt_coef[22] = 0;
            self->filt_coef[24] = 0;
            self->filt_coef[26] = 0;
            self->filt_coef[28] = 0;
        } else {
            int32_t f1, f2, f3, d1, d2, d3, n;
            f1 = bit_was_set ? (par[9] * 5 * 2) >> 5 : par[9] >> 3;
            f2 = bit_was_set ? (par[10] * 5 * 2) >> 5 : (par[10] >> 2) + 0x10;
            d1 = g_form_a[f1] * 2;
            d2 = d1 - g_form_a[f2] - 0x2c;
            d3 = d1 + g_form_a[f2] * 2 - 0xeb;
            f3 = bit_was_set ? (par[11] * 5 * 2) >> 5 : par[11] >> 1;
            n = (par[12] >> 1) - f3;
            f3 -= f2;
            f2 -= f1;
            if (f2 <= 2)
                f2 = 0;
            f3 -= 2;
            if (f3 <= 2)
                f3 = 0;
            n -= 5;
            if (n <= 2)
                n = 0;
            f2 = g_form_b[f2];
            f3 = g_form_b[f3];
            n = g_form_b[n];

            i = f2 * 2 + 0x1e + par[1] + par[3] + d2 + f3;
            if (i < 0)
                i = 0;
            {
                int32_t g = g_amp[i];
                if (g > 0xa0)
                    g = 0xa0;
                self->filt_coef[26] = (int16_t)Synth_MulShr11(g, s3);
            }
            i = f3 * 2 + 0x16 + par[1] + par[4] + d3 + n;
            if (i < 0)
                i = 0;
            if (i > 0xa0)
                i = 0xa0;
            self->filt_coef[24] = (int16_t)(-(int16_t)Synth_MulShr11(g_amp[i], s2));
            i = n * 2 + par[5] + par[1] + d3 + 0x11;
            if (i < 0)
                i = 0;
            if (i > 0xa0)
                i = 0xa0;
            self->filt_coef[22] = (int16_t)Synth_MulShr11(g_amp[i], s1);
            i = par[1] + par[6] + d3 + 0x10;
            if (i < 0)
                i = 0;
            self->filt_coef[20] =
                (int16_t)(-(int16_t)Synth_MulShr11(g_amp[i], self->filt_coef[21]));
            i = par[1] + par[7] + d3 + 0xf;
            if (i < 0)
                i = 0;
            self->filt_coef[18] = (int16_t)Synth_MulShr11(g_amp[i], 0x6520);
            self->filt_coef[28] = (int16_t)((int16_t)g_amp2[par[8] + par[1]] << 2);
        }
    }

    Tracks_Op(self, 4, -1);
    Synth_Gate(self, 4);
}
