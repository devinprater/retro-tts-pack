#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* The intonation contour: accent codes to pitch records.
 *
 * The voice keeps a fourteen-entry pitch table spanning its range, built from
 * a base, a middle and a top. An accent code is an index into it, offset by
 * 0x3D, so a code names a pitch level and the table turns it into a frequency;
 * the record carries the period rather than the frequency, at the engine's
 * fixed sample rate.
 *
 * The generator walks the accents left to right emitting one record each. Its
 * duration is the number of segments between this accent and the next, less
 * an adjustment when the next one sits on a phrase boundary, and its slope
 * says how the pitch travels there: a fall to a boundary, a step between
 * accents, or a glide when nothing intervenes. Commands embedded in the stream
 * between accents retune the range as they are passed, which is why the walk
 * carries a stream cursor alongside the accent index. */

#define ACC_MAX 96
#define RATE    0x2B11        /* the period numerator: 11025 */

static int rate_of(const bst_image *img) {
    return img->t.pitch_rate ? img->t.pitch_rate : RATE;
}
#define KEEP    (-0x6B)       /* "leave this one alone" */

typedef struct { short pos; signed char code, flag; } acc;

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void build(bst_voice *p, int base, int mid, int top) {
    p->mid = mid;
    p->hi = top;
    p->table[0] = (short)base;
    /* A third of the way from the base to the middle. The 2006 builds reach
       it as five sixteenths rounded, which is a shade over. */
    short d = p->round10 ? (short)((5 * (mid - base) + 8) >> 4)
                         : (short)((mid - base) / 3);
    p->table[1] = (short)(base + d);
    p->table[2] = (short)(mid - d);
    p->table[3] = (short)mid;
    /* The ten steps from the middle to the top. A tenth in the earlier
       builds; thirteen over a hundred and twenty-eight, rounded, in the
       2006 ones, which is not the same number. */
    if (p->round10) {
        short step = (short)(13 * (short)(top - mid));
        short acc = (short)(step + 64);
        for (int u = 4; u < 14; u++) {
            p->table[u] = (short)(p->table[3] + (acc >> 7));
            acc = (short)(acc + step);
        }
    } else {
        for (int u = 4; u < 14; u++)
            p->table[u] = (short)(p->table[3] +
                                  ((u - 3) * (short)(top - mid)) / 10);
    }
}

/* The proportional form: a percentage above the base for the middle and above
   the stored top for the top. */
static void set_range(bst_voice *p, int a, int b) {
    short s1 = (short)(p->base + (p->base >> 2));
    /* The 2006 builds reach the percentage by a shift of seven, so what the
       earlier ones write as four in a hundred is five in a hundred and
       twenty-eight here, and ten is thirteen. */
    int mid = p->round10 ? s1 + (short)((short)(s1 * a) >> 7)
                         : s1 + (s1 * a) / 100;
    if (mid < p->base) mid = p->base;
    if (mid > 600) mid = 600;
    short s2 = p->round10 ? (short)(p->top + (short)((short)(p->top * a) >> 7))
                          : (short)((p->top * (100 + a)) / 100);
    int top = p->reach ? p->reach
            : p->round10 ? s2 + (short)((short)(s2 * b) >> 7)
                         : s2 + (s2 * b) / 100;
    if (top < mid) top = mid;
    if (top > 600) top = 600;
    build(p, p->base, mid, top);
}

/* The absolute form, as the stream's own commands give it. */
static void set_abs(bst_voice *p, int base, int mid, int level, int top) {
    if (base != KEEP && (base < 0x2B || (p->base = base, base > 600)))
        p->base = p->voicebase;
    if (mid != KEEP) {
        if (mid == 0) p->mid = p->base + (p->base >> 2);
        else {
            int v = (((mid * 0x14) / 100 + 100) * p->base) / 0x50;
            if (v < 0x2B) v = 0x2B;
            else if (v > 600) v = 600;
            p->mid = v;
        }
    }
    if (level != KEEP && (level < -3 || (p->level = level, level > 10)))
        p->level = 3;
    if (top != KEEP) {
        if (top == 0) p->hi = p->base * 2;
        else {
            int v = ((top + 200) * p->base) / 100;
            if (v < 0x2B) v = 0x2B;
            else if (v > 600) v = 600;
            p->hi = v;
        }
        p->top = p->hi;
    }
    build(p, p->base, p->mid, p->hi);
}

static int freq(const bst_voice *p, int code) {
    return p->table[3 + clampi(code, -3, p->levmax)];
}

/* Narrows the range when an emphasis command is in force and the span is wide
   enough to be worth narrowing. */
static void compress(bst_voice *p) {
    int sc = (p->emphasis != 1) ? p->emphasis : 0x57;
    if (p->mid == 0 || (p->hi * 100) / p->mid <= 0x6D) return;
    p->hi = p->mid + ((p->hi - p->mid) * sc) / 100;
    short save = p->table[3 + clampi(p->level, -3, 10)];
    short span = (short)(p->hi - p->mid);
    p->table[3] = (short)p->mid;
    for (int u = 4; u < 14; u++)
        p->table[u] = (short)(p->table[3] + ((u - 3) * span + p->round10) / 10);
    int idx = 3 + clampi(p->level, -3, 10);
    p->table[idx] = (save <= p->table[8]) ? save : p->table[8];
}

static void command(bst_voice *p, const uint8_t *b) {
    int v1 = (b[1] << 8) | b[2];
    int v2 = (b[3] << 8) | b[4];
    switch (b[0]) {
    case 0x44: p->emphasis = (signed char)v1; return;
    case 0x50: set_range(p, v1, v2); return;
    case 0x66: set_abs(p, v1, 0, 3, 0); return;
    case 0x68: set_abs(p, KEEP, KEEP, KEEP, v1); return;
    case 0x6C: set_abs(p, KEEP, v1, KEEP, KEEP); return;
    case 0x76:
        if ((v1 < 1 || v1 > 6) && v1 == 0) p->voice = 1;
        else if (v1 >= 1 && v1 <= 6) p->voice = v1;
        set_abs(p, p->voicebase, 0, 3, 0);
        return;
    default: return;
    }
}

/* How many segments lie between two positions. */
static int segs_between(const bst_image *img, const uint8_t *s, int len, int a, int b) {
    bst_cur c;
    int n = 0;
    bst_scan_seg(img, s, len, a - 1, &c);
    while (c.pos != 0) {
        if (b < c.pos) break;
        n++;
        bst_scan_seg(img, s, len, c.pos, &c);
    }
    return n;
}

/* How many sounds after this one carry a spectral target of their own. */
static int run_after(const bst_image *img, const uint8_t *s, int p) {
    int q = p + 1;
    while (bst_ph_attr1(img, s[q]) & 0x80) q++;
    return q - (p + 1);
}

static void scan_back_seg(const bst_image *img, const uint8_t *s, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q, b;
        do {
            q = p; p = q - 1;
            if (p < 0) { c->val = 0; c->pos = 0; return; }
            b = s[p];
        } while (b == 0);
        if (bst_ph_attr2(img, b) & 1) { c->val = b; c->pos = p; return; }
        if (b == 0x7C) { p = q - 7; continue; }
        if (p < 1 || b == 0x4D || b == 0x4E) { c->val = 0; c->pos = 0; return; }
    }
}

/* How far back the last sound with a target of its own lies. */
/* What counts as the sound the run stops at. The Russian build asks its own
   question of the attribute byte rather than reading the open-sound bit. */
static int run_stop(const bst_image *img, int c) {
    int a = bst_ph_attr1(img, c);
    if (img->t.run_stop_kind == 1) return (a & 4) && !(a & 0x42);
    if (!(a & 0x80)) return 0;
    /* One sound carries a target of its own and still does not stop the walk,
       so a word built on it has the run reach back past the word and come out
       empty. French names its schwa here and German its own; most builds name
       nothing. */
    if (img->t.run_stop_skip && c == img->t.run_stop_skip) return 0;
    return 1;
}

static int run_before(const bst_image *img, const uint8_t *s, int p) {
    int n = 0;
    for (;;) {
        bst_cur c;
        scan_back_seg(img, s, p, &c);
        p = c.pos;
        if (c.val == 0) return 0;
        if (run_stop(img, s[c.pos])) return n;
        n++;
    }
}

int bst_contour(const bst_image *img, const uint8_t *s, int len,
                bst_voice *p, bst_contour_rec *out, int max) {
    acc e[ACC_MAX];
    int n = -1, carry = 0, seen = 0, nrec = 0;
    bst_cur eight, strong, prev8, prevstrong;

    p->round10 = img->t.contour_round ? 5 : 0;
    p->reach = img->t.voice_top;
    /* The table's own bound, not a build's. Where a build holds its accent
       codes lower than the rest, it does so as the code is chosen; by the
       time one is looked up here it means what it says. */
    p->levmax = 10;

    memset(e, 0, sizeof e);
    p->emphasis = 0;

    int mode = img->t.inton_mode ? img->t.inton_mode
                                 : ((s[6] == 'I') ? s[10] : 3);
    int a, b;
    int wide = img->t.contour_round ? 13 : 10;
    int step = img->t.contour_round ? 5 : 4;
    switch (mode) {
    case 1: b = step;  a = 0;     break;
    case 2: b = 0;     a = 0;     break;
    case 3: b = -step; a = 0;     break;
    case 4: b = -step; a = wide;  break;
    case 5: b = step;  a = wide;  break;
    case 6: b = 0;     a = -wide; break;
    case 7: b = 0;     a = wide;  break;
    default: b = 0; a = 0;   break;
    }
    set_range(p, b, a);

    bst_scan_eight_back(img, s, len, &prev8);
    int last = prev8.pos;
    bst_scan_eight(img, s, len, -1, &eight);
    strong.val = p->strong;
    strong.pos = 0;
    prev8.val = prev8.pos = 0;
    prevstrong.val = prevstrong.pos = 0;

    for (int i = 0; i < last; i++) {
        if (eight.pos - i == -1) { prev8 = eight; bst_scan_eight(img, s, len, i, &eight); }
        if (strong.pos - i == -1) {
            prevstrong = strong;
            bst_scan_strong(img, s, len, i, &strong);
        }
        int c = s[i];
        if (c == 0x7C) { i += 6; continue; }
        if (!(bst_ph_attr2(img, c) & 4) || c == 0x48) continue;
        if (n + 1 >= ACC_MAX) break;
        n++;
        e[n].code = (signed char)(c - 0x3D);
        int v = (prev8.pos < prevstrong.pos) ? prevstrong.pos : prev8.pos;
        e[n].pos = (short)v;
        if (v != prevstrong.pos) e[n].flag = 1;
        else {
            e[n].flag = 2;
            if (prevstrong.pos - i != -3) e[n].flag = 1;
        }
    }

    n++;
    if (n >= ACC_MAX) return 0;
    e[n].code = (bst_ph_attr2(img, s[last + 3]) & 4)
              ? (signed char)(s[last + 3] - 0x3D) : 4;
    e[n].pos = (short)last;
    int fin = n;

    /* Commands before the first accent are in force for it. */
    int cur = e[0].pos;
    while (cur < e[1].pos) {
        if (s[cur] == 0x7C) { command(p, s + cur + 1); cur += 6; }
        cur++;
    }

    for (int k = 0; k < n; k++) {
        if (strong.pos == k) bst_scan_strong(img, s, len, cur, &strong);

        int here = e[k].pos, next = e[k + 1].pos;
        int dur = next - here;
        if (dur > 1) {
            dur = segs_between(img, s, len, here + 1, next - 1) + 1;
            if (bst_ph_attr2(img, s[next]) & 8) dur--;
        }
        dur += carry;
        carry = 0;

        int slope;
        if (bst_ph_attr2(img, s[next]) & 8) {
            if (img->t.run_before_always ||
                (bst_ph_attr1(img, s[here]) & 0x80)) {
                carry = run_before(img, s, next);
                dur -= carry;
            }
            slope = 0x55;
        } else {
            if (!(bst_ph_attr1(img, s[next]) & 0x80)) return nrec;
            if (dur == 0) {
                bst_scan_strong(img, s, len, here, &strong);
                int t = run_after(img, s, here);
                if (!(bst_ph_attr2(img, s[e[k + 2].pos]) & 8) || t != 0 ||
                    (strong.val != 0 && strong.pos <= e[k + 2].pos))
                    slope = 0x50;
                else
                    slope = 0x2D;
            } else {
                slope = (e[k + 1].flag == 2) ? 0x50 : 5;
            }
        }

        /* The second and later accents of an emphasised phrase pull the range
           in, so the contour does not keep climbing. */
        if (p->emphasis != 0 && e[k].code > 4) {
            if (seen) {
                if (e[k + 1].flag == 1 ||
                    (e[k + 1].flag == 2 && (k > 0 && e[k - 1].code < 1) &&
                     (bst_ph_attr1(img, s[here]) & 0x80)))
                    compress(p);
            } else seen = 1;
        }

        int f = freq(p, e[k].code);
        int period = f ? rate_of(img) / f : 0;
        if (bst_trace)
            bst_tracef("con k=%d code=%d pos=%d flag=%d dur=%d slope=%02x"
                       " period=%d\n", k, e[k].code, e[k].pos, e[k].flag,
                       dur, slope, period);
        if (nrec < max) {
            out[nrec].kind = 2;
            out[nrec].period = (uint8_t)period;
            out[nrec].dur = (int16_t)dur;
            out[nrec].slope = (int16_t)slope;
        }
        nrec++;

        cur = here;
        while (k > 0 && cur < next) {
            if (s[cur] == 0x7C) { command(p, s + cur + 1); cur += 6; }
            cur++;
        }
    }

    int f = freq(p, e[fin].code);
    int period = f ? rate_of(img) / f : 0;
    if (bst_trace)
        bst_tracef("con fin=%d code=%d pos=%d carry=%d period=%d\n",
                   fin, e[fin].code, e[fin].pos, carry, period);
    if (carry != 0) {
        if (nrec < max) {
            out[nrec].kind = 2;
            out[nrec].period = (uint8_t)period;
            out[nrec].dur = (int16_t)carry;
            out[nrec].slope = 0x55;
        }
        nrec++;
    }
    if (nrec < max) {
        out[nrec].kind = 1;
        out[nrec].period = (uint8_t)period;
        out[nrec].dur = 0;
        out[nrec].slope = 0;
    }
    nrec++;
    p->strong = strong.val;
    return nrec;
}
