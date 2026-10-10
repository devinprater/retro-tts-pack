#ifndef BST_FRAMES_H
#define BST_FRAMES_H

#include <stdint.h>
#include "bst_text.h"
#include "bst_synth.h"

/* Frame generation: the three record streams the front end produces, turned
   into the sixteen-byte frames the lattice consumes.
 *
 * The three streams are read by three cursors that advance at different rates
 * and feed each other. Segments name diphone entries; expanding one appends
 * its records to a small ring and, in passing, fills in the durations of the
 * transition records that cover it. Transitions carry gain and pitch targets
 * and the spans over which to reach them. Intonation records carry the pitch
 * contour. A frame comes out when all three agree on how long the next one
 * lasts. */

typedef struct { int16_t  count, a, b; uint16_t index; } bst_seg_rec;
typedef struct { uint8_t  index, dur, p1, p2; int16_t span; } bst_trn_rec;
typedef struct { uint8_t  kind, period; int16_t dur, slope; } bst_ito_rec;

#define BST_QUEUE 12

typedef struct {
    const bst_image  *img;
    const bst_tables *tab;

    bst_seg_rec *seg; int nseg;
    bst_trn_rec *trn; int ntrn;
    bst_ito_rec *ito; int nito;

    int segrd, itord, trnbase, trnpend;

    struct { uint8_t rec[5]; int16_t dur; } q[BST_QUEUE];
    int qrd, qwr;
    int qcur;                    /* the queue entry the targets come from */

    bst_seg_rec cur;             /* the segment being consumed */
    bst_ito_rec inton, pend;     /* the intonation record and its predecessor */
    bst_trn_rec ctrn;            /* the transition being consumed */
    int nofrac;                  /* this frame carries no pitch fraction */

    int16_t dur, left, gclock, segleft, mid;
    /* The intonation clock is thirty-two bits in the engine, which stores and
       tests both halves; a phrase can carry one past what sixteen bits hold. */
    int32_t pclock;
    int16_t targets[BST_ORDER], prev[BST_ORDER], state[BST_ORDER];
    int16_t curtgt, prvtgt;
    int16_t durscale[16];

    int exc, excprev, voiced, tclass, defexc, lastclass;
    int gain_base, gain_adj, gain_target;
    int16_t gain_acc;
    uint16_t pitch_acc, pitch_target;
    int16_t pitch_period;
    int voice, rate, rate_loaded, flags;
    int fresh, failed, done;

    uint8_t frame[16];
    uint8_t *out;                /* sixteen bytes per frame */
    int nout, maxout;
} bst_gen;

/* Runs the whole generation. Returns the number of frames written. */
int bst_generate(bst_gen *g);


#endif
