#include <string.h>
#include "bst_text.h"

/* The phonological rule pass: one traversal of the sentence phoneme stream
   that rewrites sounds in context. Unvoiced stops aspirate, stops before a
   pause go unreleased, one vowel reduces at phrase edges, and a glottal stop
   is inserted between certain vowel sequences.
 *
 * It carries five cursors -- the next segment, the one after it, the next
 * stress mark, the next of an eighth class, and a delayed copy of the previous
 * segment -- and reads the stream it is rewriting, so a rewritten byte can
 * change the class its own scans look for and an insertion shifts every
 * position after it. Cursors, rewrites and insertions are one traversal and
 * cannot be separated.
 *
 * The level, set by a command in the stream, gates most of the rules: below
 * zero none of them fire, above nine one of them starts deleting.
 *
 * The cursor scans themselves are shared with the pair scan and live in
 * cursors.c. */

#define CMD 0x7C

typedef bst_cur cur;

static int a1(const bst_image *img, int c) { return bst_ph_attr1(img, c); }
static int a2(const bst_image *img, int c) { return bst_ph_attr2(img, c); }

/* Whether a marker that opens an emphasis group comes before the next phrase
   boundary. */
/* The French build's two lookaheads. The first asks whether an emphasis
   command or the marker that raises a word comes before the next boundary;
   the second asks the same of one tilde mode, and steps seven bytes at a time
   over ordinary sounds because its command test is the wrong way round. */
static int fr_raise_ahead(const bst_image *img, const uint8_t *s, int len, int from) {
    int p = from + 1;
    int c = (p >= 0 && p < len) ? s[p] : 0;
    if (a2(img, c) & 8) return 0;
    for (;;) {
        if (p >= len) return 0;
        if (c == CMD) {
            if (s[p + 1] == 'd') return 1;
            p += 6;
        } else if (bst_code(img, c) == 0x52) {
            return 1;
        }
        p++;
        c = (p >= 0 && p < len) ? s[p] : 0;
        if (a2(img, c) & 8) return 0;
    }
}

static int fr_mode_ahead(const bst_image *img, const uint8_t *s, int len, int from) {
    int p = from + 1;
    int c = (p >= 0 && p < len) ? s[p] : 0;
    if (a2(img, c) & 8) return 0;
    for (;;) {
        if (p >= len) return 0;
        if (c == CMD) {
            if (s[p + 1] == '~' && s[p + 3] == 0x18) return 1;
        } else {
            p += 6;
        }
        p++;
        c = (p >= 0 && p < len) ? s[p] : 0;
        if (a2(img, c) & 8) return 0;
    }
}

/* Whether the marker that mutes a function word comes before the next
   boundary. */
/* Forward to the next segment that opens a syllable, which the French rules
   track a generation behind to decide whether a word opener still has a
   syllable in front of it. */
static void fr_scan_vowel(const bst_image *img, const uint8_t *s, int lim,
                          int from, cur *out) {
    cur t;
    int p = from;
    for (;;) {
        bst_scan_seg(img, s, lim, p, &t);
        if (a1(img, t.val) & 0x80) { *out = t; return; }
        if (t.val == 0) { out->val = 0; out->pos = t.pos; return; }
        p = t.pos;
    }
}

static int fr_weak_ahead(const bst_image *img, const uint8_t *s, int len, int from) {
    int p = from + 1;
    int c = (p >= 0 && p < len) ? s[p] : 0;
    if (a2(img, c) & 8) return 0;
    for (;;) {
        if (p >= len) return 0;
        if (c == CMD) p += 6;
        else if (bst_code(img, c) == 0x49) return 1;
        p++;
        c = (p >= 0 && p < len) ? s[p] : 0;
        if (a2(img, c) & 8) return 0;
    }
}

/* Whether the sound at this position opens its group: walking back from it,
   a group boundary comes before another sound does. */
static int opens_run(const bst_image *img, const uint8_t *s, int pos) {
    if (!(a2(img, s[pos]) & 1)) return 0;
    int p = pos - 1;
    while (p != 0) {
        int b = s[p];
        if (b != 0 && b != CMD) {
            int a = a2(img, b);
            if (a & 1) return 0;
            if (a & 8) return 1;
        }
        if (b == CMD) p -= 6;
        p--;
    }
    return 1;
}

static int marker_ahead(const bst_image *img, const uint8_t *s, int len, int from) {
    int p = from + 1;
    for (;;) {
        int b = (p >= 0 && p < len) ? s[p] : 0;
        if ((a2(img, b) & 8) || len <= p) return 0;
        if (b == CMD) p += 6;
        else if (b == 0x4F || b == 0x50 || b == 0x51) return 1;
        p++;
    }
}

int bst_rule_mask = -1;   /* retained so a term can still be disabled by hand */

int bst_phrules(const bst_image *img, uint8_t *s, int *lenp, int cap, int level) {
    int len = *lenp;
    int inserted_total = 0;
    cur next = {0, 0}, next2 = {0, 0}, eight = {0, 0}, stress = {0, 0};
    cur prev = {0, 0}, prev8 = {0, 0};
    cur pend = {0, 0}, pend8 = {0, 0}, older8 = {0, 0};
    cur older = {0, 0}, pendx = {0, 0}, prevx = {0, 0}, vowel = {0, 0};
    int fr_q = 0, fr_s = 0;
    int changed = 0, inserted = 0, rescan = 1;
    int latch_o = 0, latch_p = 0;
    int lim = len;
    int last = 0;

    /* The traversal stops at the last sound of the eighth class, not at the
       end of the buffer. */
    bst_scan_eight_back(img, s, len, &prev8);
    last = prev8.pos;
    prev8.val = prev8.pos = 0;
    prev.val = prev.pos = 0;
    stress.pos = 0;

    bst_scan_eight(img, s, lim, -1, &eight);

    for (int i = 0; i < last; i++) {
        if (rescan) {
            bst_scan_seg(img, s, lim, i, &next);
            bst_scan_seg(img, s, lim, next.pos, &next2);
            rescan = 0;
        }
        if (inserted) {
            int shift = inserted;
            inserted = 0;
            i += shift;
            pend.pos += shift;
            next.pos += shift;
            next2.pos += shift;
            stress.pos += shift;
            if (next.pos >= 0 && next.pos < lim)   next.val = s[next.pos];
            if (next2.pos >= 0 && next2.pos < lim) next2.val = s[next2.pos];
            if (stress.pos >= 0 && stress.pos < lim) stress.val = s[stress.pos];
        }

        if (i == stress.pos) bst_scan_stress(img, s, lim, i, &stress);
        if (pend.pos - i == -1) { older = prev; prev = pend; }
        if (i == next.pos) {
            pend = next;
            next = next2;
            bst_scan_seg(img, s, lim, next2.pos, &next2);
        }
        if (pend8.pos - i == -1) { older8 = prev8; prev8 = pend8; }
        if (i == eight.pos) {
            pend8 = eight;
            bst_scan_eight(img, s, lim, i, &eight);
        }
        if (img->t.ph_kind == 6) {
            if (pendx.pos - i == -1) prevx = pendx;
            if (i == vowel.pos) {
                pendx = vowel;
                fr_scan_vowel(img, s, lim, i, &vowel);
            }
        }

        int c = s[i];
        int bit1 = a2(img, c) & 1;
        int at_edge = bit1 && !(eight.pos < i) && !(next.pos <= eight.pos);
        int after_seg = bit1 && !(prev8.pos < prev.pos);

        if (c == CMD) {
            if (s[i + 1] == 'q') {
                level = (int16_t)((s[i + 2] << 8) | s[i + 3]);
                if (level > 10)  { level = 10; i += 6; continue; }
                if (level < -10) level = -10;
            }
            i += 6;
            continue;
        }

        if (img->t.ph_kind == 6) {
            /* The French rules. Two markers latch state for the rest of the
               word; a word opener is dropped when nothing has moved past it;
               the tap becomes the trill away from a vowel; a liaison consonant
               is dropped before a word that does not want it; and the schwa
               drops with its mark, under one set of conditions when the rate
               command is slow and another when it is not. */
            int mc = bst_code(img, c);
            if (mc == 0x51) { fr_q = 1; continue; }
            if (mc == 0x53) { fr_s = 1; continue; }
            if (a2(img, c) & 8) {
                if (mc == 0x44 && !(prevx.pos > prev8.pos)) {
                    s[i] = 0;
                    pend8 = eight;
                    continue;
                }
                fr_s = fr_q = 0;
            }
            if (mc < 1 || mc > 0x28) continue;

            int nv = bst_code(img, next.val);
            int ev = bst_code(img, eight.val);

            if (mc == 0x12) {
                int rw = prev.val != 0 && !(a1(img, prev.val) & 4);
                if (!rw)
                    rw = !(next.val == 0 || (a1(img, next.val) & 4) || nv == 0x27);
                if (rw) { c = bst_uncode(img, 0x13); s[i] = (uint8_t)c; mc = 0x13; }
            }

            int handled = 0;
            if (fr_q && (a1(img, c) & 1) &&
                (at_edge || nv == 0x27 || next.val == 0)) {
                handled = 1;
                if (fr_s && ev > 0x44) {
                    if (mc == 0x0F) { c = bst_uncode(img, 0x0C); s[i] = (uint8_t)c; }
                } else {
                    int del = 1;
                    if ((a1(img, next.val) & 0x80) || nv == 0x16 || nv == 0x14 ||
                        nv == 0x15) {
                        if (!fr_raise_ahead(img, s, lim, eight.pos) ||
                            (fr_s && mc == 3))
                            del = fr_mode_ahead(img, s, lim, prev8.pos) &&
                                  !fr_mode_ahead(img, s, lim, eight.pos);
                    }
                    if (del) { s[i] = 0; pend = next; }
                }
            }

            if (!handled && mc == 0x26) {
                int drop = 0;
                if (level > 0) {
                    int step = 0;
                    if (!fr_weak_ahead(img, s, lim, prev8.pos) &&
                        !(fr_raise_ahead(img, s, lim, eight.pos) && at_edge)) {
                        if (!(a1(img, prev.val) & 1))            step = 1;
                        else if (!(a1(img, older.val) & 1))      step = 2;
                        else if (!at_edge && !(ev > 0x44))       step = 0;
                        else if (a1(img, next.val) & 1)          step = 0;
                        else                                     step = 2;
                        if (step == 2) {
                            if (prevx.val == 0) step = 0;
                            else if ((nv == 0x11 || nv == 0x12) &&
                                     bst_code(img, next2.val) == 0x14) step = 0;
                            else step = 1;
                        }
                    }
                    if (step == 1) {
                        int cbit = (a1(img, next.val) & 1) != 0;
                        drop = 1;
                        if (cbit && (a1(img, next2.val) & 1)) drop = 0;
                        if (drop && (a1(img, older.val) & 1)) {
                            int pv = bst_code(img, prev.val);
                            if ((pv == 0x11 || pv == 0x12 || pv == 0x13) &&
                                cbit && at_edge) drop = 0;
                        }
                    }
                } else {
                    int reach = (ev > 0x44 && nv == 0x27) || next.val == 0;
                    if (!reach && (a1(img, next.val) & 0x80) &&
                        !fr_raise_ahead(img, s, lim, eight.pos))
                        reach = 1;
                    if (reach && prevx.pos > prev8.pos) drop = 1;
                }
                if (drop) {
                    s[i] = 0;
                    if (i + 1 < lim) s[i + 1] = 0;
                    pend = next;
                    pendx = next2;
                }
            }

            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 7) {
            /* Hebrew has one rule: an unstressed open vowel closes when it
               ends a phrase or stands at the head of the next group. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x23) continue;
            if (mc == 0x1A && stress.val < 6 &&
                (bst_code(img, next.val) == 0x22 ||
                 (next.pos > 0 && opens_run(img, s, next.pos))))
                c = bst_uncode(img, 0x21), s[i] = (uint8_t)c;
            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 8) {
            /* Greek: an unvoiced stop after the nasal voices, three of them
               taking an extra sound with it, and a glottal stop goes between
               two sounds that open across a segment boundary. The original
               leaves its cursors where they were after an insertion, so this
               does too. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x29) continue;
            if (!(prev.pos > prev8.pos) && bst_code(img, prev.val) == 0x0A) {
                int to = 0, extra = 0;
                switch (mc) {
                case 5:    to = 7; break;
                case 3:    to = 4; break;
                case 1:    to = 2; break;
                case 0x16: to = 4; extra = 1; break;
                case 0x18: to = 7; extra = 1; break;
                case 0x15: to = 2; extra = 1; break;
                default:   break;
                }
                if (to) {
                    c = bst_uncode(img, to);
                    s[i] = (uint8_t)c;
                    if (extra && len + 1 < cap) {
                        for (int k = len; k > i + 1; k--) s[k] = s[k - 1];
                        s[i + 1] = (uint8_t)bst_uncode(img, 0x11);
                        len++; lim++;
                        if (i + 1 <= last) last++;
                        inserted_total++;
                    }
                }
            }
            if (after_seg && (a1(img, prev.val) & 0x80) && (a1(img, c) & 0x80) &&
                len + 1 < cap) {
                for (int k = len; k > i; k--) s[k] = s[k - 1];
                s[i] = (uint8_t)bst_uncode(img, 0x28);
                len++; lim++;
                if (i <= last) last++;
                inserted_total++;
            }
            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 9) {
            /* Japanese: three sounds take their palatal form before one
               vowel, the moraic nasal takes the place of what follows it,
               and two long vowels absorb the mark that follows them. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x22) continue;
            int nv = bst_code(img, next.val);
            int to = 0;
            if (mc == 2 && nv == 0x1C)      to = 0x0E;
            else if (mc == 8 && nv == 0x1C) to = 0x09;
            else if (mc == 0x0D && nv == 0x1C) to = 0x0F;
            else if (mc == 0x1A) {
                if (nv == 1 || nv == 4 || nv == 0x10) to = 0x19;
                else if (nv == 3 || nv == 6 || nv == 0x16 || nv == 0x12 ||
                         nv == 0x0A || nv == 0x21 || nv == 0x22 ||
                         (a1(img, next.val) & 0x80)) to = 0x1B;
            }
            if (to) {
                c = bst_uncode(img, to);
                s[i] = (uint8_t)c;
            } else if (mc == 0x1C || mc == 0x20) {
                if (prev.val != 0 && nv != 0x21 &&
                    s[i + 1] < 0x36 && !(a1(img, next.val) & 4) &&
                    !(a1(img, prev.val) & 4)) {
                    c = bst_uncode(img, mc == 0x1C ? 0x17 : 0x18);
                    s[i] = (uint8_t)c;
                    if (i + 1 < lim) s[i + 1] = 0;
                }
            }
            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 10) {
            /* Russian: the tap at the head of a word is trilled, two vowels
               centre between soft consonants, the voiced labial at the head
               of a word unvoices before a voiceless sound, and the front
               vowel backs after a hard one. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x34) { pend.val = (uint8_t)c; continue; }
            int seg = (a2(img, c) & 1) != 0;
            int wstart = seg && prev8.pos >= prev.pos;
            int wend = seg && eight.pos >= i && eight.pos < next.pos;
            int done = 0;
            if (wstart && (mc == 0x22 || mc == 0x23)) {
                mc += 2;
                c = bst_uncode(img, mc);
                s[i] = (uint8_t)c;
                done = 1;
            }
            if (!done && (mc == 0x29 || mc == 0x2C) && prev.pos >= prev8.pos) {
                if ((a1(img, prev.val) & 8) && next.pos <= eight.pos &&
                    (a1(img, next.val) & 8)) {
                    mc = mc == 0x29 ? 0x2A : 0x2D;
                    c = bst_uncode(img, mc);
                    s[i] = (uint8_t)c;
                }
            }
            if (wstart) {
                int edge = next.val == 0 || wend ||
                           bst_code(img, next.val) == 0x33;
                if (edge && !(a1(img, next.val) & 4) && mc == 0x13) {
                    mc = 0x11;
                    c = bst_uncode(img, mc);
                    s[i] = (uint8_t)c;
                }
                int pa = a1(img, prev.val);
                if ((pa & 1) && !(pa & 8) && mc == 0x27) {
                    mc = 0x2E;
                    c = bst_uncode(img, mc);
                    s[i] = (uint8_t)c;
                }
            }
            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 11) {
            /* Arabic rewrites nothing here: every sound its rules produce is
               the sound it speaks. */
            pend.val = (uint8_t)c;
            continue;
        }

        if (img->t.ph_kind == 1) {
            /* The Romance rules: no aspiration, no glottal stop, no vowel
               reduction; the voiced stops soften between sounds, the sibilant
               takes the voicing of what follows, and a weak vowel beside
               another vowel drops its stress mark a level. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            bst_cur nstress;
            bst_scan_stress(img, s, lim, i + 2, &nstress);
            int nv = next.val, pv = prev.val;
            int voiced = (a1(img, nv) & 4) && (a1(img, nv) & 1) &&
                         (a1(img, nv) & 0xFA);
            int flank = (a1(img, nv) & 0x80) || (a1(img, pv) & 0x80);
            int said = -1;
            switch (mc) {
            case 4:
                if (pv != 0 && pv != 0x0A) s[i] = 5;
                break;
            case 6:
                if (pv != 0 && pv != 0x0B && pv != 0x13) s[i] = 7;
                break;
            case 8:
                if (pv != 0 && pv != 0x0D && nv != 0x17) s[i] = 9;
                break;
            case 0x0B:
                if ((nv == 3 || nv == 8 || nv == 0x11 || nv == 0x17) && !after_seg)
                    s[i] = 0x0D;
                else if (nv == 1 || nv == 4 || nv == 0x0A)
                    s[i] = 0x0A;
                break;
            case 0x0F:
            case 0x10:
                /* The sibilant takes the voicing of what follows, and one
                   sibilant before another is struck out. */
                if (voiced)          s[i] = 0x10;
                else if (nv == 0x0F) { s[i] = 0; said = nv; }
                else                 s[i] = 0x0F;
                break;
            case 0x18:
                if (stress.val < 5 && flank) s[i] = 0x16;
                break;
            case 0x1C:
                if (stress.val < 5 && flank) s[i] = 0x17;
                break;
            case 0x19: case 0x1A: case 0x1B:
                if (stress.val >= 5) break;
                if (nv == mc && nstress.val < 5) {
                    s[i] = 0;
                    if (i + 1 < lim) s[i + 1] = 0;
                    said = nv;
                } else if (flank && stress.pos > 0) {
                    s[stress.pos] = 0x31;
                }
                break;
            default:
                break;
            }
            /* A sound struck out leaves the one that swallowed it behind. */
            pend.val = said >= 0 ? said : s[i];
            continue;
        }

        if (img->t.ph_kind == 2) {
            /* The Italian rules: two glides open before a stressed vowel, one
               sound doubles after nothing, and a phrase-final sound takes a
               vowel of its own. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            if (mc == 0x1E) {
                if (stress.val < 3) s[i] = 0x1D;
            } else if (mc == 0x21) {
                if (stress.val < 3) s[i] = 0x20;
            } else if (mc == 0x18) {
                if (eight.val > 0x4C && next.val == 0x2F && len + 3 <= cap) {
                    s[i] = 0x1C;
                    for (int k = len + 2; k > i + 3; k--) s[k] = s[k - 3];
                    s[i + 1] = 0x32;
                    s[i + 2] = 0;
                    s[i + 3] = 0;
                    len += 3; lim += 3;
                    if (i <= last) last += 3;
                    inserted = 3;
                    inserted_total += 3;
                }
            } else if (mc == 0x15) {
                if (prev.val == 0) s[i] = 0x16;
            }
            pend.val = s[i];
            continue;
        }

        if (img->t.ph_kind == 3) {
            /* Portuguese has one rule: an unstressed vowel of one kind
               reduces, or closes when it ends the word before a nasal. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            if (mc == 0x1B) {
                int nv = bst_code(img, next.val);
                int seg = next.pos > 0 && opens_run(img, s, next.pos);
                if ((nv == 0x0A || nv == 9) && !seg && stress.val == 6) {
                    s[i] = 0x22;
                } else if (stress.val < 6) {
                    int n2 = bst_code(img, next2.val);
                    if (nv == 0x37 || seg || (nv == 0x0F && n2 == 0x37))
                        s[i] = (uint8_t)bst_uncode(img, 0x36);
                }
            }
            pend.val = s[i];
            continue;
        }

        if (img->t.ph_kind == 4) {
            /* The Dutch rules: a voiced stop hardens where the sound before
               it allows, and a vowel that opens a group after another sound
               takes a glottal stop in front. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            int at = a1(img, c);
            if (at & 0x46) {
                int nv = next.val;
                if (nv == 0 || at_edge || nv == 0x2F || (a1(img, nv) & 0x42)) {
                    if (mc == 4)      s[i] = 1;
                    else if (mc == 5) s[i] = 2;
                }
            }
            if ((a1(img, s[i]) & 0x80) && after_seg && len + 1 <= cap) {
                for (int k = len; k > i; k--) s[k] = s[k - 1];
                s[i] = 7;
                len++; lim++;
                if (i <= last) last++;
                inserted = 1;
                inserted_total++;
            }
            pend.val = s[i];
            continue;
        }

        if (img->t.ph_kind == 5) {
            /* The German rules: a voiced sound hardens at the end of a word
               and softens between two others, one fricative takes the place
               of what precedes it, one vowel splits three ways, and a vowel
               after another sound takes a stop in front. */
            int mc = bst_code(img, c);
            if (mc < 1 || mc > 0x30 + img->t.code_shift) continue;
            int at = a1(img, c);
            int nv = next.val, pv = prev.val;
            if (at & 0x46) {
                if (nv == 0 || at_edge || nv == 0x2F || (a1(img, nv) & 0x42)) {
                    static const uint8_t hard[] = { 7, 2, 8, 4, 9, 6,
                                                    0x14, 0x0F, 0x15, 0x10 };
                    for (int k = 0; k < (int)sizeof hard; k += 2)
                        if (mc == hard[k]) { s[i] = hard[k + 1]; break; }
                }
            }
            int m2 = bst_code(img, s[i]);
            if ((a1(img, s[i]) & 6) == 2) {
                int soft = nv == 0x10 || nv == 0x11 || nv == 0x0F;
                if (!soft && !after_seg) {
                    if (at_edge || nv == 0x2F || nv == 0 ||
                        pv == 0x10 || pv == 0x11 ||
                        stress.val < 3 || stress.pos >= eight.pos)
                        soft = 1;
                }
                if (soft) {
                    if (m2 == 1)      s[i] = 2;
                    else if (m2 == 3) s[i] = 4;
                    else if (m2 == 5) s[i] = 6;
                }
            }
            m2 = bst_code(img, s[i]);
            if (m2 == 0x2F && bst_code(img, nv) == 0x1B) {
                int ok = ((a1(img, prev.val) & 1) && prev.pos >= eight.pos) ||
                         (a2(img, prev.val) & 1) || bst_code(img, prev.val) == 0x37;
                if (ok) {
                    s[i] = (uint8_t)bst_uncode(img, 0x30);
                    if (next.pos >= 0 && next.pos < lim) s[next.pos] = 0;
                    rescan = 1;
                }
            }
            m2 = bst_code(img, s[i]);
            if (m2 == 0x13) {
                int a = a1(img, pv);
                if (!(a & 8) || (a & 1)) s[i] = 0x12;
            } else if (m2 == 0x1B && !after_seg) {
                /* What stands before decides which of the two forms the
                   vowel takes. */
                int a = a1(img, pv);
                if ((a & 0x80) && (!(a1(img, nv) & 0x80) || at_edge))
                    s[i] = 0x1C;
                else if (!(a & 4))
                    s[i] = 0x1A;
            }
            /* The stop written in front of a vowel takes the vowel's place
               in the stream, but the vowel is still what stands behind the
               sound that follows. */
            int said = s[i];
            if ((a1(img, s[i]) & 0x80) && after_seg && len + 1 <= cap) {
                for (int k = len; k > i; k--) s[k] = s[k - 1];
                s[i] = 0x1F;
                len++; lim++;
                if (i <= last) last++;
                inserted = 1;
                inserted_total++;
            }
            pend.val = said;
            continue;
        }

        if (c == 0x4F && stress.val < 5 && eight.val < 0x4E)      latch_o = 1;
        else if (c == 0x50 && stress.val < 5 && eight.val < 0x4E) latch_p = 1;
        else if (a2(img, c) & 8)                                  latch_o = latch_p = 0;

        if (c == 0 || (c >= 0x31 && c < 0x80)) continue;

        int skip_rest = 0;

        if (a1(img, c) & 0x80) {
            int reduce = 0;
            if (latch_o && level >= 0) reduce = 1;
            else if (latch_p && level > 0 &&
                     !marker_ahead(img, s, lim, older8.pos) &&
                     !marker_ahead(img, s, lim, eight.pos) &&
                     prev8.val != 0x4E && eight.val != 0x4E)
                reduce = 1;
            if (reduce) {
                /* One following sound pre-empts the reduction: this sound
                   becomes a different one and the follower is deleted. */
                if (next.val == 0x12 && next.pos < eight.pos) {
                    c = 0x2E; s[i] = 0x2E;
                    if (next.pos >= 0 && next.pos < lim) s[next.pos] = 0;
                    rescan = 1;
                    changed++;
                } else if (c == 0x2C) {
                    c = 0x2A; s[i] = 0x2A; changed++;
                } else if (!(a1(img, next.val) & 0x80)) {
                    c = 0x24; s[i] = 0x24; changed++;
                }
            }
        }

        /* A glottal stop between two vowels of the listed kinds. Only the
           English builds have the rule. */
        if (after_seg && !img->t.no_glottal && (a1(img, prev.val) & 1) &&
            prev.val != 0x12 && prev.val != 0x11 && prev.val != 8 && prev.val != 9 &&
            (c == 0x12 || c == 0x11 || c == 8 || c == 9)) {
            if (len + 1 <= cap) {
                for (int k = len; k > i; k--) s[k] = s[k - 1];
                s[i] = 0x30;
                len++; lim++;
                if (i <= last) last++;
                inserted = 1;
                inserted_total++;
                changed++;
            }
        }

        /* An unreleased stop. Only the English builds have the rule. */
        if ((c == 0x18 || c == 0x14) && !img->t.no_unrelease && !after_seg &&
            (stress.val < 3 || at_edge) &&
            ((a1(img, prev.val) & 0x80) || prev.val == 0x12) &&
            (a1(img, next.val) & 0x80) && level >= 0) {
            if (c == 0x18 && (next.val == 0x24 || next.val == 0x25) && !at_edge &&
                next2.val == 0x0F && next2.pos < eight.pos)
                skip_rest = 1;
            else if (s[i] == 0x18 || s[i] == 0x14) {
                c = 0x19; s[i] = 0x19; changed++;
            }
        }
        if (skip_rest) continue;

        /* Aspiration. */
        if ((next.val == 0x0D || next.val == 0x02 ||
             (eight.val == 0x4E && next.val == 0) ||
             (((a1(img, c) & 6) == 2) && !after_seg &&
              ((stress.val < 3 && (c != 0x18 || next.val != 0x12)) ||
               prev.val == 0x0D || at_edge ||
               (!(a1(img, next.val) & 0x80) && next.val != 0x12 &&
                (next.val != 0x11 || c == 0x18) &&
                next.val != 9 && next.val != 8)))) && level >= 0) {
            if (c == 0x16)      { c = 0x17; s[i] = 0x17; changed++; }
            else if (c == 0x18) { c = 0x1B; s[i] = 0x1B; changed++; }
            else if (c == 0x1C) { c = 0x1D; s[i] = 0x1D; changed++; }
        }

        /* The reduced vowel and its phrase-edge variant. */
        if (c == 0x24) {
            if (next.val == 0x11) { c = 0x25; s[i] = 0x25; changed++; }
            if (level > 9) {
                s[i] = 0;
                if (i + 1 < lim) s[i + 1] = 0;
                prev = next;
                changed++;
            }
            /* The Polish build has no edge variant: its stream keeps the
               reduced vowel where the others swap it. */
            if (!img->t.no_edge_reduce &&
                (prev.val == 0 || next.val == 0 ||
                 prev.val == 0x2F || next.val == 0x2F)) {
                c = 0x2B; s[i] = 0x2B; changed++;
            }
        }

        pend.val = c;
    }
    (void)latch_p;
    (void)bst_rule_mask;
    (void)changed;
    *lenp = len;
    return inserted_total;
}
