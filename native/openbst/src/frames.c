#include <stdio.h>
#include <string.h>
#include "bst_frames.h"


/* Frame generation. See bst_frames.h for the shape; this file follows the
   engine's own control flow, because the three cursors feed each other and a
   tidier arrangement changes when the durations get filled in. */

static const int RECLEN[8] = { 1, 3, 1, 3, 3, 5, 1, 1 };

static int u8at(const bst_image *img, unsigned va, unsigned i) {
    return bst_u8(img, va, (int)i);
}
static int s16at(const bst_image *img, unsigned va, unsigned i) {
    return bst_s16(img, va, (int)i);
}

static int lg(const bst_gen *g, int x) {
    return (x >= 0 && x <= 255) ? g->tab->log[x] : (x >= 0 ? 255 : 0);
}
static int alg(const bst_gen *g, int x) {
    return (x >= 0 && x <= 255) ? g->tab->alog[x] : (x >= 0 ? 255 : 0);
}

static void fail(bst_gen *g) { g->failed = 1; }

/* ---- the record streams ------------------------------------------------ */

static int expand(bst_gen *g, bst_seg_rec *r);

/* Makes sure the nth segment from the read cursor has been expanded. */
static bst_seg_rec *seg_at(bst_gen *g, int n) {
    if (n > 0 && g->nseg == g->segrd) { fail(g); return NULL; }
    int base = g->segrd;
    for (int k = 0; k <= n; k++) {
        int i = base + k;
        if (i < 0 || i >= g->nseg) break;
        if (g->seg[i].a == -1 && (g->seg[i].index & 0xF000) == 0) {
            if (expand(g, &g->seg[i]) != 2) { fail(g); return NULL; }
            base = g->segrd;
        }
    }
    int i = n + base;
    if (i < 0 || i >= g->nseg) { fail(g); return NULL; }
    return &g->seg[i];
}

static bst_seg_rec *seg_peek(bst_gen *g) {
    if (g->nseg == g->segrd) return NULL;
    bst_seg_rec *r = &g->seg[g->segrd];
    if (r->a == -1 && (r->index & 0xF000) == 0)
        if (expand(g, r) != 2) { fail(g); return NULL; }
    return r;
}

static int seg_take(bst_gen *g, bst_seg_rec *dst) {
    bst_seg_rec *r = seg_peek(g);
    if (!r) { fail(g); return -2; }
    *dst = *r;
    g->segrd++;
    return 2;
}

/* The nth transition from the base, without filling anything in: expanding a
   segment writes the spans, so it must not ask for them. */
static bst_trn_rec *trn_raw(bst_gen *g, int n) {
    int i = g->trnbase + n;
    if (i < 0 || i >= g->ntrn) return NULL;
    return &g->trn[i];
}

/* Makes sure the nth transition from the base has its span filled in. */
static bst_trn_rec *trn_at(bst_gen *g, int n) {
    int guard = 0;
    while (g->ntrn - g->trnbase <= n ||
           g->trn[g->trnbase + n].span == -1) {
        if (!seg_at(g, guard)) { fail(g); return NULL; }
        if (++guard > 0x400) { fail(g); return NULL; }
    }
    return &g->trn[g->trnbase + n];
}

static int ito_take(bst_gen *g, bst_ito_rec *dst) {
    if (g->itord >= g->nito) { g->done = 1; return 0; }
    *dst = g->ito[g->itord++];
    return 2;
}

/* ---- the queue --------------------------------------------------------- */

static int enqueue(bst_gen *g, const uint8_t *rec, int16_t dur) {
    memcpy(g->q[g->qwr].rec, rec, 5);
    g->q[g->qwr].dur = dur;
    g->qwr++;
    if (g->qwr > BST_QUEUE - 1) g->qwr = 0;
    if (g->qwr == g->qrd) { fail(g); return -2; }
    return 2;
}

/* Fills the queue until it holds at least n+1 entries, then names the nth. */
static int queue_at(bst_gen *g, int n) {
    int guard = 0;
    for (;;) {
        int avail = (g->qwr < g->qrd ? BST_QUEUE : 0) - g->qrd + g->qwr;
        if (avail > n) break;
        if (!seg_at(g, guard)) { fail(g); return -1; }
        if (++guard > 0x400) { fail(g); return -1; }
    }
    int i = g->qrd + n;
    return i < BST_QUEUE ? i : i - BST_QUEUE;
}

static int dequeue(bst_gen *g) {
    if (g->qwr == g->qrd) { fail(g); return -1; }
    int i = g->qrd;
    g->left = (int16_t)(g->left + g->q[i].dur);
    g->qrd++;
    if (g->qrd > BST_QUEUE - 1) g->qrd = 0;
    int t = (g->q[i].rec[0] & 0x70) >> 4;
    if (t > 1 && t < 4) {
        int j = queue_at(g, 0);
        g->qcur = j < 0 ? i : j;
    } else g->qcur = i;
    return i;
}

/* ---- expanding a segment ----------------------------------------------- */

/* Whether a record carries a voiced class, remembered across the records that
   do not name one of their own. */
static int rec_class(bst_gen *g, const uint8_t *rec) {
    int t = (rec[0] & 0x70) >> 4;
    if (t == 0) { g->lastclass = 0; return 0; }
    if (t == 2) return g->lastclass;
    int v = (rec[2] << 8) | rec[1];
    int cs = g->img->t.class_shift ? g->img->t.class_shift : 9;
    g->lastclass = u8at(g->img, g->img->t.classtab, (unsigned)(v >> cs));
    if ((g->flags & 1) && (v >> cs) == 2) g->lastclass = 1;
    return g->lastclass;
}

static void load_rate(bst_gen *g, int rate) {
    int16_t base[16];
    for (int i = 0; i < 16; i++) base[i] = (int16_t)s16at(g->img, g->img->t.basedur, (unsigned)i);
    if (rate == 0) { memcpy(g->durscale, base, sizeof base); return; }
    g->rate = rate;
    bst_duration_scale(base, rate, g->durscale);
}

static int expand(bst_gen *g, bst_seg_rec *r) {
    if (g->rate != g->rate_loaded) { g->rate_loaded = g->rate; load_rate(g, g->rate); }
    if (!bst_at(g->img, g->img->t.diph_offsets + (uint32_t)r->index * 2, 2)) {
        fail(g);
        return -2;
    }
    unsigned entry = (unsigned)bst_u16(g->img, g->img->t.diph_offsets, r->index);
    const uint8_t *pb = bst_at(g->img, g->img->t.diph_records + entry, 1);
    if (!pb) { fail(g); return -2; }

    int16_t total = 0, voiced = 0;
    for (int k = 0; k < r->count; k++) {
        int16_t was = total;
        bst_trn_rec *tp = trn_raw(g, g->trnpend);
        if (!tp) { fail(g); return -2; }
        int b0;
        do {
            b0 = pb[0];
            /* Sixteen bits throughout, as the engine has it: the product of
               the scale and the duration is truncated before it is shifted,
               and so is the product with fifty-six. Doing either in wider
               arithmetic agrees for as long as nothing overflows, which is
               always in the 1995 build and not in the 1998 one, whose first
               scale entry is twice as large. */
            uint16_t u = (uint16_t)g->durscale[b0 & 0x0F];
            /* The duration byte is signed: the engine sign-extends it before
               the multiply, so a transition longer than 127 units multiplies
               as a negative number. */
            uint16_t mul = g->img->t.dur_mult ? g->img->t.dur_mult : 0x38;
            uint16_t p16;
            int32_t d;
            if (g->img->t.trn_dur_wide) {
                p16 = (uint16_t)(((uint32_t)u * tp->dur) >> 6);
                d = (int32_t)(((uint32_t)p16 * mul) >> 2);
            } else {
                p16 = (uint16_t)(u * (uint16_t)(int16_t)(int8_t)tp->dur);
                p16 = (uint16_t)(p16 >> 6);
                d = (int32_t)(uint16_t)((uint16_t)(p16 * mul) >> 2);
            }
            if (g->flags & 4) d = (int16_t)(d << 2);
            int16_t dd = (int16_t)d;
            if (bst_trace)
                bst_tracef("expand nib=%x u=%04x dur=%02x -> %04x\n",
                        b0 & 0x0F, u, tp->dur, (uint16_t)dd);
            if (enqueue(g, pb, dd) == -2) { fail(g); return -2; }
            total = (int16_t)(total + dd);
            if (rec_class(g, pb) != 0) voiced = (int16_t)(voiced + dd);
            pb += RECLEN[(b0 & 0x70) >> 4];
        } while (!(b0 & 0x80));
        int i = g->trnbase + g->trnpend;
        if (i > 0x202) i -= 0x203;
        if (i >= 0 && i < g->ntrn) g->trn[i].span = (int16_t)(total - was);
        g->trnpend++;
    }
    r->a = total;
    r->b = voiced;
    return 2;
}

/* ---- the transition cursor --------------------------------------------- */

/* Walks forward to the next transition that names a gain target, summing the
   spans on the way: that sum is the clock the gain smoother runs against. */
static int settle(bst_gen *g) {
    if (g->ctrn.p1 != 0x7F) g->gain_acc = (int16_t)(g->ctrn.p1 << 8);
    int n = 0;
    int16_t span = g->ctrn.span;
    int b = g->ctrn.p2;
    int guard = 0;
    while (b == 0x7F) {
        bst_trn_rec *p = trn_at(g, n);
        if (!p) { fail(g); return -2; }
        b = p->p1;
        if (b != 0x7F) break;
        span = (int16_t)(span + p->span);
        n++;
        b = p->p2;
        if (++guard > 0x400) break;
    }
    g->gain_target = b;
    g->gclock = span;
    return 2;
}

static int trn_advance(bst_gen *g) {
    bst_trn_rec *p = trn_at(g, 0);
    if (!p) { fail(g); return -2; }
    g->ctrn = *p;
    g->trnbase++;
    g->trnpend--;
    if (settle(g) == -2) { fail(g); return -2; }
    return 2;
}

/* ---- targets ----------------------------------------------------------- */

static void fetch(bst_gen *g, int16_t dst[BST_ORDER], int target) {
    bst_target_coeffs(g->img, g->voice, target, dst);
}

/* The record at the head of the queue names this position's targets; type two
   and four carry none of their own and inherit the previous set. */
static int seg_entry(bst_gen *g, const uint8_t *rec) {
    int t = (rec[0] & 0x70) >> 4;
    switch (t) {
    case 1: case 3: case 5:
        g->curtgt = (int16_t)(((rec[2] << 8) | rec[1]) & 0x1FF);
        fetch(g, g->prev, g->curtgt);
        break;
    case 2: case 4:
        memcpy(g->prev, g->targets, sizeof g->prev);
        g->curtgt = g->prvtgt;
        break;
    default:
        g->curtgt = -1;
        break;
    }

    switch (t - 1) {
    case 0:
        memcpy(g->targets, g->prev, sizeof g->targets);
        g->prvtgt = g->curtgt;
        break;
    case 1: case 2: {
        const uint8_t *c = g->q[g->qcur].rec;
        g->prvtgt = (int16_t)(((c[2] << 8) | c[1]) & 0x1FF);
        fetch(g, g->targets, g->prvtgt);
        break;
    }
    case 3:
        g->prvtgt = (int16_t)(((rec[2] << 8) | rec[1]) & 0x1FF);
        fetch(g, g->targets, g->prvtgt);
        break;
    case 4:
        g->prvtgt = (int16_t)(((rec[4] << 8) | rec[3]) & 0x1FF);
        fetch(g, g->targets, g->prvtgt);
        break;
    default:
        g->prvtgt = -1;
        break;
    }

    /* A slow voice gets a floor on how fast the spectrum may move. */
    if ((rec[0] & 0x70) != 0 && g->rate < -0x50) {
        int sum = 0;
        for (int i = 0; i < BST_ORDER; i++) {
            int d = g->prev[i] - g->targets[i];
            sum += d < 0 ? -d : d;
        }
        int16_t need = (int16_t)((sum >> 2) + (sum >> 3));
        if (g->left < need) {
            int16_t add = (int16_t)(need - g->left);
            g->left = (int16_t)(g->left + add);
            g->pclock = g->pclock + add;
            g->mid = (int16_t)(g->mid + add);
            g->segleft = (int16_t)(g->segleft + add);
            g->gclock = (int16_t)(g->gclock + add);
        }
    }
    g->fresh = 1;
    return 2;
}

static int next_record(bst_gen *g, int prev_index, int first) {
    int hi = first ? 0x80 : g->q[prev_index].rec[0];
    if (hi & 0x80) {
        g->cur.count--;
        if (g->cur.count < 0) { fail(g); return -1; }
        if (trn_advance(g) != 2) { fail(g); return -1; }
    }
    return dequeue(g);
}

/* ---- the clocks -------------------------------------------------------- */

static int interp_clock(bst_gen *g) {
    int v;
    if (g->pend.dur == -1) {
        int x = (g->mid < 1) ? g->segleft : g->mid;
        v = g->img->t.inton_slope_shift
          ? (g->pend.slope * x * 3) >> 8
          : (g->pend.slope * x) / 0x55;
    } else {
        if (g->pend.dur < 0) return -2;
        if (g->pend.slope == 0 && g->pend.dur == 0 && g->mid > 0) v = g->mid;
        else {
            int sm = g->img->t.slope_mult ? g->img->t.slope_mult : 0xB1;
            int dm = g->img->t.inton_dur_mult ? g->img->t.inton_dur_mult : 0x3AC;
            v = (int)((uint16_t)(g->pend.slope * sm) >> 4)
              + g->pend.dur * dm + g->segleft;
        }
    }
    if (bst_trace) bst_tracef("clk dur=%d slope=%d mid=%d segleft=%d -> %d\n",
                           g->pend.dur, g->pend.slope, g->mid, g->segleft, v);
    g->pclock = v;
    if (g->pend.kind == 0) {
        g->pend.kind = 2;
        g->pitch_period = g->pend.period;
        g->pitch_acc = (uint16_t)(g->pend.period << 8);
    }
    g->pitch_target = g->inton.kind ? (uint16_t)(g->inton.period << 8) : g->pitch_acc;
    return 2;
}

/* ---- the frame --------------------------------------------------------- */

/* The gain byte leaves the builder as a level and is emitted as a level
   compensated for the filter: doubled, less a thirty-second of the summed
   logs of the ten coefficients. Without this the lattice's own gain rides on
   top of the intended one. */
/* An unvoiced frame's period, split into a repeat count and a shorter period
   so the excitation restarts more often. The boundaries are the engine's. */
static void chunk_unvoiced(bst_gen *g) {
    if (!g->img->t.unvoiced_chunk) return;
    if (!(g->frame[0] & 0x20)) return;
    if (g->frame[0] & 0x10) {
        g->frame[0] = (uint8_t)((g->frame[0] & 0x0F) | 0xE0);
        return;
    }
    int p = g->frame[3];
    if (p == 0x68)     { g->frame[0] = 0xE8; g->frame[3] = 0x0D; }
    else if (p > 0xA0) { g->frame[0] = 0xEE; g->frame[3] = (uint8_t)(p >> 4); }
    else if (p > 0x50) { g->frame[0] = 0xE8; g->frame[3] = (uint8_t)(p >> 3); }
    else if (p > 0x28) { g->frame[0] = 0xE4; g->frame[3] = (uint8_t)(p >> 2); }
    else if (p > 0x14) { g->frame[0] = 0xE2; g->frame[3] = (uint8_t)(p >> 1); }
    else               { g->frame[0] = 0xE1; }
    g->frame[1] = 0;
}

static void compensate(bst_gen *g) {
    if (g->frame[0] == 0xFF) return;
    if ((int16_t)(g->frame[2] * 2) == 0) return;
    int t = s16at(g->img, g->img->t.coefgain, g->frame[4]);
    if (g->frame[1] & 0x80)
        t = (s16at(g->img, g->img->t.coefgain, g->frame[4] + 1) + t) >> 1;
    for (int i = 5; i < 14; i++) t += s16at(g->img, g->img->t.coefgain, g->frame[i]);
    int v = (int16_t)(g->frame[2] * 2) - (int16_t)((int16_t)(t + 0x10) >> 5);
    v = (int16_t)v;
    chunk_unvoiced(g);
    if ((v >> 8) & 0xFF) {
        if (v > 0xFF) { g->frame[2] = 0xFF; return; }
        if (v < 0) v = 0;
    }
    g->frame[2] = (uint8_t)v;
}

static void emit(bst_gen *g) {
    if (bst_trace)
        bst_tracef("gain dur=%04x gclock=%04x acc=%04x target=%04x\n",
                (uint16_t)g->dur, (uint16_t)g->gclock, (uint16_t)g->gain_acc,
                (uint16_t)g->gain_target);
    compensate(g);
    /* The fraction byte is the gain compensation's own input and is dropped on
       the way out, not before it. */
    if (g->nofrac && g->img->t.unvoiced_no_frac) g->frame[1] = 0;
    if (g->nout < g->maxout) memcpy(g->out + g->nout * 16, g->frame, 16);
    g->nout++;
}

static int gain_value(bst_gen *g) {
    /* The accumulator's high byte is taken unsigned. It is a signed quantity
       everywhere else, and an arithmetic shift here agrees with the original
       for as long as it stays positive, which is nearly always; the engine
       uses a logical one. */
    int nr = g->img->t.nearest_round;
    int acc = nr ? ((int)(int16_t)(g->gain_acc + 0x80) >> 8)
                 : ((uint16_t)g->gain_acc >> 8);
    int v = g->gain_base + acc;
    if (g->exc == 0x20)      v += g->gain_adj;
    else if (g->exc == 0x10) v += g->gain_adj + 8;

    int diff = g->gain_target - acc;
    if (diff != 0 && !g->fresh) {
        int mag = diff < 0 ? -diff : diff;
        int clk = nr ? (((int)(int16_t)(g->gclock + g->dur) + 8) >> 4)
                     : (((uint16_t)(g->gclock + g->dur) >> 4) + 1);
        int step = alg(g, lg(g, mag) - lg(g, clk) + lg(g, g->dur));
        g->gain_acc = (int16_t)(g->gain_acc + (diff < 0 ? -step * 16 : step * 16));
    }
    return v;
}

static void pitch_step(bst_gen *g) {
    int nr = g->img->t.nearest_round;
    unsigned t = ((unsigned)(uint16_t)g->dur + (unsigned)(uint16_t)g->pclock);
    unsigned u = ((nr ? t + 8 : t) & 0xFFFF) >> 4;
    int small = u < 0x100;
    if (!small) u = nr ? (u + 8) >> 4 : (t & 0xFFFF) >> 8;
    int rate = lg(g, g->dur) - lg(g, (int)(int16_t)u + 1);
    int diff = nr
        ? (((int)(uint16_t)g->pitch_target + 0x10) >> 5)
              - (((int)(uint16_t)g->pitch_acc + 0x10) >> 5)
        : (int)(int16_t)(g->pitch_target >> 5) - (int)(int16_t)(g->pitch_acc >> 5);
    if (diff != 0) {
        int mag = diff < 0 ? -diff : diff;
        int step = alg(g, lg(g, mag) + rate - 0x80);
        int scale = small ? 0x20 : 2;
        g->pitch_acc = (uint16_t)(g->pitch_acc + (diff < 0 ? -step * scale : step * scale));
    }
    g->pitch_period = (int16_t)(g->pitch_acc >> 8);
    if (bst_trace)
        bst_tracef("pitch n=%d dur=%04x pclock=%04x acc=%04x target=%04x -> %d\n",
                g->nout, (uint16_t)g->dur, (uint16_t)g->pclock,
                (uint16_t)g->pitch_acc, (uint16_t)g->pitch_target, g->pitch_period);
}

/* Sixty-six in every build but the 2006 ones, which floor a period at
   sixty-two. */
static uint8_t mper(const bst_gen *g) {
    return g->img->t.min_period ? g->img->t.min_period : 0x42;
}

static void silence_frame(bst_gen *g) {
    g->frame[2] = 0;
    g->frame[3] = 0;
    g->frame[0] = g->img->t.silence_f0;
}

static void build(bst_gen *g, const uint8_t *rec) {
    int wasvoiced = g->voiced;
    int t = (rec[0] & 0x70) >> 4;

    if (t == 1 || t == 5 || t == 3) {
        g->tclass = (((rec[2] << 8) | rec[1]) >>
                     (g->img->t.class_shift ? g->img->t.class_shift : 9));
        if (g->tclass == 0 || g->tclass == 4) g->exc = g->defexc;
        else {
            g->exc = u8at(g->img, g->img->t.exctab, (unsigned)g->tclass);
            if ((g->flags & 1) && g->tclass == 2) g->exc = 0x10;
        }
    } else if (t == 0) {
        g->exc = 0;
        g->dur = g->left;
        if (bst_trace) bst_tracef("sil left=%d voiced=%d\n", g->left, g->voiced);
        int16_t keep = g->dur;
        if (g->left < g->img->t.unvoiced_dur) {
            silence_frame(g);
            g->frame[3] = (g->left >= mper(g)) ? (uint8_t)g->left : mper(g);
            g->dur = keep;
            return;
        }
        if (g->voiced == 1) {
            silence_frame(g);
            g->frame[1] = g->frame[1];
            g->frame[3] = mper(g);
            g->dur = (int16_t)(g->left - mper(g));
            emit(g);
        }
        memset(g->frame + 4, 0, 12);
        g->frame[0] = g->img->t.long_silence_f0 ? g->img->t.long_silence_f0 : 0xC8;
        g->frame[1] = 0; g->frame[2] = 0; g->frame[3] = 0;
        for (; g->dur > 0x7F8; g->dur = (int16_t)(g->dur - 0x7F8)) {
            g->frame[3] = 0xFF;
            emit(g);
        }
        if (g->img->t.long_silence_chunk) {
            /* This generation holds a long silence in whole periods rather
               than in one frame of eight-period units. */
            while (g->dur > g->img->t.unvoiced_dur) {
                g->frame[3] = (uint8_t)g->img->t.unvoiced_dur;
                emit(g);
                g->dur = (int16_t)(g->dur - g->img->t.unvoiced_dur);
            }
            g->frame[0] = g->img->t.silence_f0;
            if (g->dur < mper(g)) {
                g->frame[3] = mper(g);
                g->dur = g->left;
                return;
            }
        } else if (g->dur < g->img->t.unvoiced_dur) {
            g->frame[0] = g->img->t.silence_f0;
            if (g->dur < mper(g)) {
                g->frame[3] = mper(g);
                g->dur = g->left;
                return;
            }
        } else {
            g->dur = (int16_t)((g->dur & ~0xFF) | ((g->dur >> 3) & 0xFF));
        }
        g->frame[3] = (uint8_t)g->dur;
        g->dur = g->left;
        return;
    }

    g->voiced = u8at(g->img, g->img->t.classtab, (unsigned)g->tclass);
    if ((g->flags & 1) && g->tclass == 2) g->voiced = 1;

    if (g->voiced == 1) {
        if (wasvoiced != 1) {
            silence_frame(g);
            g->frame[3] = mper(g);
            emit(g);
        }
        g->dur = g->pitch_period;
        if (g->pitch_period < 0x100) { if (g->pitch_period < 0x14) g->dur = 0x14; }
        else g->dur = 0xFF;
    } else {
        if (wasvoiced == 1) {
            silence_frame(g);
            g->frame[3] = mper(g);
            emit(g);
        }
        g->dur = g->img->t.unvoiced_dur;
    }

    int reps = g->voiced == 1 ? 1 : g->img->t.unvoiced_reps;
    if (g->exc < 0x30) {
        if (g->exc == 0x20)      g->frame[0] = (uint8_t)(0xE0 | reps);
        else if (g->img->t.exc_two_way) g->frame[0] = (uint8_t)(0xD0 | reps);
        else                     g->frame[0] = (uint8_t)((g->exc == 0x10 ? 0xF0 : 0xC0) | reps);
    } else g->frame[0] = (uint8_t)(0xD0 | reps);

    uint8_t f0 = g->frame[0];
    int nofrac = 0;
    if (g->img->t.unvoiced_chunk) {
        /* Composed the same way whether voiced or not; the split happens on
           the way out. */
        g->frame[3] = (uint8_t)g->dur;
        g->frame[0] = f0;
        g->frame[1] = (uint8_t)((g->pitch_acc >> 4) & 0x0F);
    } else if (g->exc < 0x30) {
        nofrac = 1;
        g->frame[1] = 0; g->frame[2] = 0; g->frame[3] = 0;
        /* The frame lasts as long either way; a build that chunks it says so
           in the count, and the period it holds is the chunk. */
        g->frame[3] = (uint8_t)(g->dur / reps);
    } else {
        g->frame[3] = (uint8_t)g->dur;
        g->frame[0] = f0;
        g->frame[1] = (uint8_t)((g->pitch_acc >> 4) & 0x0F);
    }

    /* The interpolation: each frame moves the running state a fraction of the
       way to the target, the fraction being this frame's length over the time
       the transition has left. */
    if (!g->fresh) {
        if ((int)(uint16_t)g->dur < (int)g->left) {
            if ((rec[0] & 0x70) != 0x10) {
                int rate = lg(g, g->dur)
                         - lg(g, (int)((uint16_t)((g->left - (g->left >> 2)) + g->dur) >> 5) + 1)
                         - 0x80;
                unsigned m = g->img->t.interp_round_mask;

                for (int i = 0; i < BST_ORDER; i++) {
                    int r = (m >> i) & 1;
                    int d = (int16_t)(g->targets[i] - g->state[i]);
                    if (d < 0)
                        g->state[i] = (int16_t)(g->state[i] - alg(g, lg(g, ((-d) + r) >> 1) + rate));
                    else
                        g->state[i] = (int16_t)(g->state[i] + alg(g, lg(g, (d + r) >> 1) + rate));
                }
            }
        } else {
            memcpy(g->state, g->targets, sizeof g->state);
        }
    } else {
        memcpy(g->state, g->prev, sizeof g->state);
    }

    if (bst_trace)
        bst_tracef("P acc=%04x tgt=%04x pclk=%04x dur=%d\n",
                (unsigned)(uint16_t)g->pitch_acc, (unsigned)(uint16_t)g->pitch_target,
                (unsigned)(uint16_t)g->pclock, (int)g->dur);
    if (0)
        bst_tracef("Q acc=%04x tgt=%04x pclk=%04x dur=%d\n",
                (unsigned)(uint16_t)g->pitch_acc, (unsigned)(uint16_t)g->pitch_target,
                (unsigned)(uint16_t)g->pclock, (int)g->dur);
    for (int i = 0; i < BST_ORDER; i++) {
        int v = g->state[i];
        if (g->img->t.coef_round_mask & (1u << i))
            v = v < 0 ? ((v - 1) >> 1) : ((v + 1) >> 1);
        else
            v = v >> 1;
        g->frame[4 + i] = (uint8_t)v;
    }
    if (g->state[0] & 1) g->frame[1] |= 0x80;
    g->nofrac = nofrac;
    g->frame[2] = (uint8_t)gain_value(g);
    pitch_step(g);
    g->fresh = 0;
}

/* ---- the driver -------------------------------------------------------- */

int bst_generate(bst_gen *g) {
    g->voiced = 0;
    g->qcur = 0;
    g->lastclass = 0;
    memset(g->frame, 0, sizeof g->frame);
    load_rate(g, g->rate);

    if (ito_take(g, &g->inton) != 2) { if (bst_trace) bst_tracef("exit1\n"); return g->nout; }

    int qi = -1;
    int first = 1;
    g->left = 0; g->pclock = 0; g->segleft = 0; g->mid = 0;

    for (int guard = 0; guard < 200000 && !g->failed; guard++) {
        int consumed = 0;
        while (g->segleft < 1) {
            bst_seg_rec *r = seg_peek(g);
            if (!r) { if (bst_trace) bst_tracef("exit2\n"); return g->nout; }
            int16_t was = g->cur.count;
            if ((r->index & 0xF000) == 0) {
                if (seg_take(g, &g->cur) != 2) { if (bst_trace) bst_tracef("exit3\n"); return g->nout; }
                if (g->inton.kind == 1 && g->pclock < 1 && g->segleft < 1 && g->left < 1)
                    { if (bst_trace) bst_tracef("exit4\n"); return g->nout; }
                g->cur.count = (int16_t)(g->cur.count + was);
                int v = g->cur.b + g->segleft;
                if (v < 1) v = 0;
                consumed++;
                g->mid = (int16_t)v;
                g->segleft = (int16_t)(g->segleft + g->cur.a);
            } else {
                /* An embedded command: it changes the voice, not the sound. */
                int op = r->index & 0xFF;
                int v1 = r->a;
                if (op == 0x65) {
                    g->defexc = v1 * 0x10;
                    if (g->defexc < 1) g->defexc = 0x30;
                } else if (op == 0x67) {
                    g->gain_base = (v1 > -0x47 && v1 < 0x15) ? v1 + 0x10 : 0x10;
                } else if (op == 0x72) {
                    load_rate(g, v1);
                } else if (op == 0x75) {
                    g->gain_adj = (v1 > -0x47 && v1 < 0x15) ? v1 - 0x12 : -0x12;
                } else if (op == 0x76) {
                    if (v1 > 0 && v1 < 7) g->voice = v1;
                    else if (v1 == 0) g->voice = 1;
                }
                g->segrd++;
                if (g->segrd > 0xCF) g->segrd -= 0xCF;
            }
        }

        if (g->pclock < 1) {
            int16_t was;
            do {
                was = g->inton.dur;
                g->inton.dur = (int16_t)(g->inton.dur + g->pend.dur);
                g->pend.kind = g->inton.kind;
                g->pend.period = g->inton.period;
                g->pend.dur = g->inton.dur;
                g->pend.slope = g->inton.slope;
                if (g->pend.kind != 1 && ito_take(g, &g->inton) != 2) { if (bst_trace) bst_tracef("exit5\n"); return g->nout; }
            } while (was == 0 && g->pend.slope == 0 && g->pend.kind != 1);
            g->pitch_period = g->pend.period;
            g->pitch_acc = (uint16_t)(g->pend.period << 8);
        }

        if (consumed != 0) {
            if (bst_trace)
                bst_tracef("step consumed=%d pend.dur=%d segleft=%d\n",
                        consumed, (int)g->pend.dur, (int)g->segleft);
            g->pend.dur = (int16_t)(g->pend.dur - consumed);
            /* A record whose time ran out mid-segment is held at the value
               that makes the glide follow what is left of the segment rather
               than the record. Overshooting it is ordinary, not an error. */
            if (g->pend.dur < -1) g->pend.dur = -1;
            interp_clock(g);
        }
        if (g->pclock < 1 && interp_clock(g) == -2) break;

        while (g->left < 1) {
            qi = next_record(g, qi, first);
            first = 0;
            if (qi < 0) { if (bst_trace) bst_tracef("exit7\n"); return g->nout; }
            if (seg_entry(g, g->q[qi].rec) == -2) { if (bst_trace) bst_tracef("exit8\n"); return g->nout; }
        }

        build(g, g->q[qi].rec);

        if (g->left < g->dur) {
            int16_t add = (int16_t)(g->dur - g->left);
            g->left = g->dur;
            g->pclock = g->pclock + add;
            g->segleft = (int16_t)(g->segleft + add);
            g->gclock = (int16_t)(g->gclock + add);
            if (g->voiced) g->mid = (int16_t)(g->mid + add);
        }
        g->left = (int16_t)(g->left - g->dur);
        if (g->voiced) g->mid = (int16_t)(g->mid - g->dur);
        g->gclock = (int16_t)(g->gclock - g->dur);
        g->pclock = g->pclock - g->dur;
        g->segleft = (int16_t)(g->segleft - g->dur);

        /* A short frame is held for several pitch periods rather than emitted
           over and over. */
        if (g->dur < 0x37 && g->voiced) {
            uint16_t acc = (uint16_t)g->dur;
            while (acc < 0x37 && g->dur < g->left) {
                g->frame[0]++;
                g->gclock = (int16_t)(g->gclock - g->dur);
                g->pclock = g->pclock - g->dur;
                g->segleft = (int16_t)(g->segleft - g->dur);
                g->mid = (int16_t)(g->mid - g->dur);
                g->left = (int16_t)(g->left - g->dur);
                acc = (uint16_t)(acc + g->dur);
            }
        }

        if (g->pclock < 1 && g->mid == g->pclock && g->segleft > 0 &&
            interp_clock(g) == -2) { fail(g); { if (bst_trace) bst_tracef("exit9\n"); return g->nout; } }
        if (g->done) { if (bst_trace) bst_tracef("exit10\n"); return g->nout; }
        emit(g);
    }
    { if (bst_trace) bst_tracef("exit11\n"); return g->nout; }
}
