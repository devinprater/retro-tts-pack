#include "bst_text.h"

/* The cursor scans the sentence stages share. Each walks the stream from a
   starting position, skips empty slots, steps over the six-byte command
   records, and stops on the first byte carrying a given attribute bit. */

#define CMD 0x7C

int bst_ph_attr1(const bst_image *img, int c) {
    return bst_u8(img, img->t.phattr1, bst_code(img, c & 0xFF));
}
int bst_ph_attr2(const bst_image *img, int c) {
    return bst_u8(img, img->t.phattr2, bst_code(img, c & 0xFF));
}

void bst_scan_seg(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (bst_ph_attr2(img, b) & 1) { c->val = b; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim - 1 || b == 0x4D || b == 0x4E) { c->val = 0; c->pos = 0; return; }
    }
}

void bst_scan_stress(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (bst_ph_attr2(img, b) & 2) { c->val = b - 0x30; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim - 1) { c->val = 0; c->pos = 0; return; }
    }
}

void bst_scan_eight(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q + 1;
            if (p > lim) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (bst_ph_attr2(img, b) & 8) { c->val = b; c->pos = p; return; }
        if (b == CMD) { p = q + 7; continue; }
        if (p >= lim) { c->val = 0; c->pos = 0; return; }
    }
}

void bst_scan_eight_back(const bst_image *img, const uint8_t *s, int from, bst_cur *c) {
    int p = from;
    for (;;) {
        int q;
        do {
            q = p; p = q - 1;
            if (p < 0) { c->val = 0; c->pos = 0; return; }
        } while (s[p] == 0);
        int b = s[p];
        if (bst_ph_attr2(img, b) & 8) { c->val = b; c->pos = p; return; }
        if (b == CMD) { p = q - 7; continue; }
        if (p < 0) { c->val = 0; c->pos = 0; return; }
    }
}

/* Repeats the segment scan until it lands on a sound that opens a group, or
   runs out of stream. */
void bst_scan_strong(const bst_image *img, const uint8_t *s, int lim, int from, bst_cur *c) {
    bst_cur t = { 0, from };
    do {
        bst_scan_seg(img, s, lim, t.pos, &t);
        if (bst_ph_attr1(img, t.val) & 0x80) break;
    } while (t.val != 0);
    *c = t;
}
