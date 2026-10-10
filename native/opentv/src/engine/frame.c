/*
 * Building one synthesizer frame.
 *
 * Synth_Step calls this once per frame.  It reads the 22 parameter tracks at
 * the current position, applies the per-voice percentage adjustments, turns
 * the pitch into the two half-period counts (with a jitter LFSR), looks the
 * formant frequencies and bandwidths up in the sample-rate dependent
 * coefficient tables, and leaves the 40 int16s of Engine.filt_coef ready for
 * Synth_Generate.
 */
#include "engine.h"

/* Per-voice parameter adjustments: 15 int32s per voice. */
/* @0x100b5068 */ extern const int32_t g_voice_adjust[];
/* Jitter, shimmer and gain tables. */
/* @0x100b5648 */ extern const int32_t g_tab_5648[];
/* @0x100b56c8 */ extern const int32_t g_tab_56c8[];
/* @0x100b5688 */ extern const int32_t g_tab_5688[];
/* @0x100b57c8 */ extern const int32_t g_tab_57c8[];
/* @0x100b5848 */ extern const int32_t g_tab_5848[];
/* @0x100b5a78 */ extern const int32_t g_tab_5a78[];
/* @0x100b5af8 */ extern const int32_t g_tab_5af8[];
/* @0x100b5418 */ extern const int32_t g_tab_5418[];
/* @0x10123a48 */ extern const int32_t g_tab_123a48[];
/* @0x101239bc */ extern const int32_t g_tab_1239bc[];
/* @0x10123b70 */ extern const int32_t g_tab_123b70[];
/* @0x10123d58 */ extern const int32_t g_tab_123d58[];
/* @0x10123818 */ extern const int32_t g_tab_123818[];
/* @0x101238ac */ extern const int32_t g_tab_1238ac[];
/* Flag bits per parameter index (shared with stage 0). */
/* @0x100f83a0 */ extern const uint32_t g_s0_flag_lo[16];

/* @0x10025150 */
int32_t TV_STDCALL Synth_MulQ15(int32_t a, int32_t b)
{
    return (b * a) / 0x7fff;
}

/* @0x10025280 */
int32_t TV_STDCALL Synth_MulShr11(int32_t a, int32_t b)
{
    return (b * a) >> 11;
}

/* Returns the product at Q12 and leaves it at Q13 in *hi. */
/* @0x10025290 */
int32_t TV_STDCALL Synth_MulShr12(int32_t a, int32_t b, int32_t *hi)
{
    int32_t v = b * a;

    *hi = v >> 13;
    return v >> 12;
}

/* @0x10004750 */
uint8_t TV_THISCALL Synth_Gate(Engine *self, int32_t op)
{
    if (op == 3) {
        if (self->synth_19ad != 0)
            return 0;
        if (self->synth_19ae != 0)
            return 0;
        self->synth_19ae = 1;
        return 1;
    }
    if (op == 4) {
        self->synth_19ad = 1;
        self->synth_19ae = 0;
        return 1;
    }
    if (op == 5)
        return self->synth_19ad;
    if (op == 6) {
        self->synth_19ad = 0;
        return 1;
    }
    return 0;
}

/* The pitch-jitter LFSR: one step, returning the low six bits. */
static int32_t jitter(Engine *self)
{
    int32_t v = self->e_206c;
    int32_t nv = ((((v * 2) ^ v) & 2) << 6) + (v >> 1);

    self->e_206c = nv;
    return nv & 0x3f;
}

/*
 * OpenTV: one cycle of a cosine in sixteen steps, scaled to +-256, for the
 * waver a sung note carries.  Sixteen frames is 160 ms, so advancing one step
 * a frame is 6.25 Hz.
 */
static const int16_t g_sing_cos[16] = {
     256,  237,  181,   98,    0,  -98, -181, -237,
    -256, -237, -181,  -98,    0,   98,  181,  237
};

int tv_ext_clarity = 0;

/* Widen a formant's bandwidth at high speech rates.
 *
 * The byte offset into tables 6 and 7 is the bandwidth in hertz -- they hold
 * exp(-4*pi*i/Fs) and its square for i = offset/4 -- so widening is a plain
 * scale of the offset.  A wider bandwidth is a shorter impulse response, so
 * the resonator settles inside the phoneme instead of ringing on into the
 * next one, which is what fast speech otherwise sounds like.
 *
 * From TGSpeechBox by Tamas Geczy (MIT), which does the same thing to its
 * cascade bandwidths above a speed threshold; see NOTICE.
 */
static int32_t bw_widen(const Engine *self, int32_t off)
{
    int32_t row = self->rate_index, ramp, scale;

    if (!tv_ext_clarity || row <= TV_BW_ROW_START)
        return off;
    ramp = (row - TV_BW_ROW_START) * 256 / (TV_BW_ROW_FULL - TV_BW_ROW_START);
    if (ramp > 256)
        ramp = 256;
    scale = 256 + ramp * (TV_BW_MAX_Q8 - 256) / 256;
    off = (off * scale) >> 8;
    /* Table 6 is 180 entries of four bytes; the last one is offset 716. */
    if (off > 716)
        off = 716;
    return off & ~3;
}

/* The coefficient tables are indexed by byte offset. */
#ifdef TV_DIAG
/* The widest byte offset each resonator table is read at, so a voice that
 * walks off the end of one can be caught saying so.  Tables 6 and 7 are 180
 * int32 (720 bytes) and table 8 is 700 (2800); see docs/VOICES.md. */
long tv_diag_syn[10];
static int32_t syn_at(const Engine *self, int i, int32_t off);
#define SYN(i, off) syn_at(self, (i), (int32_t)(off))
#else
#define SYN(i, off) (*(const int32_t *)((const uint8_t *)self->syn_tab[i] + (off)))
#endif

/* @0x10002a40 */
#ifdef TV_DIAG
/* The five Q13 pole coefficients scaled to Q15 by multiplying by four.  A
 * radius near one puts the product at 32768, which does not fit, and a wrapped
 * coefficient is a filter that no longer decays.  Slot order follows
 * filt_coef: 5, 7, 9, 11, 15. */
long tv_diag_q15[5];
long tv_diag_q15_over[5];

static int32_t q15(int slot, int32_t v)
{
    long a = v < 0 ? -(long)v : (long)v;

    if (a > tv_diag_q15[slot])
        tv_diag_q15[slot] = a;
    if (v > 32767 || v < -32768)
        tv_diag_q15_over[slot]++;
    return v;
}
#define Q15(slot, v) q15((slot), (int32_t)(v))
#else
#define Q15(slot, v) (v)
#endif

#ifdef TV_DIAG
static int32_t syn_at(const Engine *self, int i, int32_t off)
{
    if (off > tv_diag_syn[i])
        tv_diag_syn[i] = off;
    return *(const int32_t *)((const uint8_t *)self->syn_tab[i] + off);
}
#endif

void TV_THISCALL Synth_Frame(Engine *self)
{
    int32_t p[22];
    const int32_t *adj;
    int32_t pos, i, k, v, sr2, jit, t10, t14 = 0, t1c, t18, t20, t24;
    int32_t c23, c25, c27, lo, hi, d1, d2, d3, e1, e2, e3;
    int32_t flag;
    int16_t w;

    if (self->s3_1fe0 + self->trk_04 >= self->trk_0c)
        return;
    if (!Synth_Gate(self, 3))
        return;

    /* The three amplitude tracks must not dip below the floor. */
    for (i = 0; i < 3; i++) {
        uint8_t *q = self->trk_buf[13 + i] + (self->trk_04 & 0xff);

        if ((int32_t)*q * 2 < 0x2d)
            *q = 0x16;
    }

    pos = self->trk_04 & 0xff;
    k = pos >> 3;
    v = pos & 7;
    if ((uint32_t)self->trk_14[k] & g_s0_flag_lo[v]) {
        self->trk_34++;
        self->trk_14[k] = (uint8_t)(self->trk_14[k] & (uint8_t)~(uint8_t)g_s0_flag_lo[v]);
    }
    flag = self->s3_1fbd[k] & (uint8_t)g_s0_flag_lo[v];
    self->s3_1fbd[k] = (uint8_t)(self->s3_1fbd[k] & (uint8_t)~(uint8_t)g_s0_flag_lo[v]);

    for (i = 0; i < 22; i++)
        p[i] = self->trk_buf[i][pos];

    /* ---- per-voice adjustments -------------------------------------- */
    t1c = (p[21] & 0xf0) >> 4;
    adj = tv_v_adjust(t1c);
    p[9] += (adj[0] * p[9]) / 100;
    p[13] += (adj[1] * p[13]) / 100;
    p[10] += (adj[2] * p[10]) / 100;
    if (p[10] > 0xff)
        p[10] = 0xff;
    p[14] += (adj[3] * p[14]) / 100;
    p[11] += (adj[4] * p[11]) / 100;
    p[15] += (adj[5] * p[15]) / 100;
    p[12] += (adj[6] * p[12]) / 100;
    v = p[11] + 0x14;
    if (v > p[12])
        p[12] = v;
    t24 = adj[8];
    v = (p[12] + 0x19) << 4;
    if (v > t24)
        t24 = v;
    p[18] = adj[13];
    p[19] = adj[14];
    p[2] += adj[12];
    if (p[2] > 0x6b)
        p[2] = 0x6b;

    /*
     * OpenTV: breathiness, as a floor rather than an offset.  Only while the
     * voice is actually sounding -- p[0] is the voicing amplitude -- so the
     * silence test below still sees a silent frame, and so an /h/ that already
     * asks for more than this keeps its own level.  A stock voice asks for 0
     * and nothing here runs.
     */
    {
        int32_t asp = tv_v_aspir(self->cur_voice);

        if (asp != 0 && p[0] != 0 && p[2] < asp)
            p[2] = asp;
    }

    /*
     * OpenTV: a sung note held across a join does not widen its bandwidths.
     *
     * A note longer than stage 3 will give one phoneme is sung as that phoneme
     * repeated, and the engine articulates across every pair of phonemes --
     * which for a repeat is wrong, because a repeat is not a boundary.  What it
     * does is widen the bandwidths: measured frame by frame through an A-to-A
     * join, B2 walks 60, 71, 74 ... 125 over the eighteen frames before it and
     * B3 walks 100 to 200, then both drop back.  A wide bandwidth is a shallow
     * resonance, so the level follows them down **13 dB for about 150 ms** and
     * a sustained note pulses once a chunk.
     *
     * Stage 3 is the wrong place to stop it -- the travel is drawn by the rule
     * passes and not from the lead, start and target that Stage3_Write lays
     * down, so flattening all 22 of those changes nothing.  Here is the last
     * place the parameters pass before they become filter coefficients, so here
     * the three bandwidths are simply not allowed above what the note began
     * with.  Narrower is left alone: the vowel may still close up, it may just
     * not blow open at a seam that is not really there.
     *
     * Only a tied phoneme is held.  A note made of different phonemes -- which
     * is most of a song -- articulates between them as it always did.
     */
    /*
     * OpenTV: a sung note held across a join keeps its vowel.
     *
     * A note longer than stage 3 will give one phoneme is sung as that phoneme
     * repeated, and the engine articulates across every pair of phonemes --
     * which for a repeat is wrong, because a repeat is not a boundary.  Left
     * alone it drops the level **13 dB for about 150 ms** at each join, so a
     * sustained note pulses once a chunk.
     *
     * What carries that drop is the **formant frequencies**, which was not the
     * obvious answer.  The bandwidths move at the same time and look like the
     * culprit -- B2 walks 60 to 125 across a join, B3 100 to 200, and a wide
     * bandwidth is a shallow resonance -- but holding them changes almost
     * nothing.  Freezing one group of parameters at a time across the join
     * settles it:
     *
     *      nothing held                 16.5 dB of level range
     *      p0-p2    amplitudes          13.7
     *      p3-p8                        16.5
     *      **p9-p12  formants            4.3**
     *      p13-p15  bandwidths          13.5
     *      p16-p21                      16.5
     *      everything                    2.5
     *
     * The engine walks the formants toward a boundary position -- F1 from 105
     * down to 74 -- and that moves the resonances off the harmonics they were
     * reinforcing.  So the resonator shape is held: the four formants and the
     * three bandwidths together, which measures 3.3 dB against a floor of 2.5.
     *
     * Everything else is left alone, which is the point.  The amplitudes, the
     * aspiration and the glottal parameters go on coming from real frames and
     * the pitch goes on wavering, so a long note is a sustained vowel rather
     * than a frozen one; `ESC[<n>g`, which repeated a single frame and was what
     * "robotic" meant, is no longer used by a score at all.
     *
     * Only a tied phoneme is held.  A note made of several different phonemes,
     * which is most of a song, articulates between them as it always did.
     */
    /* ---- silence detection ------------------------------------------ */
    if (p[0] == 0 && p[1] == 0 && p[2] == 0) {
        if (self->e_2074 == 0)
            self->e_2074 = 1;
        self->e_2070++;
    } else {
        if (self->e_2074 == 1)
            self->e_2074 = 0;
        self->e_2070 = 0;
    }

    /* ---- the two half periods --------------------------------------- */
    if (p[0] == 0)
        p[17] = 0;
    p[17] <<= 1;
    if (p[17] == 0) {
        self->filt_coef[0] = 0;
        self->filt_coef[30] = 0;
        self->filt_coef[31] = 0;
        self->filt_coef[34] = 0;
        self->filt_coef[35] = 0;
    } else {
        if (p[17] == 0x45 || p[17] == 0x4a || p[17] == 0x4f ||
            p[17] == 0x54 || p[17] == 0x5a)
            p[17]++;

        jit = jitter(self);
        t10 = Synth_MulQ15(p[17], g_tab_5648[adj[10]]);
        v = Synth_MulQ15(t10, g_tab_56c8[jit]);
        self->filt_coef[30] = (int16_t)((int16_t)p[17] + v * 8);

        jit = jitter(self);
        v = Synth_MulQ15(t10, g_tab_56c8[jit]);
        self->filt_coef[31] = (int16_t)((int16_t)p[17] + v * 8);

        sr2 = (int32_t)self->sample_rate * 2;
        self->filt_coef[30] = (int16_t)((sr2 / (int32_t)self->filt_coef[30]) / 2);
        w = self->filt_coef[30];
        self->filt_coef[31] = (int16_t)((sr2 / (int32_t)self->filt_coef[31]) / 2);

        /*
         * OpenTV: a sung note's pitch -- glided into, wavering, and then put
         * on the sample grid.
         *
         * The shape is DECtalk's, because that is plainly where the product
         * that drove this engine took its score syntax from, and it is what a
         * sung note is missing otherwise.  A note is reached over 100 ms
         * rather than stepped to, and it wavers at 6.25 Hz by plus and minus
         * 2.05 Hz.  A frequency written out in hertz instead of a note is
         * what a glide is written with, so it travels over the whole phoneme
         * and does not waver -- DECtalk sets its vibrato switch for a note
         * from the table and clears it for a straight line, and this is the
         * same distinction.
         *
         * The period itself is a whole number of samples and the same in
         * both slots, so nothing alternates; see below.
         */
        if (tv_ext_sing && (tv_sing_f0q[4] & TV_SING_Q_MASK) > 0) {
            int32_t want = tv_sing_f0q[4] & TV_SING_Q_MASK;
            int32_t is_note = (tv_sing_f0q[4] & TV_SING_Q_HZ) == 0;
            int32_t q, h2;

            /* a target it has not heard yet starts a new glide */
            if (want != tv_sing_f0_tgt) {
                int32_t frames, glide;

                tv_sing_f0_tgt = want;
                if (tv_sing_f0_fx <= 0)
                    tv_sing_f0_fx = want << 8;      /* the first note of a score */
                /*
                 * A note takes 100 ms.  A frequency takes the phoneme it was
                 * written on, which is what makes a glide a glide; the
                 * duration escape has it in hundredths and a hundredth is a
                 * frame.
                 */
                glide = (tv_sing_glide_ms + 5) / 10;
                if (glide < 1)
                    glide = 1;
                frames = is_note ? glide
                                 : (tv_sing_dur[4] > 0 ? tv_sing_dur[4]
                                                       : glide);
                if (frames < 1)
                    frames = 1;
                tv_sing_f0_step = ((want << 8) - tv_sing_f0_fx) / frames;
            }

            /* travel, without overshooting */
            if (tv_sing_f0_step != 0) {
                tv_sing_f0_fx += tv_sing_f0_step;
                if ((tv_sing_f0_step > 0 &&
                     tv_sing_f0_fx >= (tv_sing_f0_tgt << 8)) ||
                    (tv_sing_f0_step < 0 &&
                     tv_sing_f0_fx <= (tv_sing_f0_tgt << 8))) {
                    tv_sing_f0_fx = tv_sing_f0_tgt << 8;
                    tv_sing_f0_step = 0;
                }
            }

            q = tv_sing_f0_fx;
            if (is_note && tv_sing_vib_depth > 0) {
                /* a quarter of a hertz is four, and the table is +-256 */
                int32_t depth = (tv_sing_vib_depth * 1024) / 100;

                tv_sing_vib_ph = (tv_sing_vib_ph +
                                  (tv_sing_vib_rate * 65536) / 10000) & 0xffff;
                q += (depth * (int32_t)g_sing_cos[(tv_sing_vib_ph >> 12) & 15])
                     >> 8;
            }
            q >>= 8;
            if (q < 1)
                q = 1;

            /*
             * q is quarter-hertz, so the period is 4*sr/q samples, rounded to
             * a whole one.  It is kept in half samples because generate.c
             * counts in them, but it is always an even number of them: both
             * slots get the same period and the oscillator does not alternate.
             *
             * It used to.  Coefficients 30 and 31 are swapped at every period
             * boundary, so setting them a sample apart puts the *average*
             * half a sample from either, and that bought about three cents of
             * tuning accuracy -- 4.4 against 7.1 mean over the scale.  It also
             * meant the oscillator ran 56 samples, then 57, then 56, for the
             * whole length of a note, and a sample is a wide interval: 31
             * cents at G3.  The spectrum says the artefact is 57 dB down,
             * which is why it survived three rounds of measurement; the ear
             * says it is a warble, and the ear is right.  Dither is dither
             * however it is dressed up, and a note that is a few cents flat
             * and steady beats one that is exactly right on average.
             */
            h2 = 2 * (((int32_t)self->sample_rate * 4 + q / 2) / q);

            /*
             * And do not let the grid turn a small waver into a large one.
             *
             * The period is a whole number of samples, so the step between
             * neighbouring pitches widens as the scale climbs: around 15 cents
             * at the bottom of the range, 31 at G3 and **over 80 at C5**,
             * where the period is only 21 samples.  A waver of 2.05 Hz is 38
             * cents at the bottom and 7 at the top, so before long it is
             * smaller than one step, and quantising it does not make it small
             * -- it
             * makes it snap between neighbouring steps, slowly and
             * erratically.  Measured before this guard, C5 wavered **74 cents
             * at 2 Hz** where 7 at 6.25 was asked for, which is a wobble
             * rather than a waver.  DECtalk worried about the same thing from
             * the other end: a comment in its vocal tract model keeps extra
             * fractional bits to "preserve vibrato at high notes".
             *
             * So a period is only taken if the pitch it really gives is
             * inside the waver that was asked for, with half as much again
             * for rounding.  Where it is not, the note is sung without one.
             * The waver thins out towards the top of the scale, which is the
             * engine's grid showing through and is the honest answer to it.
             */
            if (h2 >= 4 && tv_sing_vib_depth > 0 && is_note) {
                int32_t base = tv_sing_f0_fx >> 8;
                int32_t got = ((int32_t)self->sample_rate * 8 + h2 / 2) / h2;
                int32_t off = got - base;
                int32_t room = (tv_sing_vib_depth * 4 * 3) / (100 * 2);

                if (off < 0)
                    off = -off;
                if (off > room) {
                    if (base < 1)
                        base = 1;
                    h2 = ((int32_t)self->sample_rate * 8 + base / 2) / base;
                }
            }
            if (h2 >= 4) {
                tv_sing_per = h2;
                self->filt_coef[30] = (int16_t)(h2 / 2);
                self->filt_coef[31] = (int16_t)(h2 - h2 / 2);
            } else {
                tv_sing_per = 0;
            }
        } else {
            tv_sing_per = 0;
        }

        self->filt_coef[0] = (int16_t)p[18];
        v = Synth_MulQ15((int32_t)w, p[19] << 11);
        v = Synth_MulQ15(v, g_tab_57c8[p[18]]);
        if (v > 0x8b)
            v = 0x8b;
        self->filt_coef[1] = (int16_t)g_tab_5848[v];

        self->filt_coef[34] = (int16_t)g_tab_123a48[p[0]];
        v = Synth_MulQ15((int32_t)self->filt_coef[34], g_tab_5a78[p[18]]);
        self->filt_coef[34] = (int16_t)v;
        v = Synth_MulQ15((int32_t)self->filt_coef[34], g_tab_5af8[p[19]]);
        v >>= 3;
        /* OpenTV: a voice of our own can ask for a quieter source, which is
         * what keeps its filter states inside 16 bits.  See TvVoiceDef. */
        v = tv_v_gain(self->cur_voice, v);
        self->filt_coef[34] = (int16_t)v;
        w = (int16_t)((int16_t)(self->filt_coef[30] >> 2) * (int16_t)v);
        self->filt_coef[34] = w;
        self->filt_coef[35] = w;

        v = Synth_MulQ15((int32_t)w, g_tab_5688[adj[11]]);
        v = Synth_MulQ15(v, g_tab_56c8[jit]);
        self->filt_coef[34] = (int16_t)(self->filt_coef[34] + (int16_t)(v << 3));

        jit = jitter(self);
        v = Synth_MulQ15((int32_t)self->filt_coef[35], g_tab_5688[adj[11]]);
        v = Synth_MulQ15(v, g_tab_56c8[jit]);
        self->filt_coef[35] = (int16_t)(self->filt_coef[35] + (int16_t)(v << 3));
    }

    if (self->mute == 1)
        self->filt_coef[0] = (int16_t)(self->filt_coef[0] | 0x80);
    if (self->e_2070 > 2) {
        self->filt_coef[0] = (int16_t)(self->filt_coef[0] | 0x80);
        self->e_2070 = 2;
    }

    /* ---- the nasal branch and the first formant ---------------------- */
    /* OpenTV: the aspiration source follows the voice's gain too, or a
     * quieter voice is only half quieter and the noise still drives the
     * resonators as hard as ever. */
    self->filt_coef[16] =
        (int16_t)tv_v_gain(self->cur_voice, g_tab_1239bc[p[2]]);
    self->filt_coef[38] = (int16_t)*(const int32_t *)self->syn_tab[0];
    self->filt_coef[39] = (int16_t)*(const int32_t *)self->syn_tab[1];
    self->filt_coef[37] = (int16_t)*(const int32_t *)self->syn_tab[2];

    v = (int32_t)(int16_t)((int32_t)self->sample_rate / 2);
    if (v < t24) {
        self->filt_coef[21] = 0x2000;
        self->filt_coef[4] = 0;
        self->filt_coef[5] = 0;
    } else {
        int32_t o9 = bw_widen(self, adj[9] & ~3);

        t20 = 0;
        v = SYN(7, o9);
        lo = Synth_MulShr12(SYN(6, o9), SYN(8, ((t24 & ~6) >> 1)), &t14);
        self->filt_coef[4] = (int16_t)lo;
        self->filt_coef[5] = (int16_t)Q15(0, v * 4);
        self->filt_coef[21] = (int16_t)((int16_t)((int16_t)v - (int16_t)t14) + 0x2000);
    }

    v = (int32_t)(int16_t)((int32_t)self->sample_rate / 2);
    if ((p[12] << 4) >= v)
        p[12] = (v - 0xa) >> 4;


    /* ---- F1: from adj[7] and p[12] ---------------------------------- */
    {
        int32_t o7 = bw_widen(self, adj[7] & ~3);

        c23 = SYN(7, o7);
        lo = Synth_MulShr12(SYN(6, o7), SYN(8, p[12] * 8), &t14);
        self->filt_coef[6] = (int16_t)lo;
        self->filt_coef[7] = (int16_t)Q15(1, c23 * 4);
        c23 = c23 - t14 + 0x2000;
        self->filt_coef[23] = (int16_t)c23;
    }

    /* ---- F2: from p[15] and p[11] ----------------------------------- */
    {
        int32_t off = bw_widen(self, (p[15] & ~1) * 2);
        int32_t bwoff = flag ? (((p[11] * 10) & ~6) >> 1) : (p[11] * 8);

        c25 = SYN(7, off);
        lo = Synth_MulShr12(SYN(6, off), SYN(8, bwoff), &t14);
        self->filt_coef[8] = (int16_t)lo;
        self->filt_coef[9] = (int16_t)Q15(2, c25 * 4);
        c25 = c25 - t14 + 0x2000;
        self->filt_coef[25] = (int16_t)c25;
    }

    /* ---- F3: from p[14] and p[10] ----------------------------------- */
    {
        int32_t off = bw_widen(self, (p[14] & ~1) * 2);
        int32_t bwoff = flag ? (((p[10] * 10) & ~6) >> 1) : (p[10] * 4 + 0xfc);

        c27 = SYN(7, off);
        lo = Synth_MulShr12(SYN(6, off), SYN(8, bwoff), &t14);
        self->filt_coef[10] = (int16_t)lo;
        self->filt_coef[11] = (int16_t)Q15(3, c27 * 4);
        c27 = c27 - t14 + 0x2000;
        self->filt_coef[27] = (int16_t)c27;
    }

    /* ---- F4: from p[13] and p[9] ------------------------------------ */
    {
        int32_t off = bw_widen(self, (p[13] & ~1) * 2);
        int32_t bwoff = flag ? (((p[9] * 10) & ~6) >> 1) : ((p[9] & ~1) * 2);
        int32_t c36;

        c36 = SYN(7, off);
        lo = Synth_MulShr12(SYN(6, off), SYN(8, bwoff), &t14);
        self->filt_coef[14] = (int16_t)lo;
        self->filt_coef[15] = (int16_t)Q15(4, c36 * 4);
        c36 = c36 - t14 + 0x2000;
        if (c36 > 0x7ff)
            c36 = 0x7ff;
        self->filt_coef[36] = (int16_t)((int16_t)c36 << 4);
    }

    self->filt_coef[17] = (int16_t)g_tab_5418[t1c];
    lo = Synth_MulShr12(self->syn_2038,
                        SYN(8, ((((p[16] & 0xfe) << 2) + 0xc0) & ~6) >> 1), &t14);
    self->filt_coef[32] = (int16_t)lo;
    self->filt_coef[29] =
        (int16_t)((int16_t)(*(const int32_t *)((const uint8_t *)self->syn_tab[9] +
                                               (p[16] & ~3))) * 2);

    /* ---- the amplitudes --------------------------------------------- */
    if (p[1] <= 0) {
        self->filt_coef[20] = 0;
        self->filt_coef[22] = 0;
        self->filt_coef[24] = 0;
        self->filt_coef[26] = 0;
        v = 0;
    } else {
        d1 = flag ? ((p[9] * 10) >> 5) : (p[9] >> 3);
        d2 = flag ? ((p[10] * 10) >> 5) : ((p[10] >> 2) + 0x10);
        e1 = g_tab_123b70[d1] * 2;
        t10 = e1 - g_tab_123b70[d2] - 0x2c;
        t18 = e1 + g_tab_123b70[d2] * 2 - 0xeb;
        d3 = flag ? ((p[11] * 10) >> 5) : (p[11] >> 1);
        e3 = (p[12] >> 1) - d3;
        e2 = d3 - d2;
        e1 = d2 - d1;
        if (e1 <= 2)
            e1 = 0;
        e2 -= 2;
        if (e2 <= 2)
            e2 = 0;
        e3 -= 5;
        if (e3 <= 2)
            e3 = 0;
        lo = g_tab_123d58[e2];
        hi = g_tab_123d58[e3];
        t20 = g_tab_123d58[e1];

        v = t10 + t20 * 2 + 0x1e + lo + p[1] + p[3];
        if (v < 0)
            v = 0;
        k = g_tab_123818[v];
        if (k > 0xa0)
            k = 0xa0;
        self->filt_coef[26] = (int16_t)Synth_MulShr11(k, c27);

        v = t18 + lo * 2 + 0x16 + hi + p[1] + p[4];
        if (v < 0)
            v = 0;
        if (v > 0xa0)
            v = 0xa0;
        self->filt_coef[24] = (int16_t)(-(int16_t)Synth_MulShr11(g_tab_123818[v], c25));

        v = t18 + hi * 2 + p[5] + p[1] + 0x11;
        if (v < 0)
            v = 0;
        if (v > 0xa0)
            v = 0xa0;
        self->filt_coef[22] = (int16_t)Synth_MulShr11(g_tab_123818[v], c23);
        if (self->sample_rate == 0x1f40)
            self->filt_coef[22] = (int16_t)(self->filt_coef[22] >> 1);

        v = t18 + p[1] + p[6] + 0x10;
        if (v < 0)
            v = 0;
        if (v > 0x9a)
            v = 0x9a;
        self->filt_coef[20] =
            (int16_t)(-(int16_t)Synth_MulShr11(g_tab_123818[v],
                                               (int32_t)self->filt_coef[21]));

        v = (int16_t)((int16_t)g_tab_1238ac[p[8] + p[1]] << 2);
    }
    self->filt_coef[28] = (int16_t)v;

    self->trk_04++;
    self->synth_19ad = 1;
    self->synth_19ae = 0;
}
