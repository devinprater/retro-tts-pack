#include <string.h>
#include "bst_synth.h"

/* Every arithmetic step here mirrors the original's fixed-point exactly:
   Q8 products with an arithmetic shift right of 8, 32-bit lattice state,
   and a final shift of 3 before clamping. Deviating anywhere costs
   bit-exactness even where the audible difference would be nil. */

void bst_synth_init(bst_synth *s, const bst_tables *t) {
    memset(s, 0, sizeof *s);
    s->t = t;
    s->rand = 0xa396;   /* the seed the engine resets to */
}

int bst_synth_frame(bst_synth *s, const uint8_t f[16]) {
    if (f[0] == 0x00 || f[0] == 0xff) return 0;

    s->periods_left = f[0] & 0x0f;
    s->mode         = f[0] & 0x30;
    s->gain         = s->t->gain[f[2]];
    s->period       = (uint16_t)(((uint16_t)f[3] << 4) | (f[1] & 0x0f));

    /* Reflection coefficients are signed bytes scaled by two, so they span
       the full Q8 range of plus or minus one. The first one carries an extra
       low bit in the top bit of byte 1. */
    s->k[0] = (int16_t)((int16_t)(int8_t)f[4] * 2 | (f[1] >> 7));
    for (int i = 1; i < BST_ORDER; i++)
        s->k[i] = (int16_t)((int16_t)(int8_t)f[4 + i] * 2);

    return 1;
}

/* One step of the excitation noise generator, returning an index into the
   noise table. The 2006 builds carry the seed through a thirty-two bit
   multiply-add and take the low five bits; the earlier ones keep it in
   sixteen and take bits eight to twelve. */
static inline int noise_step(bst_synth *s) {
    if (s->t->noise_kind) {
        s->rand = (int32_t)(s->rand * 3 + 5) >> 1;
        return s->rand & 0x1f;
    }
    s->rand = (uint16_t)(s->rand * 5 + 3);
    return ((int)(s->rand & 0x1f00) >> 8);
}

static inline int32_t excitation(bst_synth *s) {
    int32_t x;

    if (s->mode == BST_NOISE) {
        x = s->t->noise[noise_step(s)];
        return (x * s->gain) >> 8;
    }

    if (s->mode == BST_MIXED && s->phase >= BST_PULSE_LEN) {
        x = s->t->noise[noise_step(s)];
        /* The noise half of a mixed frame runs at half amplitude. */
        return ((x * s->gain) >> 1) >> 8;
    }

    if (s->phase < BST_PULSE_LEN) {
        x = s->t->pulse[s->phase];
        return (x * s->gain) >> 8;
    }
    return 0;
}

/* One pass of the lattice, from the highest-order stage down. The recursion
   is the standard reflection-coefficient ladder:
       f[i-1] = f[i] - k[i] * b[i-1]
       b[i]   = b[i-1] + k[i] * f[i-1]
   with b[] carried between samples. */
static inline int16_t lattice(bst_synth *s, int32_t x) {
    /* The top stage only subtracts; it has no backward state above it to
       update. Every stage below both subtracts and writes the state one
       position up, so stage i pairs coefficient i with state i but produces
       state i+1. */
    x -= (s->b[BST_ORDER - 1] * s->k[BST_ORDER - 1]) >> 8;

    for (int i = BST_ORDER - 2; i >= 0; i--) {
        int32_t k = s->k[i];
        x -= (s->b[i] * k) >> 8;
        s->b[i + 1] = ((x * k) >> 8) + s->b[i];
    }
    s->b[0] = x;

    if (s->t->out_8bit) {
        /* Rounded, shifted by ten, clamped to a signed byte. The engine adds
           a hundred and twenty eight and hands the host that byte; widening
           it back keeps the step it actually produced. */
        x = (x + 0x200) >> 10;
        if (x < -128) x = -128;
        if (x > 127) x = 127;
        return (int16_t)(x * 256);
    }
    x >>= s->t->out_shift ? s->t->out_shift : 3;
    if (x < -32767) x = -32767;
    if (x > 32767) x = 32767;
    return (int16_t)x;
}

size_t bst_synth_run(bst_synth *s, int16_t *out, size_t max) {
    size_t n = 0;

    /* A silent frame emits zeroes without disturbing the filter state. */
    for (;;) {
        if (n >= max) break;

        if (s->mode == BST_SILENT) out[n++] = 0;
        else                       out[n++] = lattice(s, excitation(s));

        s->phase = (uint16_t)(s->phase + 16);
        if (s->phase < s->period) continue;

        s->phase = (uint16_t)(s->phase - s->period);
        if (--s->periods_left <= 0) break;
    }
    return n;
}

/* Where each table sits in the 1995 build. Other builds carry the same tables
   at their own offsets, and the ones with no log pair leave those zero. */
const bst_offsets BST_OFFSETS_1995 = {
    0x17E48, 0x17E08, 0x18488, 0x17A08, 0x17C08, 0x18C20, 0, 0, 0, 0
};

int bst_tables_load_at(bst_tables *t, const void *image, size_t len,
                       const bst_offsets *o) {
    const uint8_t *p = image;
    memset(t, 0, sizeof *t);
    if (o->pulse + sizeof t->pulse > len) return -1;
    if (o->noise + sizeof t->noise > len) return -1;
    if (o->gain  + sizeof t->gain  > len) return -1;
    memcpy(t->pulse, p + o->pulse, sizeof t->pulse);
    memcpy(t->noise, p + o->noise, sizeof t->noise);
    memcpy(t->gain,  p + o->gain,  sizeof t->gain);
    t->out_shift = o->out_shift;
    t->noise_kind = o->noise_kind;
    t->out_8bit = o->out_8bit;
    /* The 2006 builds have no log pair: their interpolation is not in the
       log domain, and the lattice never reads them. */
    if (o->log_bytes) {
        if (o->log  && o->log  + 256 <= len)
            for (int i = 0; i < 256; i++) t->log[i]  = p[o->log  + i];
        if (o->alog && o->alog + 256 <= len)
            for (int i = 0; i < 256; i++) t->alog[i] = p[o->alog + i];
    } else {
        if (o->log  && o->log  + sizeof t->log  <= len) memcpy(t->log,  p + o->log,  sizeof t->log);
        if (o->alog && o->alog + sizeof t->alog <= len) memcpy(t->alog, p + o->alog, sizeof t->alog);
    }
    if (o->duration && o->duration + sizeof t->duration <= len)
        memcpy(t->duration, p + o->duration, sizeof t->duration);
    return 0;
}

int bst_tables_load_split(bst_tables *t, const void *image, size_t len,
                          const bst_offsets *o,
                          const void *core, size_t corelen) {
    if (!core) return bst_tables_load_at(t, image, len, o);

    /* The excitation and gain tables out of the core, everything else out of
       the language module. */
    bst_offsets exc = *o;
    exc.log = exc.alog = exc.duration = 0;
    if (bst_tables_load_at(t, core, corelen, &exc) < 0) return -1;

    bst_tables rest;
    bst_offsets own = *o;
    own.pulse = own.noise = own.gain = 0;
    if (bst_tables_load_at(&rest, image, len, &own) < 0) return -1;
    memcpy(t->log, rest.log, sizeof t->log);
    memcpy(t->alog, rest.alog, sizeof t->alog);
    memcpy(t->duration, rest.duration, sizeof t->duration);
    return 0;
}

int bst_tables_load(bst_tables *t, const void *image, size_t len) {
    return bst_tables_load_at(t, image, len, &BST_OFFSETS_1995);
}
