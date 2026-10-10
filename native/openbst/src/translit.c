#include <string.h>
#include "bst_text.h"

/* The Arabic build reads its text as bytes in its own code page and rewrites
 * it into a Latin spelling before anything else looks at it, because its
 * letter-to-sound rules are written in that spelling. The rewrite is not one
 * character to one: a letter carrying a doubling mark comes out twice, the
 * definite article becomes a marker and an l, and a long vowel written as two
 * characters comes out as one pair.
 *
 * The pass works on a three-character window of the build's own numbering, so
 * a rule can look at what follows before deciding, and says how much of the
 * window it consumed. */

typedef struct { uint8_t *p; int n, max; } abuf;

static void put(abuf *o, int c) { if (o->n < o->max) o->p[o->n++] = (uint8_t)c; }

/* One character on its own, with no context. */
static void one(const bst_image *img, abuf *o, int c) {
    switch (c) {
    case 0xA5: put(o, 0x82); put(o, 'q'); put(o, 'c'); return;
    case 0xA6: put(o, 'q'); put(o, 'c'); put(o, 'a'); put(o, 'a'); return;
    case 0xA7: put(o, 0x88); put(o, 'q'); put(o, 'c'); return;
    case 0xA8: put(o, 0x89); put(o, 'q'); put(o, 'c'); return;
    case 0xA9: put(o, 0x8A); put(o, 'q'); put(o, 'c'); return;
    default: break;
    }
    if (c >= 0x99) {
        int a = bst_u8(img, img->t.xlat_first, (unsigned)c);
        if (!a) return;
        put(o, a);
        int b = bst_u8(img, img->t.xlat_second, (unsigned)c);
        if (b) put(o, b);
        return;
    }
    if (c >= 0x81 && c <= 0x98) {
        if (c == 0x81 || c == 0x85 || c == 0x87) { put(o, ' '); return; }
        if (c == 0x97) { put(o, 0x8C); return; }
        if (c == 0x98) return;
    }
    put(o, c);
}

/* The build's own test for a sound that can carry a doubling mark. */
static int cons(int c) { return c != 0xAB && c > 0xA4; }

/* The vowel marks, which stop a long vowel from being written out twice. */
static int mark(int c) { return c >= 0x97 && c <= 0xA4; }

static int step(const bst_image *img, abuf *o, const uint8_t *w, int *flag) {
    int a = w[0], b = w[1], c = w[2];

    if (a < 0x80) { put(o, a); *flag &= ~2; return 1; }

    if (b == 0x98) {                      /* a bare doubling mark */
        one(img, o, a);
        if (cons(a)) one(img, o, a);
        *flag |= 2;
        return 2;
    }
    if (b == 0xF5) {                      /* the l of the definite article */
        if (a == 0xAB) {
            if (c == 0x97) { put(o, 0xA0); put(o, 'l'); put(o, 0x8C); *flag |= 2; return 3; }
            if (*flag & 2) { put(o, 'a'); put(o, 'a'); put(o, 'l'); *flag |= 2; return 2; }
            if (c == 0x9A) {
                put(o, 'q'); put(o, 'c'); put(o, 'a');
                put(o, 'l'); put(o, 'l'); put(o, 'a');
                *flag |= 2;
                return 3;
            }
            put(o, 0xA0); put(o, 'l'); *flag |= 2; return 2;
        }
    } else if (b == 0xAB) {               /* a following long a */
        if (a == 0x9C) { put(o, 'a'); put(o, 'n'); *flag |= 2; return 2; }
        if (a == 0x99) { put(o, 'a'); put(o, 'a'); *flag |= 2; return 2; }
    } else if (b == 0x99) {
        if (a == 0xA7) { put(o, 'q'); put(o, 'c'); put(o, 'a'); *flag |= 2; return 2; }
        if (a == 0xAB) { put(o, 'a'); *flag |= 2; return 2; }
    } else if (b == 0x9B) {
        if (a == 0xAB) { put(o, 'a'); put(o, 'n'); *flag |= 2; return 2; }
        *flag |= 2;
        return 1;
    } else if (b == 0xFC) {
        if (a == 0x99) { one(img, o, 0x99); put(o, 0x98); }
        else           { one(img, o, a); put(o, 0x98); put(o, 'a'); }
        put(o, 'a');
        *flag |= 2;
        return 2;
    } else if (b == 0xFB) {               /* a following w, which may be a long u */
        if (a == 0x9D) {
            if (mark(c) || c == a) { one(img, o, 0x9D); *flag |= 2; return 1; }
            put(o, 'u'); put(o, 'u'); *flag |= 2; return 2;
        }
        if (cons(a) && cons(c)) {
            one(img, o, a); put(o, 'u'); put(o, 'u'); one(img, o, c);
            *flag |= 2;
            return 3;
        }
    } else if (b == 0xFD) {               /* a following y, which may be a long i */
        if (a == 0xA1) {
            if (mark(c) || c == a) { one(img, o, 0xA1); *flag |= 2; return 1; }
            put(o, 'i'); put(o, 'i'); *flag |= 2; return 2;
        }
        if (cons(a) && cons(c)) {
            one(img, o, a); put(o, 'i'); put(o, 'i'); one(img, o, c);
            *flag |= 2;
            return 3;
        }
    } else if (b >= 0x9A && b <= 0xA4 && !(b & 1)) {
        /* a doubling mark carrying its own vowel */
        one(img, o, a);
        one(img, o, a);
        switch (b) {
        case 0x9A: if (c != 0xAB) put(o, 'a'); break;
        case 0x9C: put(o, 'a'); put(o, 'n'); break;
        case 0x9E: put(o, 'u'); break;
        case 0xA0: put(o, 'u'); put(o, 'n'); break;
        case 0xA2: put(o, 'i'); break;
        case 0xA4: put(o, 'i'); put(o, 'n'); break;
        default: break;
        }
        *flag |= 2;
        return 2;
    }

    if (a >= 0x9A && a <= 0xA2) {
        if (a & 1) one(img, o, a);
    } else {
        one(img, o, a);
    }
    *flag |= 2;
    return 1;
}

int bst_translit(const bst_image *img, const char *in, char *out, int max) {
    if (img->t.xlat_kind != 1) return -1;

    /* The code page first, joining a doubling mark to the vowel after it. */
    uint8_t code[BST_XLAT_MAX];
    int n = 0;
    for (const uint8_t *p = (const uint8_t *)in; *p && n < BST_XLAT_MAX - 4; p++) {
        int v = img->t.xlat_map[*p];
        if (v == 0x98 && p[1]) {
            static const uint8_t pair[7] = { 0x9C, 0xA0, 0xA4, 0x9A, 0, 0x9E, 0xA2 };
            int d = img->t.xlat_map[p[1]];
            int k = -1;
            switch (d) {
            case 0x9B: k = 0; break;
            case 0x9F: k = 1; break;
            case 0xA3: k = 2; break;
            case 0x99: k = 3; break;
            case 0x9D: k = 5; break;
            case 0xA1: k = 6; break;
            default: break;
            }
            if (k >= 0) { v = pair[k]; p++; }
        }
        code[n++] = (uint8_t)v;
    }
    code[n++] = 0x2E;                    /* the stop the build adds itself */
    code[n++] = ' ';
    code[n++] = ' ';
    code[n] = 0;

    abuf o = { (uint8_t *)out, 0, max - 1 };
    int flag = 0;
    for (int i = 0; i < n;) {
        uint8_t w[3];
        for (int k = 0; k < 3; k++) w[k] = (i + k < n) ? code[i + k] : 0;
        i += step(img, &o, w, &flag);
    }
    out[o.n] = 0;
    return o.n;
}
