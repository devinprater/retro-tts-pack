#include <string.h>
#include "bst_synth.h"

/* Both lookups clamp the same way the engine's do: an index outside 0..255
   yields 255 when positive and 0 when negative. */
static int lut(const int16_t *t, int x);

static int lut(const int16_t *t, int x) {
    if (x >= 0 && x <= 255) return t[x];
    return x >= 0 ? 255 : 0;
}

void bst_interp_init(bst_interp *ip, const bst_tables *t) {
    memset(ip, 0, sizeof *ip);
    ip->t = t;
}

void bst_interp_snap(bst_interp *ip, const int16_t target[BST_ORDER]) {
    memcpy(ip->state, target, sizeof ip->state);
}

void bst_interp_step(bst_interp *ip, const int16_t target[BST_ORDER],
                     int dur, int remaining) {
    /* rate = log(dur) - log(scaled remaining) - four octaves. Adding it to
       log(distance/2) and taking the antilog gives the step. */
    int idx = (((remaining - (remaining >> 2)) + dur) >> 5) + 1;
    int rate = lut(ip->t->log, dur) - lut(ip->t->log, idx) - 0x80;

    for (int i = 0; i < BST_ORDER; i++) {
        int diff = target[i] - ip->state[i];
        if (diff < 0)
            ip->state[i] -= (int16_t)lut(ip->t->alog, lut(ip->t->log, (-diff) >> 1) + rate);
        else
            ip->state[i] += (int16_t)lut(ip->t->alog, lut(ip->t->log, diff >> 1) + rate);
    }
}

int bst_transition_len(const int16_t from[BST_ORDER], const int16_t to[BST_ORDER]) {
    int sum = 0;
    for (int i = 0; i < BST_ORDER; i++) {
        int d = from[i] - to[i];
        sum += d < 0 ? -d : d;
    }
    return (sum >> 2) + (sum >> 3);
}

void bst_gain_init(bst_gain *g, const bst_tables *t) {
    g->t = t;
    g->acc = 0;
}

int bst_gain_value(const bst_gain *g, int base, int adj, int exc_class) {
    int v = base + (g->acc >> 8);
    if (exc_class == BST_NOISE)       v += adj;
    else if (exc_class == BST_VOICED) v += adj + 8;
    return v & 0xff;
}

void bst_gain_step(bst_gain *g, int target, int dur, int clock) {
    int diff = target - (g->acc >> 8);
    if (diff == 0) return;
    int idx = ((clock + dur) >> 4) + 1;
    int mag = diff < 0 ? -diff : diff;
    int step = lut(g->t->alog, lut(g->t->log, mag) - lut(g->t->log, idx) + lut(g->t->log, dur));
    g->acc = (int16_t)(g->acc + (diff > 0 ? step * 16 : -step * 16));
}

void bst_pitch_init(bst_pitch *p, const bst_tables *t) {
    p->t = t;
    p->acc = 0;
}

void bst_pitch_step(bst_pitch *p, uint16_t target, int dur, int clock) {
    unsigned t16 = (unsigned)(dur + clock) & 0xffff;
    unsigned u = t16 >> 4;
    int small = u < 0x100;
    if (!small) u = t16 >> 8;

    int rate = lut(p->t->log, dur) - lut(p->t->log, (int)u + 1);
    int diff = (int)(target >> 5) - (int)(p->acc >> 5);
    if (diff == 0) return;

    int mag = diff < 0 ? -diff : diff;
    int step = lut(p->t->alog, lut(p->t->log, mag) + rate - 0x80);
    int scale = small ? 0x20 : 2;
    p->acc = (uint16_t)(p->acc + (diff > 0 ? step * scale : -step * scale));
}

int bst_pitch_period(const bst_pitch *p) {
    return p->acc >> 8;
}

int bst_segment_duration(const int16_t scale[16], int first_byte, int rate,
                         int slow) {
    /* (scale * rate) >> 6, then times fourteen, reached as (x * 7) << 3 >> 2. */
    int32_t v = (int32_t)(uint16_t)scale[first_byte & 0x0F] * rate;
    v >>= 6;
    v &= 0xFFFF;
    v = ((v * 7) << 3) >> 2;
    if (slow) v = (int16_t)(v << 2);
    return (int16_t)v;
}

void bst_duration_scale(const int16_t base[16], int rate, int16_t out[16]) {
    if (rate < -100) rate = -100;
    for (int i = 0; i < 16; i++) {
        int16_t v = base[i];
        if (rate != 0 && v > 11) {
            v = (int16_t)(((rate + 100) * (int32_t)v) / 100);
            if (v < 12) v = 12;
        }
        out[i] = v;
    }
}
