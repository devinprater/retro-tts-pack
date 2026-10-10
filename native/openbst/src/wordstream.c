#include <string.h>
#include "bst_text.h"

/* The join between the two halves of the word front end.
 *
 * The dictionary stores a pronunciation as typed records -- append a sound,
 * modify the nth vowel back, put the stress on the nth syllable back, insert
 * a sound before it -- and the rest of the engine wants a flat code stream in
 * which every sound that opens a syllable occupies the first of four slots,
 * with the mark and two empty ones after it. This applies the records in
 * order, growing and rewriting the stream as it goes.
 *
 * The records are relative to what has already been laid down, so they cannot
 * be applied in any other order, and the index of syllable openers has to be
 * maintained as insertions shift everything after them. */

#define OPENERS 96

static int a1(const bst_image *img, int c) { return bst_ph_attr1(img, c); }

static int modmap(const bst_image *img, int v) {
    const uint8_t *p = bst_at(img, img->t.modmap + (unsigned)(v & 0xFF), 1);
    return p ? bst_uncode(img, *p) : 0;
}

/* The variant of a sound a modifier selects, or -1 when there is none, which
   means the record deletes rather than rewrites. */
static int variant(const bst_image *img, int sound, int mod) {
    /* The table is indexed in the module's own numbering, not the library's. */
    const uint8_t *e = bst_at(img,
                              img->t.modtab + (unsigned)bst_code(img, sound) * 4, 4);
    if (!e) return -1;
    uint32_t rows = img->t.modtab_rows ? img->t.modtab_rows
                                       : (img->t.modtab & 0xFFFF0000u);
    uint32_t va = img->t.near_ptrs
                ? (rows | (unsigned)(e[0] | (e[1] << 8)))
                : (uint32_t)(e[0] | (e[1] << 8) | (e[2] << 16) | (e[3] << 24));
    if (!va) return -1;
    const uint8_t *p = bst_at(img, va + (unsigned)(mod & 0xFF), 1);
    if (!p) return -1;
    return (int8_t)*p == -1 ? -1 : bst_uncode(img, *p);
}

typedef struct {
    const bst_image *img;
    bst_builder     *b;
    uint8_t         *s;       /* b->buf + 2, where the stream proper starts */
    int16_t          open[OPENERS];
    int              nopen;
} ws;

/* Turns phoneme codes into records. A code whose attribute opens a group takes
   three slots, one before it, itself and one after; codes that do not open a
   group are written into whichever of those slots their attributes select. */
void bst_build_init(bst_builder *b) {
    memset(b, 0, sizeof *b);
    b->before = b->at = b->after = -1;
}

void bst_build_emit(const bst_image *img, bst_builder *b, int code) {
    if (b->stopped) return;
    int p = b->pos;
    if (p >= 0x60) { b->stopped = 1; return; }

    int a1 = bst_ph_attr1(img, code);
    int a2 = bst_ph_attr2(img, code);
    int opens = (a1 & 0x80) || code == 0x4C || code == 0x49;

    if (!opens) {
        int special = code > 0x75 && code < 0x7C;
        if (!(a2 & 2) && !special) {
            if (!(a2 & 4)) {
                p = b->pos;
                b->pos = p + 1;
                b->before = b->at = b->after = -1;
            } else if (b->at == -1) {
                if (b->after == -1) return;
                p = b->after;
                b->after = -1;
            } else {
                p = b->at;
                b->before = b->at = -1;
            }
        } else {
            p = b->before;
            if (p == -1) return;
            b->before = -1;
        }
    } else {
        b->before = p + 1;
        b->at = p + 2;
        b->after = p + 3;
        b->pos = p + 4;
        for (int k = 3; k <= 5; k++)
            if (p + k < (int)sizeof b->buf) b->buf[p + k] = 0;
    }
    if (p + 2 >= 0 && p + 2 < (int)sizeof b->buf) b->buf[p + 2] = (unsigned char)code;
}

/* Puts back the sound of a suffix the normaliser removed. The flag byte is
   walked from the top bit down, and each suffix appends codes chosen by what
   the stem ended on: "churches" gets a different plural from "dogs". */
void bst_build_suffix(const bst_image *img, bst_builder *b, int flags, int y_from_i) {
    if (img->t.strip_kind == 1) {
        /* The German set: three codes whatever came off, then one that says
           which did. */
        for (int slot = 0; flags; slot++, flags = (flags << 1) & 0xFF) {
            if (!(flags & 0x80)) continue;
            bst_build_emit(img, b, bst_uncode(img, 0x51));
            bst_build_emit(img, b, bst_uncode(img, 0x2F));
            bst_build_emit(img, b, bst_uncode(img, 0x7E));
            if (img->t.suffix_tail[slot])
                bst_build_emit(img, b, img->t.suffix_tail[slot]);
        }
        return;
    }
    for (int slot = 0; flags; slot++, flags = (flags << 1) & 0xFF) {
        if (!(flags & 0x80)) continue;
        int last = (b->pos + 1 < (int)sizeof b->buf) ? b->buf[b->pos + 1] : 0;
        int code;

        switch (slot) {
        case 0:                                   /* -ed */
            bst_build_emit(img, b, 0x49);
            if (last == 0x18 || last == 0x14) {
                bst_build_emit(img, b, 0x24);
                bst_build_emit(img, b, 0x76);
                code = 0x14;
            } else if (last != 0 && (last < 4 || last == 0x0D || last == 0x1C ||
                                     last == 0x16 || last == 0x0B)) {
                code = 0x18;
            } else {
                code = 0x14;
            }
            break;
        case 1:                                   /* -ing */
            bst_build_emit(img, b, 0x49);
            bst_build_emit(img, b, 0x29);
            bst_build_emit(img, b, 0x76);
            code = 0x10;
            break;
        case 2:                                   /* -ly */
            if (last == 0x11) {
                code = 0x49;
            } else {
                /* When the stem's i became a y, a preceding vowel marker is
                   promoted before the suffix goes on. */
                if (y_from_i && b->pos >= 2 && b->buf[b->pos - 2] == 0x23)
                    b->buf[b->pos - 2] = 0x24;
                bst_build_emit(img, b, 0x49);
                code = 0x11;
            }
            bst_build_emit(img, b, code);
            bst_build_emit(img, b, 0x23);
            code = 0x76;
            break;
        case 3:                                   /* -s */
        case 4:                                   /* -'s */
            bst_build_emit(img, b, 0x49);
            if (last < 0x0E && ((1u << (last & 0x1F)) & 0x38C8u)) {
                bst_build_emit(img, b, 0x29);
                bst_build_emit(img, b, 0x76);
                code = 6;
            } else if (last < 0x1D && (last > 0x15 || last == 1 || last == 2)) {
                code = 0x0D;
            } else {
                code = 6;
            }
            break;
        default:
            return;
        }
        bst_build_emit(img, b, code);
    }
}


/* Rebuilds the opener index over the whole stream. */
static void reindex(ws *w) {
    w->nopen = 0;
    int i = 0;
    if (w->b->pos != 0) {
        do {
            int b = w->s[i];
            if (a1(w->img, b) & 0x80) {
                if (w->nopen < OPENERS - 1) w->open[w->nopen++] = (int16_t)i;
                i += 4;
            } else if (a1(w->img, b) & 1) {
                if (w->nopen < OPENERS - 1) w->open[w->nopen++] = (int16_t)i;
                i += 1;
            } else if (b == 0x4C || b == 0x49) {
                i += 4;
            } else {
                i += 1;
            }
        } while (i < w->b->pos);
    }
    w->open[w->nopen] = (int16_t)i;
    if (i < (int)(sizeof w->b->buf - 2)) w->s[i] = 0;
}

/* Counts back over n openers of a kind, and returns the index into the opener
   table rather than the position, because the caller wants both. */
static int back_vowels(ws *w, int n, int k) {
    if (n < 1) return k;
    do {
        k--;
        if (k < 0) return 0;
        if (a1(w->img, w->s[w->open[k]]) & 1) n--;
    } while (n > 0);
    return k;
}

static int back_openers(ws *w, int n, int k) {
    if (n < 1) return k;
    do {
        k--;
        if (k < 0) return 0;
        if (a1(w->img, w->s[w->open[k]]) & 0x80) n--;
    } while (n > 0);
    return k;
}

static void insert(ws *w, int at, int sound, int wide) {
    int grow = wide ? 4 : 1;
    if (w->b->pos + grow >= (int)(sizeof w->b->buf - 2)) { w->b->stopped = 1; return; }
    for (int u = w->b->pos; u >= at; u--) w->s[u + grow] = w->s[u];
    w->s[at] = (uint8_t)sound;
    if (wide) { w->s[at + 1] = 0; w->s[at + 2] = 0; w->s[at + 3] = 0; }
    w->b->pos += grow;
}

int bst_recs_to_stream(const bst_image *img, const bst_rec *rec, int nrec,
                       bst_builder *b, int suffix, int *stress_seen,
                       int *accent_seen) {
    ws w;
    memset(&w, 0, sizeof w);
    w.img = img;
    w.b = b;
    w.s = b->buf + 2;
    reindex(&w);

    int f6 = stress_seen ? *stress_seen : 0;
    int f5 = accent_seen ? *accent_seen : 0;

    for (int i = 0; i < nrec && !w.b->stopped; i++) {
        int a = rec[i].a, op = rec[i].b;
        switch (rec[i].type) {
        case 'T':
            w.b->pos = 0;
            w.s[0] = 0;
            w.b->before = w.b->at = w.b->after = -1;
            reindex(&w);
            break;
        case 'X':
            bst_build_emit(img, w.b, a);
            break;
        case 'F':
            /* The flags byte the engine keeps in front of the stream; nothing
               downstream of here reads it. */
            b->buf[1] = (unsigned char)a;
            break;
        case 'S': {
            w.nopen = back_openers(&w, a, w.nopen);
            int c = (op == 0) ? '6' : (op == 1) ? '5' : (op == 2) ? '3' : op;
            if (c == '6') {
                if (!suffix) f6 = 1;
                else if (f6) {
                    for (int k = 0; k < w.b->pos; k++) if (w.s[k] == '6') w.s[k] = 0;
                    f6 = 0;
                }
            } else if (c == '5') {
                if (!suffix) f5 = 1;
                else if (f5) {
                    for (int k = 0; k < w.b->pos; k++) if (w.s[k] == '5') w.s[k] = 0;
                    f5 = 0;
                }
            }
            int p = w.open[w.nopen] + 1;
            if (p >= 0 && p < (int)(sizeof w.b->buf - 2)) w.s[p] = (uint8_t)c;
            break;
        }
        case 'V': case 'C': {
            w.nopen = (rec[i].type == 'C') ? back_vowels(&w, a + 1, w.nopen)
                                           : back_openers(&w, a + 1, w.nopen);
            int p = w.open[w.nopen];
            int c = variant(img, w.s[p], op);
            if (c == -1) {
                if (rec[i].type == 'C') w.s[p] = 0;
                else { w.s[p] = 0; w.s[p + 1] = 0; w.s[p + 2] = 0; w.s[p + 3] = 0; }
            } else {
                w.s[p] = (uint8_t)c;
            }
            break;
        }
        case 'P': {
            w.nopen -= a;
            if (w.nopen < 0) w.nopen = 0;
            int sound = modmap(img, op);
            int at = w.open[w.nopen];
            int wide = (a1(img, sound) & 0x80) || sound == 0x4C || sound == 0x49;
            insert(&w, at, sound, wide);
            break;
        }
        default:
            i = nrec;
            break;
        }
    }

    if (stress_seen) *stress_seen = f6;
    if (accent_seen) *accent_seen = f5;
    return w.b->stopped ? -1 : w.b->pos;
}
