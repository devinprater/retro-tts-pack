#include <stdio.h>
#include <string.h>
#include "bst_text.h"

/* The pronunciation dictionary: a sorted, front-coded, nibble-packed word list
   in fifteen buckets. A word is rewritten as a stream of 4-bit codes, one to
   three per letter, through one table for the initial letter and another for
   the rest; the first nibble picks the bucket. Entries store how many nibbles
   they share with the entry before them, so only the differing tail is kept. */

static int u8at(const bst_image *img, uint32_t va)  { return bst_u8(img, va, 0); }
static int u16at(const bst_image *img, uint32_t va) { return bst_u16(img, va, 0); }
static int s16at(const bst_image *img, uint32_t va) { return bst_s16(img, va, 0); }

/* Nibble i of the entry at va. */
static int nib(const bst_image *img, uint32_t va, int i) {
    int b = u8at(img, va + (i >> 1));
    return (i & 1) ? (b & 0xF) : (b >> 4);
}

/* Encodes a word into the engine's nibble form: count, nibbles, silent-e flag. */
static int encode(const bst_image *img, const char *stem, uint8_t *out, int max) {
    char w[BST_WORD_MAX];
    int n = 0;
    for (; stem[n] && n < BST_WORD_MAX - 1; n++) w[n] = stem[n];
    w[n] = 0;

    int flag = 0;
    int silent = img->t.dict_silent ? img->t.dict_silent : 'e';
    if (n > 0 && w[n - 1] == silent) { n--; flag = 1; }

    uint8_t nb[BST_WORD_MAX * 3];
    int k = 0;
    for (int i = n - 1; i >= 0 && k + 3 < (int)sizeof nb; i--) {
        int idx = u8at(img, img->t.symmap + (unsigned char)w[i]) - 1;
        if (idx > 0x34) idx = 0x34;
        if (idx < 0) idx = 0;
        int v = u16at(img, (i == 0 ? img->t.code_initial : img->t.code_medial) + idx * 2);
        nb[k++] = (uint8_t)(v & 0xF);
        if (v > 0xF) {
            nb[k++] = (uint8_t)((v >> 4) & 0xF);
            if (v > 0xFF) nb[k++] = (uint8_t)((v >> 8) & 0xF);
        }
    }

    /* The engine builds [flag, nibbles...] backwards then reverses the lot, so
       the count leads and the flag trails. */
    int total = k + 1;
    if (total + 1 > max) return 0;
    out[0] = (uint8_t)total;
    for (int i = 0; i < k; i++) out[1 + i] = nb[k - 1 - i];
    out[1 + k] = (uint8_t)flag;
    int lead = img->t.dict_lead_max ? img->t.dict_lead_max : 8;
    if (total > 2 && out[1] == 0x0F && out[2] < lead) out[1] = 1;
    return total;
}

static void entry_header(const bst_image *img, uint32_t va,
                         int *total, int *shared, int *used) {
    int b = u8at(img, va);
    uint32_t p;
    if ((b & 0xF0) == 0xF0) {
        if (b == 0xFF) { *total = (u8at(img, va + 1) >> 4) + 0x20; *used = 3; p = va + 1; }
        else           { *total = (b & 0xF) + 0x11;                *used = 2; p = va + 1; }
    } else {
        *total = (b >> 4) + 2; *used = 1; p = va;
    }
    if (*used & 1) {
        if ((u8at(img, p) & 0xF) == 0xF) { *shared = (u8at(img, p + 1) >> 4) + 0xF; *used += 2; }
        else                             { *shared = u8at(img, p) & 0xF;            *used += 1; }
    } else {
        int v = u8at(img, p);
        if ((v & 0xF0) == 0xF0) { *shared = (v & 0xF) + 0xF; *used += 2; }
        else                    { *shared = v >> 4;          *used += 1; }
    }
}

/* 0 means this entry is the word; positive is its length, meaning step past
   it; negative means the search has gone too far. */
static int compare(const bst_image *img, const uint8_t *word, uint32_t va, int *state) {
    int total, shared, used;
    entry_header(img, va, &total, &shared, &used);
    if (shared < *state) return -1;
    if (*state < shared) return total;

    int i = shared + 1, n = used, last = total;
    while (i <= word[0]) {
        last = nib(img, va, n);
        if (word[i] != last) break;
        i++; n++;
    }
    *state = i - 1;
    if (i != word[0] + 1) return word[i] <= last ? -1 : total;
    *state = n;
    return 0;
}

static void emit_code(int code, bst_recs *out) {
    if (out->n >= BST_RECS_MAX) return;
    bst_rec *r = &out->rec[out->n];
    int top = code & 0xE00;
    switch (top) {
    case 0x000: r->type = 'T'; r->a = 0; r->b = 0; break;
    case 0x200: r->type = 'X'; r->a = (uint8_t)(code & 0xFF); r->b = 0; break;
    case 0x400: r->type = 'A'; r->a = (uint8_t)(code & 0xFF); r->b = 0; break;
    case 0x600: r->type = 'S'; r->a = (uint8_t)(code & 0xF); r->b = (uint8_t)((code >> 4) & 3); break;
    case 0x800: r->type = 'V'; r->a = (uint8_t)(code & 0xF); r->b = (uint8_t)((code >> 4) & 0xF); break;
    case 0xA00: r->type = 'C'; r->a = (uint8_t)(code & 0xF); r->b = (uint8_t)((code >> 4) & 0xF); break;
    case 0xC00: case 0xE00:
                r->type = 'P'; r->a = (uint8_t)(code & 0xF); r->b = (uint8_t)((code >> 4) & 0x3F); break;
    default: return;
    }
    out->n++;
}

static void emit_byte(int b, bst_recs *out) {
    if (out->n >= BST_RECS_MAX) return;
    bst_rec *r = &out->rec[out->n];
    switch (b & 0xE0) {
    case 0x00: r->type = 'T'; r->a = 0; r->b = 0; break;
    case 0x40: r->type = 'A'; r->a = (uint8_t)(b & 0x1F); r->b = 0; break;
    case 0x60: r->type = 'S'; r->a = (uint8_t)(b & 3); r->b = (uint8_t)((b & 0x1C) >> 2); break;
    case 0x80: r->type = 'V'; r->a = (uint8_t)(b & 3); r->b = (uint8_t)((b & 0x1C) >> 2); break;
    case 0xA0: r->type = 'C'; r->a = (uint8_t)(b & 3); r->b = (uint8_t)((b & 0x1C) >> 2); break;
    case 0xC0: r->type = 'P'; r->a = (uint8_t)(b & 3); r->b = (uint8_t)((b & 0x1C) >> 2); break;
    default: return;
    }
    out->n++;
}

/* Codes are bytes: low ones name a single record, middle ones a common pair,
   and the top range takes a second byte. */
static int decode_one(const bst_image *img, int b, int next, bst_recs *out) {
    int smax = img->t.ph_single_max ? s16at(img, img->t.ph_single_max)
                                    : img->t.ph_single_n;
    int pmax = img->t.ph_pair_max ? s16at(img, img->t.ph_pair_max)
                                  : img->t.ph_pair_n;
    if (b <= smax) { emit_code(u16at(img, img->t.ph_single + b * 2), out); return 1; }
    if (b - (smax + 1) <= pmax) {
        int pair = u16at(img, img->t.ph_pair + (b - (smax + 1)) * 2);
        emit_byte(pair & 0xFF, out);
        emit_byte(pair >> 8, out);
        return 1;
    }
    emit_code((b << 8) | next, out);
    return 2;
}

int bst_dict_lookup(const bst_image *img, const bst_word *w, bst_recs *out) {
    uint8_t enc[BST_WORD_MAX * 3 + 2];
    /* w->buf is "_stem__"; the dictionary wants the stem alone. */
    char stem[BST_WORD_MAX];
    int n = 0;
    for (int i = 1; i < w->len - 2 && n < BST_WORD_MAX - 1; i++) stem[n++] = w->buf[i];
    stem[n] = 0;

    memset(out, 0, sizeof *out);
    if (!encode(img, stem, enc, (int)sizeof enc)) return 0;

    if (bst_trace) {
        bst_tracef("dict key");
        for (int q = 0; q <= enc[0]; q++) bst_tracef(" %02x", enc[q]);
        bst_tracef("\n");
    }
    int bucket = enc[1] - 1;
    if (bucket < 0 || bucket >= 15) return 0;

    uint32_t idx_va = img->t.bucket_index[bucket], data_va = img->t.bucket_data[bucket];
    int count = u16at(img, idx_va);
    if (count < 2) return 0;

    int lo = 1, hi = count - 1, mid = 1, res = -1;
    uint32_t entry = 0;
    int state = 0;

    while (lo <= hi) {
        mid = (lo + hi) >> 1;
        state = 0;
        entry = data_va + u16at(img, idx_va + mid * 2);
        res = compare(img, enc, entry, &state);
        if (res == 0) goto found;
        if (res < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    if (res < 0) {
        if (mid == 1) return 0;
        mid--;
    }

    {
        int limit = u16at(img, idx_va + (mid + 1) * 2);
        entry = data_va + u16at(img, idx_va + mid * 2);
        state = 0;
        while (entry < data_va + (uint32_t)limit) {
            res = compare(img, enc, entry, &state);
            if (res <= 0) break;
            entry += (uint32_t)res;
        }
        if (res != 0) return 0;
    }

found:
    {
        int total, shared, used;
        entry_header(img, entry, &total, &shared, &used);
        uint32_t p = entry + (uint32_t)(state >> 1);
        if (state & 1) {
            int lo4 = u8at(img, p) & 0xF;
            if (lo4 != 0xF) decode_one(img, lo4, u8at(img, p + 1), out);
            p++;
        }
        while (p < entry + (uint32_t)total)
            p += (uint32_t)decode_one(img, u8at(img, p), u8at(img, p + 1), out);
    }
    if (bst_trace) {
        bst_tracef("dict recs");
        for (int q = 0; q < out->n; q++)
            bst_tracef(" %c%d,%d", out->rec[q].type, out->rec[q].a, out->rec[q].b);
        bst_tracef("\n");
    }
    return 1;
}
