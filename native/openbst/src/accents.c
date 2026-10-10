#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* Accent assignment: writing the pitch-target codes into the sentence stream.
 *
 * Every group opener in the stream reserves two empty slots after it, and this
 * pass fills some of them with codes in the range 0x3A to 0x48. The contour
 * generator reads them back as offsets from 0x3D into a fourteen-entry pitch
 * table, so a code is a pitch level rather than a frequency.
 *
 * Which accents get codes is decided in two sweeps. The first walks the stream
 * collecting one entry per group opener, classifying each by the mark that
 * follows it and by the emphasis commands in force, and picking out three
 * landmarks: the first accent that counts, the last, and the last of all. The
 * second walks from the first landmark to the last writing codes, with the
 * sentence type -- statement, question, exclamation, carried from the type
 * command at the head of the stream -- choosing between rise and fall at each
 * step and at the end. */

#define ACC_MAX 64

typedef struct { short pos; signed char kind; } acc;

static int cmd16(const uint8_t *s, int i) { return (s[i + 2] << 8) | s[i + 3]; }

/* Backward to the previous segment, giving up at the start of the stream or
   at a phrase marker. */
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

/* Forward to the next sound that opens a group. The end of the stream and a
   phrase marker both count as nothing at all. */
static void scan_fwd_seg(const bst_image *img, const uint8_t *s, int len,
                         int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int b;
        do {
            p++;
            if (p >= len) { c->val = 0; c->pos = 0; return; }
            b = s[p];
        } while (b == 0);
        if (bst_ph_attr2(img, b) & 1) { c->val = b; c->pos = p; return; }
        if (b == 0x7C) { p += 6; continue; }
        if (p >= len - 1 || b == 0x4D || b == 0x4E) {
            c->val = 0; c->pos = 0; return;
        }
    }
}

/* Backward to the previous group marker, the companion of scan_back_seg that
   the French build uses to decide whether a landmark sits inside a group. */
static void scan_back_grp(const bst_image *img, const uint8_t *s, int from,
                          bst_cur *c) {
    int p = from;
    for (;;) {
        int q, b;
        do {
            q = p; p = q - 1;
            b = p >= 0 ? s[p] : 0x4E;
        } while (b == 0);
        if (bst_ph_attr2(img, b) & 8) { c->val = b; c->pos = p; return; }
        if (b == 0x7C) { p = q - 7; continue; }
        if (p < 0) { c->val = 0; c->pos = 0; return; }
    }
}

/* Whether the next marker after this position is the one that mutes a
   function word rather than a group or phrase boundary. */
static int weak_word_next(const bst_image *img, const uint8_t *s, int len, int i) {
    int p = i + 1;
    int c = p < len ? s[p] : 0;
    if (bst_ph_attr2(img, c) & 8) return 0;
    for (;;) {
        if (p >= len) return 0;
        if (c == 0x7C)      p += 6;
        else if (c == 0x51) return 1;
        p++;
        c = p < len ? s[p] : 0;
        if (bst_ph_attr2(img, c) & 8) return 0;
    }
}

/* The French build's accent pass, which is a different algorithm rather than
   the English one with other constants.
 *
 * It keeps no per-accent classification. It walks the stream noting three
 * things -- the accent the phrase's last group opens on, the one a tilde
 * command names, and the one an accent command names -- and then writes at
 * most three codes: a rise on the syllable before the last group's, the
 * shape code on the last group's own, and the sentence type's code on the
 * last accent of all. Everything else is left flat, which is why a French
 * phrase carries so much less contour than an English one. */
static int accents_french(const bst_image *img, uint8_t *s, int len,
                          bst_accent_state *st) {
    short pos[ACC_MAX];
    int n = 0;
    int last = 0, latest = 0, marked = 0, first = 0;
    int w = 0, wmark = 0, kind = 0;
    int code = 0, rise = 1, pending = 0, seen = 0, tcode = 0;
    int out = 0, type;

    memset(pos, 0, sizeof pos);
    st->emphasis = 0;

    if (st->flags & 8) {
        if (s[4] == 0) s[4] = 0x41;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = 0x41;
        return 0;
    }

    if (s[6] != 'I') {
        type = 0x12;
        code = 0x3D;
    } else {
        type = s[8] + 0x0E;
        if (s[9] == 0x48)      code = 0x44;
        else if (s[9] == 0x4C) code = 0x3D;
        else if (s[9] == 0x4D) code = 0x41;
    }

    bst_cur lastseg;
    scan_back_seg(img, s, len - 6, &lastseg);
    int voiced = lastseg.val != 0x2F;

    for (int i = 0; i < len; i++) {
        int b = s[i];
        if (b == 0x7C) {
            if (s[i + 1] == '~') {
                int v = cmd16(s, i);
                int w2 = (s[i + 4] << 8) | s[i + 5];
                if (v == 0x1D) {
                    tcode = s[i + 5];
                    pending = 1;
                    marked = n + 1;
                } else if (v == 0x20 || v == 0x21) {
                    if (w2 == 0x48)      code = 0x44;
                    else if (w2 == 0x4C) code = 0x3D;
                    else if (w2 == 0x4D) code = 0x41;
                    first = n + 1;
                    kind = v == 0x21 ? 2 : 1;
                    seen = 1;
                }
            }
            i += 6;
            if (bst_ph_attr2(img, s[i + 1]) & 8) i++;
            continue;
        }
        if (bst_ph_attr2(img, b) & 8) {
            int go = 0;
            w++;
            if (seen || last != 0) {
                go = first != 0;
            } else if (first != 0) {
                go = 1;
            } else if (!weak_word_next(img, s, len, i) && latest == 0) {
                if (type == 0x11) { first = n + 1; kind = 2; }
                else if (type == 0x17) {
                    wmark = w; code = 0x44; kind = 1; first = n + 1;
                }
            }
            if (go && n != 0) {
                if (kind == 2) {
                    bst_cur c1, c2;
                    scan_back_seg(img, s, first, &c1);
                    scan_back_grp(img, s, first, &c2);
                    if ((bst_ph_attr1(img, c1.val) & 1) && c1.pos > c2.pos) first++;
                    last = first;
                } else if (s[pos[n] - 1] == 0x26 && n > first) {
                    last = n - 1;
                } else {
                    last = n;
                }
                first = 0;
            }
            if (pending) {
                if (n != 0) {
                    if (tcode == 0x12) {
                        latest = (s[pos[n] - 1] == 0x26 && n > marked) ? n - 1 : n;
                    } else {
                        bst_cur c1, c2;
                        scan_back_seg(img, s, marked, &c1);
                        scan_back_grp(img, s, marked, &c2);
                        if ((bst_ph_attr1(img, c1.val) & 1) && c1.pos > c2.pos) marked++;
                        latest = marked;
                    }
                }
                pending = 0;
            }
        } else if (bst_ph_attr1(img, b) & 0x80) {
            if (n + 1 >= ACC_MAX) break;
            n++;
            pos[n] = (short)(i + 1);
            i += 3;
        }
    }
    w--;

    if (last != 0 && !seen && latest == 0 && (type == 0x11 || type == 0x17)) {
        /* A word whose every stress mark has been used takes the level below
           the one a phrase reaches. The 1998 module holds that level lower
           than the 2006 one does, and the cap belongs here rather than on
           every lookup: the level a phrase carries is higher still and must
           not be pulled down with it. */
        int own = 0x3D + (img->t.level_max ? img->t.level_max : 5);
        if (own > 0x42) own = 0x42;
        if (n - last + 1 > 4) {
            if (kind == 2) { if (type == 0x11) { type = 0x16; code = 0x44; } }
            else if (w == wmark) code = own;
        } else if (kind == 2) {
            last = 0;
        } else if (w == wmark) {
            code = own;
        }
    }

    if (n == 0) {
        if (s[4] == 0) s[4] = 0x41;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = 0x41;
        st->carried = 0;
        return 0;
    }

    if (last == 0) { if (latest != 0) last = latest; }
    else if (latest != 0 && last > latest) last = latest;

    if (last < 2) {
        out = code;
        if (code != 0x3D) rise = 0;
    } else {
        out = 0x3D;
        s[pos[last - 1] + 1] = 0x3D;
        rise = 0;
        if (latest == 0 || last < latest) s[pos[last] + 1] = (uint8_t)code;
    }

    if (s[4] == 0) {
        if (st->carried) {
            s[4] = (uint8_t)st->carried;
            if (s[0] == 0x4E && st->carried != 0x3D && last != 1 && latest != 1)
                s[pos[1] + 1] = 0x3D;
        } else {
            s[4] = (uint8_t)out;
        }
    }

    if (pending || tcode) {
        if (tcode == 0x10 || tcode == 0x17) {
            s[pos[latest] + 1] = (uint8_t)(tcode == 0x10 ? 0x44 : 0x41);
            if (latest == n) s[pos[latest] + 2] = 0x3D;
            else             s[pos[latest + 1] + 1] = 0x3D;
            if (rise) { s[pos[latest - 1] + 1] = 0x3D; rise = 0; }
        } else if (tcode == 0x12) {
            s[pos[latest] + 1] = 0x3C;
        }
    }

    int dx, cx;
    if (s[pos[n] - 1] == 0x26 && n > 1) {
        dx = n - 1;
        cx = n <= 2 ? n - 1 : n - 2;
    } else {
        dx = n;
        cx = n > 1 ? n - 1 : n;
    }

    int slot = 0, slotcode = 0;
    int gap = latest < cx && last < cx && cx < n;
    switch (type) {
    case 0x0F: out = 0x46; break;
    case 0x10:
        if (gap) { slot = pos[cx] + 1; slotcode = 0x44; }
        out = voiced ? 0x3D : 0x3A;
        if (rise && cx > 1) s[pos[cx - 1] + 1] = 0x3D;
        break;
    case 0x11:
        slotcode = 0x3D;
        if (tcode == 0x10)  slot = pos[dx] + 1;
        else if (gap)       slot = pos[cx] + 1;
        out = 0x46;
        break;
    case 0x12: out = voiced ? 0x3D : 0x3A; break;
    case 0x13:
        if (gap) { slot = pos[cx] + 1; slotcode = 0x3D; }
        out = 0x40;
        break;
    case 0x14: out = 0x40; break;
    case 0x15:
        if (gap) { slot = pos[cx] + 1; slotcode = 0x44; }
        out = 0x40;
        if (rise && cx > 1) s[pos[cx - 1] + 1] = 0x3D;
        break;
    case 0x16:
        if (gap) { slot = pos[cx] + 1; slotcode = 0x41; }
        out = 0x46;
        if (rise && cx > 1) s[pos[cx - 1] + 1] = 0x3D;
        break;
    case 0x17:
        if (latest < dx && (last < dx || last == 1)) s[pos[dx] + 1] = 0x41;
        out = voiced ? 0x3D : 0x3A;
        if (rise && dx > 1) s[pos[dx - 1] + 1] = 0x3D;
        break;
    default: break;
    }
    if (slot) s[slot] = (uint8_t)slotcode;

    if (bst_trace)
        bst_tracef("acc n=%d last=%d latest=%d w=%d/%d code=%02x type=%02x"
                   " dx=%d cx=%d out=%02x\n",
                n, last, latest, w, wmark, code, type, dx, cx, out);

    if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
        s[st->tail] = (uint8_t)out;
    st->carried = voiced ? out : 0;
    return n;
}

/* The Japanese build's accent pass, which is again its own algorithm.
 *
 * It keeps three landmarks rather than a classification: the first accent
 * whose sound carries a formant target, the next one after it, and the first
 * whose mark is anything but the reduced one. The contour is written on the
 * first two of those and, for a long phrase, on one more chosen by how far
 * apart the first two sit. */
static int accents_japanese(const bst_image *img, uint8_t *s, int len,
                            bst_accent_state *st) {
    short pos[ACC_MAX];
    int n = 0, first = 0, l14 = 0, l24 = 0;
    int flagA = 0, flagB = 0, flagT = 0, emph = 0;
    int type, shape = 0x48;

    memset(pos, 0, sizeof pos);
    st->emphasis = 0;

    if (st->flags & 8) {
        int c = st->level + 0x3D;
        if (s[4] == 0) s[4] = (uint8_t)c;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = (uint8_t)c;
        return 0;
    }

    if (s[6] != 'I') { type = 0x12; shape = 0x48; }
    else             { type = s[8] + 0x0E; shape = s[9]; }

    bst_cur lastseg;
    scan_back_seg(img, s, len - 6, &lastseg);
    int voiced = lastseg.val != 0x2F;

    for (int i = 12; i < len; i++) {
        int b = s[i];
        if (b == 0x7C) {
            if (s[i + 1] == 'D') emph = cmd16(s, i) >= 1;
            else if (s[i + 1] == '~' && cmd16(s, i) == 0x21)
                flagT = ((s[i + 4] << 8) | s[i + 5]) == 0x48;
            i += 6;
            if (bst_ph_attr2(img, s[i + 1]) & 8) i++;
            continue;
        }
        int a2b = bst_ph_attr2(img, b);
        if (a2b & 0x10) {
            if (b == 0x51)      emph |= 1;
            else if (b == 0x52) emph |= 6;
            continue;
        }
        if (a2b & 8) { emph = 0; continue; }
        if (!(bst_ph_attr1(img, b) & 0x80)) continue;
        if (n + 1 >= ACC_MAX) break;
        n++;
        i++;
        pos[n] = (short)i;
        int mark = s[i];
        if (!(emph & 1)) {
            int open = s[i - 1];
            if (first == 0 && (bst_ph_attr1(img, open) & 4)) first = n;
            if (l24 == 0 && first != 0 && n > 1) {
                if (!(bst_ph_attr1(img, open) & 4)) continue;
                if (i - pos[first] == 4)
                    { if (s[pos[first] - 1] == open) flagA = 1; else flagB = 1; }
                l24 = n;
            }
            if (l14 == 0 && mark >= 0x33) l14 = n;
        }
        if (l24 == 0 && l14 != 0) l24 = l14;
    }

    if (n == 0) {
        int c = st->carried ? st->carried : st->level + 0x3D;
        if (s[4] == 0) s[4] = (uint8_t)c;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = (uint8_t)c;
        return 0;
    }

    /* What stands after the last accent, not before it. */
    bst_cur back;
    scan_fwd_seg(img, s, len, pos[n], &back);
    int carry = st->carried ? st->carried : 0x3D;

    int hi = 0, lo = 0, route;
    if (l24 == 0) {
        route = l14 != 0;
    } else if (l24 < l14 || l14 == 0) {
        if (shape == 0x48 && l14 == 0) { hi = 0x43; route = 0; }
        else {
            if (shape == 0x48)      hi = (flagT ? 1 : 0) + 0x44;
            else if (shape == 0x4D) hi = (flagT ? 1 : 0) + 0x40;
            else                    hi = flagT ? 0x3D : 0x3F;
            route = l14 != 0;
        }
    } else {
        route = 1;
    }
    if (route) {
        if (shape == 0x48)      lo = 0x44;
        else if (shape == 0x4D) lo = (flagT ? 0 : 1) + 0x3F;
        else                    lo = 0x3D;
    }

    int out, tailcode;
    first = l14;
    if (type == 0x0F) {
        tailcode = (hi != 0 && first == 0) ? hi : 0x44;
    } else if (type == 0x12 || type == 0x14) {
        int edge = back.val == 0 || back.val == 0x2F;
        if (first == n && edge)                 tailcode = lo;
        else if (l24 == n && edge)              tailcode = hi;
        else if (type == 0x14)                  tailcode = (!flagT && first == 0)
                                                           ? 0x40 : 0x3F;
        else                                    tailcode = voiced ? 0x3D : 0x3A;
    } else {
        tailcode = 0x3A;
    }
    out = tailcode;

    if (s[4] == 0) s[4] = (uint8_t)carry;

    if (l24 > 1 && !flagA) {
        int p = pos[l24 - 1];
        int a = s[p];
        if (a != 0x17 && a != 0x18) {
            int v = carry > 0x40 ? 0x40 : carry;
            s[p + (flagB ? 1 : 2)] = (uint8_t)v;
        }
    }
    if (hi) s[pos[l24] + 1] = (uint8_t)hi;
    if (lo) s[pos[first] + 1] = (uint8_t)lo;

    if (first != 0 && first < n) {
        if (shape != 0x4C) {
            int k;
            if (first + 2 > n)                            k = first + 1;
            else if (first == 1)                          k = first + 2;
            else if (pos[first + 1] - pos[first] > 4)      k = first + 1;
            else                                          k = first + 2;
            while (k <= n && !(bst_ph_attr1(img, s[pos[k] - 1]) & 4)) k++;
            if (k <= n && (bst_ph_attr1(img, s[pos[k] - 1]) & 4))
                s[pos[k] + 1] = 0x3D;
        }
        if (type != 0x12 && (bst_ph_attr1(img, s[pos[n] - 1]) & 4))
            s[pos[n] + 1] = 0x3D;
    }

    if (bst_trace)
        bst_tracef("acc n=%d first=%d l14=%d l24=%d shape=%02x type=%02x"
                   " hi=%02x lo=%02x out=%02x\n",
                n, first, l14, l24, shape, type, hi, lo, out);

    if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
        s[st->tail] = (uint8_t)tailcode;
    st->carried = voiced ? out : 0;
    return n;
}

int bst_accents(const bst_image *img, uint8_t *s, int len, bst_accent_state *st) {
    acc e[ACC_MAX];
    short bcmd[16];
    int nb = 0, n = 0;
    int first = 0, last = 0, latest = 0;     /* the three landmarks */
    int seen = 0;                            /* the running "last that counts" */
    int suppress = 0;                        /* inside a bracket that mutes accents */
    int soft = 0;                            /* a mark has opened the first accent */
    int f1d = 0, f1e = 0, f1f = 0;           /* the three tilde modes */
    int emph = 0;
    int type, shape;
    int codeA = 0, codeB = 0;

    if (img->t.acc_kind == 1) return accents_french(img, s, len, st);
    if (img->t.acc_kind == 3) return accents_japanese(img, s, len, st);

    memset(e, 0, sizeof e);
    st->emphasis = 0;

    /* A stream flagged as a fragment carries no contour of its own; it only
       hands the following one a starting level. */
    if (st->flags & 8) {
        if (s[4] == 0) s[4] = (uint8_t)(st->level + 0x3D);
        if (st->tail >= 0 && st->tail < len) {
            if (s[st->tail] != 0) return 0;
            s[st->tail] = (uint8_t)(st->level + 0x3D);
        }
        return 0;
    }

    if (s[6] == 'I') {
        type  = s[8] + 0x0E;
        shape = img->t.acc_shape ? img->t.acc_shape : (s[9] & 0x7F);
    } else {
        shape = img->t.hdr_shape ? img->t.hdr_shape : 0x4C;
        type  = 0x12;
    }

    bst_cur lastseg;
    scan_back_seg(img, s, len - 6, &lastseg);

    for (int i = 12; i < len; ) {
        int b = s[i];
        int next = i;
        if (b == 0x7C) {
            int c = s[i + 1];
            if (c == 'D') emph = cmd16(s, i) >= 1;
            else if (c == 'b' && nb < 10) bcmd[++nb] = (short)i;
            else if (c == '~') {
                int v = cmd16(s, i);
                if (v == 0x1D) f1d = 1;
                else if (v == 0x1E) f1e = 1;
                else if (v == 0x1F) f1f = 1;
            }
            next = i + 6;
            if (bst_ph_attr2(img, s[i + 7]) & 8) next = i + 7;
        } else if (bst_ph_attr2(img, b) & 0x10) {
            if (b == 0x4F || b == 0x50 || b == 0x51) suppress = 1;
        } else if (bst_ph_attr2(img, b) & 8) {
            if (b == 0x4B) suppress = 0;
            else { suppress = f1d = f1e = f1f = 0; }
        } else if (bst_ph_attr1(img, b) & 0x80) {
            if (n + 1 >= ACC_MAX) break;
            n++;
            e[n].pos = (short)(i + 1);
            e[n].kind = 0;
            int mark = s[i + 1];
            int t = seen;
            int tail = 0;
            if (mark < 0x36) {
                if (mark == 0x35) {
                    if (first != 0 || suppress) { tail = suppress; }
                    else { e[n].kind = 3; soft = 1; first = n; }
                }
            } else {
                if (!suppress) {
                    if (f1e)      { e[n].kind = 1; if (soft && last == 0) first = n; }
                    else if (f1f) { e[n].kind = 2; if (soft && last == 0) first = n; }
                    else            e[n].kind = 3;
                    if (first == 0) first = n;
                    t = n;
                    if (!f1d) { t = seen; if (seen == 0) last = n; }
                }
                tail = 1;
            }
            if (tail) { latest = n; seen = t; }
            next = i + 3;
        }
        i = next + 1;
    }

    /* Nothing to accent: hand the level on and stop. */
    if (n == 0) {
        int c = st->carried ? st->carried : st->level + 0x3D;
        if (s[4] == 0) s[4] = (uint8_t)c;
        if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
            s[st->tail] = (uint8_t)c;
        return 0;
    }

    if (seen == 0) {
        if (last == 0 && type != 0x13) {
            int k = latest ? latest : n;
            first = k;
            e[k].kind = 3;
            last = k;
            seen = k;
        } else if (first == 0 || last == 0) {
            int c = st->carried ? st->carried : st->level + 0x3D;
            if (s[4] == 0) s[4] = (uint8_t)c;
            if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
                s[st->tail] = (uint8_t)c;
            return 0;
        } else {
            seen = last;
        }
    }
    last = seen;

    /* A phrase with one accent falls back to the plain shape unless the
       header's tenth byte has its top bit set. */
    if (first == last && !(s[9] & 0x80) && !img->t.acc_shape_keep) shape = 0x4C;

    int kind = e[first].kind;
    int nextkind = 0;
    for (int k = first; k <= last; k++) {
        nextkind = e[k].kind;
        if (nextkind != 0) break;
    }

    /* A bracket command before the first accent sets the pitch level. */
    for (int k = nb; k >= 1; k--)
        if (bcmd[k] < e[first].pos) { st->level = s[bcmd[k] + 3]; break; }

    int lead;
    if (first == 1) {
        lead = (kind == 1 || kind == 2) ? 0x3C : st->level + 0x3D;
        if (kind == 2) codeA = 0x3D;
    } else if (last == first || kind == 1 || nextkind == 1) {
        if (shape == 0x48 || (shape == 0x52 && (kind == 1 || kind == 2))) lead = 0x42;
        else lead = 0x3D;
        if ((!emph || (kind != 3 && kind != 2) || shape == 0x4C) &&
            (shape != 0x48 || kind != 1)) {
            if (shape == 0x4C && (kind == 3 || kind == 2))      codeA = 0x3D;
            else if (shape == 0x48 && kind == 2)                codeA = 0x42;
            else if (shape == 0x52 && kind == 2)                codeA = 0x3D;
            else                                                codeA = 0;
        } else codeA = 0x42;
    } else {
        lead = 0x3D;
        codeA = 0x3D;
    }

    if (s[4] == 0) {
        s[4] = (uint8_t)lead;
        if (st->carried) {
            s[4] = (uint8_t)st->carried;
            if (s[0] == 0x4E && first > 1) {
                if (last == first &&
                    (shape == 0x48 || (shape == 0x52 && (kind == 1 || kind == 2))))
                    s[e[1].pos + 2] = 0x42;
                else
                    s[e[1].pos + 2] = 0x3D;
            }
        }
    }

    if (bst_trace)
        bst_tracef("acc n=%d first=%d last=%d kind=%d nextkind=%d emph=%d"
                   " shape=%02x type=%02x level=%d lead=%02x codeA=%02x\n",
                n, first, last, kind, nextkind, emph, shape, type, st->level,
                lead, codeA);

    int cur = first;
    if (kind == 2) {
        s[e[first].pos + 1] = (uint8_t)codeA;
    } else if (first > 1) {
        if (codeA < 0x42) s[e[first - 1].pos + 1] = (uint8_t)codeA;
        else              s[e[first - 1].pos + 2] = (uint8_t)codeA;
    }

    while (cur < last && last != first) {
        int nx = cur;
        int nk;
        do { nx++; nk = e[nx].kind; } while (nk == 0 && nx < last);

        for (int k = nb; k >= 1; k--)
            if (e[cur].pos < bcmd[k] && bcmd[k] < e[nx].pos) {
                st->level = s[bcmd[k] + 3];
                break;
            }

        if (shape == 0x4C && (kind == 3 || kind == 2) && (nk == 3 || nk == 2))
            codeB = st->level + 0x3D;
        else
            codeB = 0;

        if (nk == 2) codeA = 0x3D;
        else if (last == nx && (shape == 0x48 || nx - cur == 1) &&
                 (kind == 3 || kind == 2)) codeA = 0x42;
        else if (shape == 0x48 || ((kind != 3 && kind != 2) || nk != 3)) {
            int keep = (emph && kind == 1 && nk == 3) ||
                       (shape == 0x48 && (kind == 3 || kind == 2) && nk == 1) ||
                       (emph && shape == 0x48 && (kind == 3 || kind == 2) && nk == 3);
            if (!keep) codeA = 0;
            else if (last == nx)
                codeA = (!emph && type != 0x0F && type != 0x10) ? 0x43 : 0x42;
            else codeA = 0x42;
        } else {
            codeA = st->level + 0x3D;
        }

        if (kind == 2) s[e[cur].pos + 2] = 0x42;
        else           s[e[cur].pos + 1] = (uint8_t)(kind == 1 ? 0x3C : 0x42);

        kind = nk;
        int prev = cur;
        cur = nx;
        if (nk == 2) {
            s[e[nx].pos + 1] = 0x3D;
        } else if (nx - prev < 2) {
            if (last == nx) s[e[nx - 1].pos + 2] = (uint8_t)codeA;
        } else {
            if (codeA < 0x42 && shape != 0x52) s[e[nx - 1].pos + 1] = (uint8_t)codeA;
            else                               s[e[nx - 1].pos + 2] = (uint8_t)codeA;
            if (nx - prev > 2) s[e[prev + 1].pos + 1] = (uint8_t)codeB;
        }
    }

    for (int k = nb; k >= 1; k--)
        if (e[last].pos < bcmd[k]) { st->level = s[bcmd[k] + 3]; break; }

    int fin;
    if (kind == 1) {
        codeB = 0;
        fin = 0x3C;
        codeA = (type == 0x0F || type == 0x11) ? 0x42 : 0x3C;
    } else {
        if (emph || type == 0x0F)      fin = 0x42;
        else                           fin = (type == 0x10) ? 0x42
                                           : img->t.acc_fin ? img->t.acc_fin : 0x43;
        switch (type) {
        case 0x0F: codeB = 0;                 codeA = 0x45; break;
        case 0x10: codeB = 0;                 codeA = 0x42; break;
        case 0x11: codeB = 0x3D;
                   codeA = img->t.acc_code11 ? img->t.acc_code11 : 0x42;
                   break;
        case 0x16: codeA = 0x45;              codeB = st->level + 0x3D; break;
        case 0x14: codeB = 0;                 codeA = st->level + 0x3D; break;
        case 0x17: codeA = st->level + 0x3D;  codeB = codeA; break;
        case 0x13: codeB = 0;                 codeA = 0x3D; break;
        case 0x12: codeB = 0;
                   codeA = (lastseg.val == 0x2F) ? 0x3A : 0x3D; break;
        default: break;
        }
    }

    if (kind == 2) s[e[cur].pos + 2] = (uint8_t)fin;
    else           s[e[cur].pos + 1] = (uint8_t)fin;

    int done = 0;
    if (type == 0x11 || type == 0x13 || type == 0x12) {
        if (kind == 3) {
            int fc = (n == cur && img->t.fall_code_last)
                   ? img->t.fall_code_last
                   : img->t.fall_code ? img->t.fall_code : 0x3D;
            if (img->t.fall_on_next && n - cur > 0) s[e[cur + 1].pos + 1] = (uint8_t)fc;
            else                                    s[e[cur].pos + 2] = (uint8_t)fc;
            done = 1;
        }
        else if (kind == 2) {
            if (n != cur && n - cur >= 0) s[e[cur + 1].pos + 1] = 0x3D;
            done = 1;
        }
    }
    if (!done && (type == 0x16 || type == 0x17))
        s[e[cur + 1].pos + 1] = (uint8_t)codeB;

    int rest = n - cur;
    if (rest > 0 && (kind != 2 || rest > 1)) s[e[n].pos + 1] = (uint8_t)codeB;

    if (st->tail >= 0 && st->tail < len && s[st->tail] == 0)
        s[st->tail] = (uint8_t)codeA;

    st->carried = (lastseg.val == 0x2F) ? 0 : codeA;
    st->emphasis = emph;
    return n;
}
