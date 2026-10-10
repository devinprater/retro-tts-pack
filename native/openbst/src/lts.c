#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* Letter-to-sound rules, in the NRL style. At each position the engine tries a
   two-letter key then a one-letter key, hashes it modulo 237 into a dispatch
   table, and walks a chain of candidate rules. A rule fires when its right
   context matches forwards from just past the key and its left context matches
   backwards from just before it.
 *
 * Rules carry a priority, lower being better. Both key lengths are tried and
 * the best priority wins, so a one-letter rule can beat a two-letter one, and
 * within a chain the walk stops at the first rule whose priority is no better
 * than the best so far. */

static int ru8(const bst_image *img, uint32_t va)  { return bst_u8(img, va, 0); }
static int rs16(const bst_image *img, uint32_t va) { return bst_s16(img, va, 0); }
static uint32_t ru32(const bst_image *img, uint32_t va) { return bst_u32(img, va, 0); }
static int ru16(const bst_image *img, uint32_t va) { return bst_u16(img, va, 0); }

/* The suffix strings live in the writable section and the rule patterns in the
   read-only one, but both are just addresses to bst_at. */
static const char *pat_at(const bst_image *img, uint32_t va) {
    const uint8_t *p = bst_at(img, va, 1);
    return p ? (const char *)p : "";
}
#define dstr pat_at

static int is_letter(const bst_image *img, int c) {
    return c > 0 && c < 256 && (ru8(img, img->t.chattr + (unsigned)c) & 8);
}
static int lattr(const bst_image *img, int c) {
    return (c > 0 && c < 256) ? ru8(img, img->t.letterattr + (unsigned)c) : 0;
}


/* Walks one side of a pattern. step is +1 for the right context, -1 for the
   left. */
static int match(const bst_image *img, const char *pat, int pi,
                 const unsigned char *t, int len, int ti, int step) {
    int ext = img->t.pat_ext;
    while (pi >= 0 && pat[pi]) {
        char pc = pat[pi];
        if (pc == ')') { pi++; continue; }
        int tc = (ti >= 0 && ti < len) ? t[ti] : 0;

        if (is_letter(img, (unsigned char)pc) || pc == '`') {
            if ((unsigned char)pc != tc) return 0;
            pi += step; ti += step; continue;
        }
        if ((unsigned char)pc < 0x60 && img->t.pat_bit[(unsigned char)pc]) {
            if (!(lattr(img, tc) & img->t.pat_bit[(unsigned char)pc])) return 0;
            pi += step; ti += step; continue;
        }
        switch (pc) {
        case ' ':
            if (tc && is_letter(img, tc)) return 0;
            if (ext && tc == '`') return 0;
            pi += step; ti += step; continue;
        case '&':
            /* A doubled consonant, which eats two letters. */
            if (!ext) return 0;
            if (!(lattr(img, tc) & 4)) return 0;
            ti += step;
            if (tc != ((ti >= 0 && ti < len) ? t[ti] : 0)) return 0;
            pi += step; ti += step; continue;
        case '$': {
            /* One of six two-letter clusters. The right-context form walks
               the pair backwards and leaves the walk going that way, which
               is what the original does. */
            if (!ext) return 0;
            pi += step;
            if (step == 1) { ti++; step = -1; tc = (ti >= 0 && ti < len) ? t[ti] : 0; }
            int nx;
            ti += step;
            nx = (ti >= 0 && ti < len) ? t[ti] : 0;
            if (tc == 'h')      { if (nx != 'c' && nx != 't' && nx != 'p') return 0; }
            else if (tc == 'u') { if (nx != 'q' && nx != 'g') return 0; }
            else if (tc == 'n') { if (nx != 'g') return 0; }
            else if (tc == 'c') { if (nx != 's') return 0; }
            else return 0;
            ti += step;
            continue;
        }
        case '#':
            if (!(lattr(img, tc) & 1)) return 0;
            pi += step; ti += step; continue;
        case '^':
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 4)))
                return 0;
            pi += step; ti += step; continue;
        case ':':
            while (tc && is_letter(img, tc) && (lattr(img, tc) & 4)) {
                ti += step;
                tc = (ti >= 0 && ti < len) ? t[ti] : 0;
            }
            pi += step; continue;
        case '+':
            if (tc != 'i' && tc != 'e' && tc != 'y') return 0;
            pi += step; ti += step; continue;
        case '.':
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 2))) return 0;
            pi += step; ti += step; continue;
        case '%': {
            /* As many consecutive letters as the build's longest suffix,
               matched whole against one of its stored suffixes. They live in
               .data, not .rdata. A build with no suffixes of its own keeps a
               single empty one, which every position matches. */
            char got[8];
            int cap = img->t.suffix_max;
            int n = img->t.suffix_n;
            int g = 0, k = ti;
            while (g < cap && k >= 0 && k < len && is_letter(img, t[k])) got[g++] = (char)t[k++];
            got[g] = 0;
            int hit = 0;
            for (int s = 0; s < n && !hit; s++) {
                uint32_t e = img->t.suffix_ptrs + (uint32_t)s * 4;
                uint32_t a = img->t.near_ptrs
                           ? (img->t.suffix_ptrs & 0xFFFF0000u) | (unsigned)ru16(img, e)
                           : ru32(img, e);
                if (strcmp(got, dstr(img, a)) == 0) hit = 1;
            }
            if (!hit) return 0;
            ti += g - 1;
            pi += step;
            continue;
        }
        case '{': {
            /* Nothing but the build's own prefixes stands between here and
               the head of the word. They are taken off one at a time. */
            if (!img->t.prefixes) return 0;
            int lim = img->t.prefix_max ? img->t.prefix_max : 8;
            for (;;) {
                unsigned char run[16];
                int k = 0, p = ti;
                while (k < lim && p >= 0) {
                    int ch = p < len ? t[p] : 0;
                    if (!ch || !is_letter(img, ch)) break;
                    run[k++] = (unsigned char)ch;
                    p--;
                }
                int hit = 0;
                for (int q = 0; q < img->t.prefix_n && !hit; q++) {
                    const uint8_t *e = bst_at(img, img->t.prefixes +
                                              (uint32_t)q * 4, 4);
                    if (!e) continue;
                    uint32_t va = (uint32_t)(e[0] | (e[1] << 8) |
                                             (e[2] << 16) | (e[3] << 24));
                    const uint8_t *w = va ? bst_at(img, va, 1) : NULL;
                    if (!w || !*w) continue;
                    int n = 0;
                    while (w[n]) n++;
                    if (n > k) continue;
                    int same = 1;
                    for (int j = 0; j < n && same; j++)
                        if (w[j] != run[n - 1 - j]) same = 0;
                    if (same) { ti -= n; hit = 1; }
                }
                if (!hit) return 0;
                if (ti <= 0) break;
            }
            pi += step;
            continue;
        }
        case '@':
            if (tc == 'h') {
                int prev = ti > 0 ? t[ti - 1] : 0;
                if (prev != 't' && prev != 'c' && prev != 's') return 0;
                pi--; ti -= 2; continue;
            }
            if (!(tc && is_letter(img, tc) && (lattr(img, tc) & 0x10))) return 0;
            pi--; ti--; continue;
        default:
            return 0;
        }
    }
    return 1;
}

static int chain_head(const bst_image *img, int key) {
    /* The key is a signed sixteen-bit value, so a letter above 0x7F in the
       high byte makes it negative, but the bucket comes from the unsigned
       form. */
    int bucket = (key & 0xFFFF) % 0xED;
    uint32_t o = img->t.lts_index +
                 (uint32_t)bucket * img->t.lts_index_stride;
    int entry_off = rs16(img, o);
    int count = ru8(img, o + 2);
    if (entry_off < 0) return -1;
    for (int k = 0; k < count; k++) {
        uint32_t p = img->t.dispatch + (uint32_t)entry_off * 4 + (uint32_t)k * 4;
        int rkey = rs16(img, p);
        int ridx = rs16(img, p + 2);
        if (rkey == key) return ridx;
        if (rkey < key) break;
    }
    return -1;
}

static void rule_fields(const bst_image *img, int ridx,
                        int *prio, int *next, int *pat, int *out) {
    uint32_t r = img->t.rules + (uint32_t)ridx * img->t.rule_stride;
    int w = img->t.rule_prio_w;
    *prio = w == 1 ? ru8(img, r) : rs16(img, r);
    *next = rs16(img, r + (uint32_t)w);
    *pat  = rs16(img, r + (uint32_t)w + 2);
    *out  = rs16(img, r + (uint32_t)w + 4);
}

static int try_at(const bst_image *img, const unsigned char *t, int len, int pos,
                  int *used_out) {
    int best = -1, threshold = 0x240;
    *used_out = 1;

    for (int span = 2; span >= 1; span--) {
        if (pos + span > len) continue;
        if (span == 2 && !(is_letter(img, t[pos + 1]) || t[pos + 1] == 0x60)) continue;
        int key = span == 1 ? t[pos]
                            : (int16_t)(t[pos] | (t[pos + 1] << 8));

        for (int ridx = chain_head(img, key); ridx >= 0;) {
            int prio, next, patoff, outoff;
            rule_fields(img, ridx, &prio, &next, &patoff, &outoff);
            if ((prio & 0xFF) >= threshold) {
                if (bst_trace)
                    bst_tracef("  cand pos=%d span=%d ridx=%d prio=%04x"
                               " over threshold %04x\n",
                            pos, span, ridx, prio, threshold);
                break;
            }

            const char *pat = pat_at(img, img->t.patterns + (uint32_t)patoff);
            const char *tp = strchr(pat, 'T');
            const char *cp = strchr(pat, ')');
            if (!tp || !cp) break;
            int ti = (int)(tp - pat), ci = (int)(cp - pat);

            int ok = match(img, pat, ti + 1, t, len, pos + span, 1) &&
                     match(img, pat, ti - 1, t, len, pos - 1, -1);
            if (bst_trace)
                bst_tracef("  cand pos=%d span=%d ridx=%d prio=%04x pat='%s' %s\n",
                        pos, span, ridx, prio, pat, ok ? "MATCH" : "no");
            if (ok) {
                best = ridx;
                *used_out = span + (ci - ti - 1);
                threshold = prio & 0xFF;
                break;
            }
            ridx = next >= 0 ? next : -1;
        }
    }
    return best;
}

/* The rule pass on its own, into a builder the caller owns. A dictionary
   entry is a set of corrections to this rather than a replacement for it, so
   both paths start here unless the entry says to throw it away. */
void bst_lts_build(const bst_image *img, const bst_word *w, bst_builder *bp,
                   int indict) {
    const unsigned char *t = (const unsigned char *)w->buf;
    int len = w->len;
    bst_builder b = *bp;

    int pos = 1;
    while (pos < len && t[pos] != '_' && t[pos]) {
        /* The shortcut is the rule table's stand-in for a letter it has no
           chain for, and a dictionary entry speaks for the whole word, so the
           engine turns it off for the words it knows. */
        if (img->t.lts_lit_char && !indict && t[pos] == img->t.lts_lit_char) {
            bst_build_emit(img, &b, bst_uncode(img, img->t.lts_lit_code));
            pos++;
            continue;
        }
        int used = 1;
        int ridx = try_at(img, t, len, pos, &used);
        if (ridx < 0) { pos++; continue; }

        int prio, next, patoff, outoff;
        rule_fields(img, ridx, &prio, &next, &patoff, &outoff);
        const char *o = pat_at(img, img->t.outputs + (uint32_t)outoff);
        if (bst_trace)
            bst_tracef("lts pos=%d used=%d ridx=%d outoff=%04x at=%08x\n",
                    pos, used, ridx, outoff, img->t.outputs + (unsigned)outoff);
        for (; *o; o++) bst_build_emit(img, &b, bst_uncode(img, (unsigned char)*o));
        pos += used > 0 ? used : 1;
    }
    *bp = b;
}

/* Copies a builder out as the stream form the caller sees: the flag byte
   first, then the codes. */
static void build_out(const bst_builder *b, bst_stream *out) {
    memset(out, 0, sizeof *out);
    int n = b->pos + 2;
    if (n > BST_STREAM_MAX - 1) n = BST_STREAM_MAX - 1;
    for (int i = 1; i <= n; i++) out->buf[i - 1] = b->buf[i];
    out->len = n;
}

void bst_lts(const bst_image *img, const bst_word *w, bst_stream *out) {
    bst_builder b;
    bst_build_init(&b);
    bst_lts_build(img, w, &b, 0);
    if (w->flags) bst_build_suffix(img, &b, w->flags, w->y_from_i);
    build_out(&b, out);
}

/* A dictionary entry keeps its suffix pronunciations after the main records,
   each introduced by an 'A' record naming which suffix it is. */
static int suffix_block(const bst_recs *r, int flags) {
    int want = (flags & 0xF8) >> 3;
    for (int i = 0; i < r->n; i++) {
        if (!r->rec[i].type) break;
        if (r->rec[i].type == 'A' && r->rec[i].a == want) return i + 1;
    }
    return -1;
}

int bst_word_pronounce(const bst_image *img, const char *word,
                       bst_recs *recs, bst_stream *stream) {
    bst_word w;
    bst_normalise(img, word, &w);

    memset(recs, 0, sizeof *recs);
    memset(stream, 0, sizeof *stream);

    if (bst_dict_lookup(img, &w, recs)) {
        bst_builder b;
        bst_build_init(&b);
        /* Unless the entry opens by throwing it away, the records correct
           what the rules produce rather than replacing it. */
        if (recs->n == 0 || recs->rec[0].type != 'T')
            bst_lts_build(img, &w, &b, 1);
        int stress = 0, accent = 0;
        bst_recs_to_stream(img, recs->rec, recs->n, &b, 0, &stress, &accent);
        if (w.flags) {
            bst_build_suffix(img, &b, w.flags, w.y_from_i);
            int k = suffix_block(recs, w.flags);
            if (k >= 0)
                bst_recs_to_stream(img, recs->rec + k, recs->n - k, &b, 1,
                                   &stress, &accent);
        }
        build_out(&b, stream);
        return 1;
    }
    bst_lts(img, &w, stream);
    return 0;
}
