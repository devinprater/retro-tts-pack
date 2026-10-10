#ifndef BST_SYNTH_H
#define BST_SYNTH_H

#include <stdint.h>
#include <stddef.h>

/* The BeSTspeech low-level synthesizer: a 10th-order LPC lattice driven by a
   stored glottal pulse and a table of noise values, all in Q8 fixed point.
   Frames arrive as 16 opaque bytes and each one is held for a whole number of
   pitch periods, so frame duration is pitch-synchronous rather than fixed. */

#define BST_ORDER        10
#define BST_PULSE_LEN    160   /* phase units, 16 per output sample */
#define BST_NOISE_BYTES   64
#define BST_GAIN_ENTRIES 256

/* Tables lifted from the original binary. The log and antilog pair are
   base two with 32 units to the octave, and the engine uses them to multiply
   and divide without a multiply: add logs, take the antilog. */
typedef struct {
    int16_t pulse[BST_PULSE_LEN];
    int16_t noise[BST_NOISE_BYTES / 2];
    int16_t gain[BST_GAIN_ENTRIES];
    int16_t log[256];
    int16_t alog[256];
    int16_t duration[16];   /* base segment durations, before rate scaling */
    /* How far the lattice's accumulator is shifted down on the way out.
       Three in the earlier builds, five in the 2006 ones. Zero means three. */
    int     out_shift;
    int     noise_kind;
    /* The 1998 modules are an eight-bit synthesizer: they round rather than
       truncate, shift by ten, clamp to a signed byte and hand the host an
       unsigned one. Everything they say carries that quantisation, so it is
       kept and widened to sixteen bits rather than smoothed away. */
    int     out_8bit;
} bst_tables;

/* Parameter interpolator. The engine never jumps to a target: each frame it
   moves the running state a fraction of the remaining distance, the fraction
   being this frame's duration over the time left in the transition. Working
   in the log domain is what makes the result awkward to reproduce by fitting
   a curve, and exact to reproduce by following the arithmetic. */
typedef struct {
    const bst_tables *t;
    int16_t state[BST_ORDER];   /* twice the Q8 coefficient */
} bst_interp;

void bst_interp_init(bst_interp *ip, const bst_tables *t);
void bst_interp_snap(bst_interp *ip, const int16_t target[BST_ORDER]);
void bst_interp_step(bst_interp *ip, const int16_t target[BST_ORDER],
                     int dur, int remaining);

/* Minimum length of a transition, from the total coefficient movement across
   it: three eighths of the summed absolute difference. */
int  bst_transition_len(const int16_t from[BST_ORDER], const int16_t to[BST_ORDER]);

/* Gain, smoothed the same way but on its own clock. The accumulator carries
   eight fractional bits and the emitted frame byte is its integer part plus a
   base and an adjustment that depends on the excitation class. */
typedef struct {
    const bst_tables *t;
    int16_t acc;
} bst_gain;

void bst_gain_init(bst_gain *g, const bst_tables *t);
int  bst_gain_value(const bst_gain *g, int base, int adj, int exc_class);
void bst_gain_step(bst_gain *g, int target, int dur, int clock);

/* Pitch, the third smoother of the same family. The accumulator is unsigned
   and its high byte is the period in samples that reaches frame byte 3, so a
   voiced frame lasts exactly one pitch period. The step is scaled by 32 while
   the clock is short and by 2 once it is long, which slows the glide as a
   phrase runs on. */
typedef struct {
    const bst_tables *t;
    uint16_t acc;
} bst_pitch;

void bst_pitch_init(bst_pitch *p, const bst_tables *t);
void bst_pitch_step(bst_pitch *p, uint16_t target, int dur, int clock);
int  bst_pitch_period(const bst_pitch *p);

/* Segment duration. The scale table is indexed by the low nibble of a segment
   record's first byte and has already been adjusted for the speaking rate;
   the rate argument is the per-position byte the engine keeps alongside each
   segment. Verified against the engine at the instruction that computes it. */
int  bst_segment_duration(const int16_t scale[16], int first_byte, int rate,
                          int slow);

/* Builds the runtime scale table from the stored base table for a speaking
   rate in the engine's units, where 0 is normal and -100 the slowest. */
void bst_duration_scale(const int16_t base[16], int rate, int16_t out[16]);

typedef struct {
    const bst_tables *t;

    int32_t  b[BST_ORDER];        /* lattice backward states */
    int16_t  k[BST_ORDER];        /* reflection coefficients, Q8 */

    int16_t  gain;
    uint16_t period;              /* Q4: 16 phase units per output sample */
    uint16_t phase;
    int32_t  rand;                /* excitation noise generator */
    uint8_t  mode;                /* 0x00 silence, 0x10 voiced,
                                     0x20 noise, 0x30 mixed */
    int      periods_left;
} bst_synth;

/* Excitation modes, as held in the top nibble of frame byte 0. */
#define BST_SILENT 0x00
#define BST_VOICED 0x10
#define BST_NOISE  0x20
#define BST_MIXED  0x30

void   bst_synth_init(bst_synth *s, const bst_tables *t);

/* Loads one 16-byte frame. Returns 0 for an end marker (byte 0 of 0x00 or
   0xff), 1 otherwise. */
int    bst_synth_frame(bst_synth *s, const uint8_t f[16]);

/* Renders the current frame to completion, appending to out. Returns the
   number of samples written, capped at max. */
size_t bst_synth_run(bst_synth *s, int16_t *out, size_t max);

/* Where each table sits in a build, as a file offset. */
typedef struct {
    size_t pulse, noise, gain, log, alog, duration;
    /* The 2006 builds store the log pair a byte to an entry where the earlier
       ones store a word. Zero means a word. */
    int    log_bytes;
    /* The lattice's output shift; zero means three. */
    int    out_shift;
    /* Which noise generator: zero for the earlier builds' sixteen-bit
       multiply-add, one for the 2006 and 1998 builds' thirty-two-bit one. */
    int    noise_kind;
    /* Set for the 1998 modules, which synthesize in eight bits. */
    int    out_8bit;
} bst_offsets;

extern const bst_offsets BST_OFFSETS_1995;

/* Reads the tables out of a loaded BeSTspeech image. The plain form assumes
   the 1995 build; the other takes the offsets, which tablescan.py reports. */
int    bst_tables_load(bst_tables *t, const void *image, size_t len);
int    bst_tables_load_at(bst_tables *t, const void *image, size_t len,
                          const bst_offsets *o);

/* The 1998 modules split them: the excitation and gain tables are in the core
   module every language shares, and only the log pair is in the language
   module, at an offset of its own. `core` is that module, or NULL when one
   file holds everything and this is bst_tables_load_at. */
int    bst_tables_load_split(bst_tables *t, const void *image, size_t len,
                             const bst_offsets *o,
                             const void *core, size_t corelen);

#endif
