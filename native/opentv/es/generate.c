/*
 * The things about this function that are easy to get wrong.  Each was a
 * real difference the unit harness reported, not a hypothetical:
 *
 *   - the five parallel sections double only their first product, where the
 *     serial chain doubles the sum of the first two;
 *   - the parallel sections do not get the same noise the serial chain got:
 *     past the half-way point of a pitch period it is halved;
 *   - the aspiration tap reads o_20ae[6] in two states at once, before and
 *     after the resonator above moved it along;
 *   - the pulse-mode branch tests the high byte of o_2080, not a bit of
 *     coef[0];
 *   - o_208e takes the last sample's shaped pulse, and o_2092[10] is simply
 *     coef[33]; neither is touched inside the loop;
 *   - coef[34]/coef[35] and c30/c31 are each a pair that swaps on every
 *     pitch pulse.  Assigning coef[31] to c30 rather than swapping gets the
 *     first two periods right and every one after that wrong, which no
 *     single-call comparison can see;
 *   - the noise generator's state lives in o_2088 between calls, so it has
 *     to be read in at entry -- starting it at zero passes every unit case
 *     whose seed leaves o_2088 at zero and fails the corpus from the second
 *     frame onward;
 *   - the negations are done in a 16-bit register before being widened, so
 *     -32768 negates to itself;
 *   - the intermediates wrap, and signed overflow is undefined in C, so they
 *     are written unsigned.
 *
 * unit_es -U synth drives it directly, both as single calls over 256 seeded
 * states and as runs of 400 consecutive frames with the coefficients moving
 * under it; the latter is what catches anything carried between frames.
 * Setting o_rate_div100 to 1 in the harness's seed narrows a failure to a
 * single sample, which is how most of the above were found.
 */
/*
 * One frame of audio.
 *
 * Synth_Step calls this once per frame with the sample rate and the forty
 * coefficients Prosody_Build has just filled in.  It generates
 * o_rate_div100 samples -- ten milliseconds' worth -- and appends them to
 * out_buf.
 *
 * The shape is a formant synthesiser: an excitation, either a glottal pulse
 * read out of a table or nothing, plus noise from an LFSR, fed through a
 * chain of second-order resonators in Q15.  Each resonator is
 *
 *     y = (2 * (x * a + y1 * b) + y2 * c) >> 15
 *
 * saturated to int16 -- at 0x7fff going up and 0x8000 going down, with the
 * lower test made against -32767 rather than -32768.  The delay pairs live
 * in o_20ae, two entries per resonator, and the coefficients come partly
 * from the argument and partly from o_2092, which is reloaded from the
 * argument only when a new pitch pulse starts.  So the filter is held
 * constant across a pitch period.
 *
 * Two things about the state are easy to get wrong and are deliberate here.
 * In the chain that starts at o_20ae[12], each section stores its result
 * into its own delay slot **before** the saturation, so the state keeps the
 * truncated value while the chain carries the saturated one.  And the
 * sections from o_20ae[14] onward run in parallel rather than in series:
 * each takes the same noise input and its output is added into a running
 * sum that is saturated after every addition.
 *
 * coef[0] is a control word rather than a coefficient: bit 7 is the mute
 * flag Prosody_Build ORs in, and the low five bits index g_pulse_gain and
 * decide whether a constant of 0x4000 is used.
 */
#include "es_engine.h"


/* @0x10049b78 */
extern const int16_t g_pulse_gain[32];
/* @0x10049970 */
extern const int16_t g_pulse_shape[256];

/* The original negates these in a 16-bit register and then sign-extends,
 * so -32768 negates to itself.  Doing it in int would give 32768. */
#define NEG16(x) ((int16_t)(-(int16_t)(x)))

#define SAT(x) ((x) >= 0x7fff ? 0x7fff : ((x) <= -32767 ? -32768 : (x)))

/* y = (2*(x*a + y1*b) + y2*c) >> 15, saturated, with the delay pair moved
 * along.  Every resonator in the chain is this. */
/* The intermediates are done unsigned because the original's are 32-bit
 * wrapping arithmetic and signed overflow is undefined in C: with the
 * coefficients the engine really produces it never wraps, but a decompilation
 * that only matches on the inputs it happens to be given is not a
 * decompilation.  unit_es feeds it pseudo-random taps for exactly this. */
static int32_t reson(int32_t x, int32_t a, int32_t b, int32_t c,
                     int16_t *y1, int16_t *y2)
{
    uint32_t p = (uint32_t)((int32_t)x * a) + (uint32_t)((int32_t)*y1 * b);
    int32_t y = (int32_t)(p * 2u + (uint32_t)((int32_t)*y2 * c)) >> 15;

    *y2 = *y1;
    *y1 = (int16_t)SAT(y);
    return SAT(y);
}

/* @0x10009bf0 */
void TV_THISCALL Synth_Generate(Engine *self, uint16_t rate, const int16_t *coef)
{
    int32_t ctl = coef[0] & 0x1f;
    int16_t mutebit = (int16_t)(coef[0] & 0x80);
    int16_t q4000 = (int16_t)((ctl & 0xff) < 0xf ? 0 : 0x4000);
    int16_t gain = g_pulse_gain[ctl];
    int16_t scaled1 = (int16_t)(((int32_t)coef[1] * 20861) >> 15);
    int16_t c30 = coef[30], c31 = coef[31];
    int16_t c34 = coef[34], c35 = coef[35];
    int16_t n_left = self->o_rate_div100;
    /* The noise generator's state lives in o_2088 between calls: the
     * epilogue writes it out and this reads it back. */
    int16_t noise = (int16_t)self->o_2088;
    int16_t last_t = 0;   /* o_208e gets the last sample's shaped pulse */

    do {
        int32_t exc, acc, t, w, ser = 0;
        int16_t old6, old7;

        /* ---- the pitch counter and the pulse it starts */
        if (c30 == 0 && self->o_2076 < 0) {
            self->o_2082 = 0;
            self->o_2078 = 0;
            self->o_2076 = 0;
        } else {
            self->o_2076 = (int16_t)(self->o_2076 - 1);
            if (self->o_2076 < 0) {
                int16_t half = (int16_t)(c30 >> 1);
                self->o_2082 = -1;
                self->o_2092[0] = coef[14];
                self->o_2092[1] = (int16_t)NEG16(coef[15]);
                self->o_2092[2] = coef[36];
                self->o_2092[3] = coef[10];
                self->o_2092[4] = (int16_t)NEG16(coef[11]);
                self->o_2092[5] = coef[27];
                self->o_2092[6] = coef[8];
                self->o_2092[7] = (int16_t)NEG16(coef[9]);
                self->o_2092[8] = coef[25];
                self->o_2092[9] = coef[32];
                self->o_20a8 = coef[29];
                /* c30 and c31 are a pair that swaps on every pitch
                 * pulse, the way coef[34] and coef[35] do below, so the
                 * period alternates between the two.  Assigning coef[31]
                 * here instead alternates once and then sticks. */
                self->o_2076 = c30;
                {
                    int16_t sw = c30;
                    c30 = c31;
                    c31 = sw;
                }
                self->o_2078 = half;
            } else {
                self->o_2082 = 1;
                self->o_2078 = (int16_t)(self->o_2078 - 1);
            }
        }

        /* ---- the excitation for this sample */
        if (self->o_2082 < 0) {
            self->o_20e0 = 0;
            self->o_207a = 0;
            self->o_2080 = q4000;
            self->o_20e4 = (int32_t)c34;
            {
                int16_t sw = c34;
                c34 = c35;
                c35 = sw;
            }
            self->o_207e = scaled1;
            self->o_20dc = gain;
            exc = 0;
        } else if (self->o_2082 == 0 || (self->o_207a & 1) != 0) {
            exc = 0;
        } else {
            int32_t ph = self->o_207e + self->o_20e0;
            self->o_20e0 = ph;
            if (ph > 0xffff) {
                self->o_20e0 = ph & 0xffff;
                if ((self->o_207a & 2) != 0) {
                    self->o_207a = 3;
                    exc = 0;
                    goto have_exc;
                }
                ph &= 0xffff;
                self->o_207a = 2;
            } else if ((self->o_207a & 2) == 0) {
                int32_t lo = ph & 0xff, hi = (ph >> 8) & 0xff;
                int32_t v = g_pulse_shape[hi];
                int32_t k = 0x80 - hi;
                if (k < 0)
                    k = -k;
                v -= (int16_t)((g_pulse_shape[k] * lo) >> 15);
                t = self->o_20e4 >> 1;
                exc = (int16_t)(t - (int16_t)((v * t) >> 15));
                goto have_exc;
            }
            {
                int32_t lo = ph & 0xff, hi = (ph >> 8) & 0xff;
                int32_t v = g_pulse_shape[hi];
                int32_t k = 0x80 - hi;
                if (k < 0)
                    k = -k;
                v -= (int16_t)((g_pulse_shape[k] * lo) >> 15);
                w = v * self->o_20dc;
                t = (w - (self->o_20dc << 15)) >> 15;
                /* the original tests the high byte of o_2080, which is
                 * where q4000 was parked when the pulse started */
                if ((self->o_2080 & 0x4000) != 0) {
                    int32_t u = (t << 16) + (w & 0x7fff) * 2 + 0x7fffff;
                    if (u < 0) {
                        self->o_207a = 3;
                        exc = 0;
                        goto have_exc;
                    }
                    exc = (self->o_20e4 * ((u & 0xffff00) >> 8)) >> 15;
                } else {
                    t += 0x7fff;
                    if ((t << 16) < 0) {
                        self->o_207a = 3;
                        exc = 0;
                        goto have_exc;
                    }
                    exc = (self->o_20e4 * (int16_t)t) >> 15;
                }
            }
        }
have_exc:
        /* ---- differentiate, add the noise */
        t = ((int32_t)(int16_t)exc - self->o_208c) * 8;
        self->o_208c = (int16_t)exc;
        t = SAT((t * coef[17]) >> 15);
        last_t = (int16_t)t;
        {
            int32_t x = (int16_t)(self->o_208a ^ noise);
            int32_t nx = ((x & 0xfc00) >> 7) | ((x << 6) & 0xffff);
            self->o_208a = noise;
            noise = (int16_t)(nx != 0 ? nx : 0xaaaa);
        }
        exc = (int16_t)(((noise * coef[16]) >> 15) + t);

        /* ---- the resonator chain */
        acc = reson(exc, coef[37], coef[38], NEG16(coef[39]),
                    &self->o_20ae[10], &self->o_20ae[11]);
        acc = (int32_t)coef[19] * acc;
        if (rate != 8000) {
            acc = reson(acc, 1, coef[4], NEG16(coef[5]),
                        &self->o_20aa, &self->o_20ac);
            acc = (int32_t)coef[21] * acc;
        }
        acc = reson(acc, 1, coef[6], NEG16(coef[7]),
                    &self->o_20ae[0], &self->o_20ae[1]);
        acc = reson(acc, coef[23], self->o_2092[6], self->o_2092[7],
                    &self->o_20ae[2], &self->o_20ae[3]);
        acc = reson(acc, self->o_2092[8], self->o_2092[3], self->o_2092[4],
                    &self->o_20ae[4], &self->o_20ae[5]);
        old6 = self->o_20ae[6];
        old7 = self->o_20ae[7];
        acc = reson(acc, self->o_2092[5], coef[12], NEG16(coef[13]),
                    &self->o_20ae[6], &self->o_20ae[7]);

        /* the aspiration tap, which reads the delay pair it has just moved */
        {
            /* Both delayed samples here are the ones from before the
             * resonator above moved them along, and the third term is the
             * new value -- so this reads one slot in two states. */
            int16_t a6 = old6, a7 = old7;
            int32_t s = ((int32_t)coef[33] * a7) >> 15;
            int32_t p = (((int32_t)self->o_2092[9] * a6) >> 14) & ~1;
            t = (int16_t)(s + self->o_20ae[6] + (int16_t)p);
            acc = reson(t, self->o_20a8, self->o_2092[0], self->o_2092[1],
                        &self->o_20ae[8], &self->o_20ae[9]);
            /* saturated like every other stage; it only shows up when the
             * output is muted, because then the sample is forced to zero and
             * o_207c is the one place the value survives. */
            ser = SAT((((int32_t)self->o_2092[2] * acc) & ~0x4000) >> 14);
        }

        /* the parallel branch: same input to each, outputs summed.
         *
         * The input is not quite the noise the serial chain got: past the
         * half-way point of a pitch period -- o_2078 counts down from half
         * the period and o_2076 is non-zero only while a period is running
         * -- it is halved, in 16 bits, so the sign propagates.  The engine
         * is quieting the aspiration in the closed phase of the glottal
         * cycle.  coef[28]'s term at the bottom takes the same halved value.
         */
        {
            int16_t old;
            int32_t sum;
            int16_t npar = (self->o_2076 != 0 && self->o_2078 < 0)
                           ? (int16_t)(noise >> 1) : noise;
            old = self->o_20ae[12];
            t = (int32_t)((uint32_t)((int32_t)old * coef[2]) * 2u
                 + (uint32_t)((int32_t)self->o_20ae[13] * NEG16(coef[3]))
                 + (uint32_t)((int32_t)coef[18] * npar)) >> 15;
            self->o_20ae[12] = (int16_t)t;
            self->o_20ae[13] = old;
            sum = SAT(t);

            old = self->o_20ae[14];
            t = (int32_t)((uint32_t)((int32_t)old * coef[4]) * 2u
                 + (uint32_t)((int32_t)self->o_20ae[15] * NEG16(coef[5]))
                 + (uint32_t)((int32_t)npar * coef[20])) >> 15;
            self->o_20ae[14] = (int16_t)t;
            self->o_20ae[15] = old;
            sum = SAT((int16_t)sum + t);

            old = self->o_20ae[16];
            t = (int32_t)((uint32_t)((int32_t)old * coef[6]) * 2u
                 + (uint32_t)((int32_t)self->o_20ae[17] * NEG16(coef[7]))
                 + (uint32_t)((int32_t)npar * coef[22])) >> 15;
            self->o_20ae[16] = (int16_t)t;
            self->o_20ae[17] = old;
            sum = SAT((int16_t)sum + t);

            old = self->o_20ae[18];
            t = (int32_t)((uint32_t)((int32_t)old * self->o_2092[6]) * 2u
                 + (uint32_t)((int32_t)self->o_20ae[19] * self->o_2092[7])
                 + (uint32_t)((int32_t)npar * coef[24])) >> 15;
            self->o_20ae[18] = (int16_t)t;
            self->o_20ae[19] = old;
            sum = SAT((int16_t)sum + t);

            old = self->o_20ae[20];
            t = (int32_t)((uint32_t)((int32_t)old * self->o_2092[3]) * 2u
                 + (uint32_t)((int32_t)self->o_20ae[21] * self->o_2092[4])
                 + (uint32_t)((int32_t)npar * coef[26])) >> 15;
            self->o_20ae[20] = (int16_t)t;
            self->o_20ae[21] = old;
            sum = SAT((int16_t)sum + t);

            acc = (int16_t)((((int32_t)coef[28] * npar) >> 15) + sum);
        }

        /* ---- the output stage
         *
         * The two scalings are lea chains in the original, 6221/32768 on the
         * parallel branch and 7537/32768 on the feedback.  Written as the
         * multiplies they are rather than as the shift-and-add the compiler
         * made of them. */
        {
            int32_t v = ((int32_t)(int16_t)acc * 6221) >> 15;
            int32_t d = (int16_t)((int16_t)v - self->o_2086);
            int32_t fb = ((int32_t)self->o_207c * 7537) >> 15;

            self->o_2086 = (int16_t)v;
            d = (int16_t)(d - (int16_t)fb);
            d = (int16_t)(d + (int16_t)ser);
            self->o_207c = (int16_t)d;
            if (mutebit != 0)
                d = 0;
            *(int16_t *)(self->out_buf + (self->out_count & ~1u)) =
                (int16_t)((int16_t)d << 3);
            self->out_count += 2;
        }
        n_left = (int16_t)(n_left - 1);
    } while (n_left > 0);

    /* Two things the epilogue writes that the loop never touches: the noise
     * generator's final state, and o_2092[10], which is simply coef[33]. */
    self->o_2088 = (uint16_t)noise;
    self->o_208e = last_t;
    self->o_2092[10] = coef[33];
}
