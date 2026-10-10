#include <string.h>
#include "bst_text.h"

/* Sentence assembly: tokens to the phoneme stream a phrase is spoken from.
 *
 * The stream opens with a five-byte header saying how the previous phrase
 * ended, then a command record carrying the sentence type, then the words'
 * codes with emphasis markers between them, and closes on a marker that tells
 * the rest of the engine where the phrase stops. Punctuation does not appear
 * in the stream as itself: it chooses the closing marker, sets the sentence
 * type, and pads the end with a run of silences whose length says how long the
 * pause is.
 *
 * The command record's payload sits at fixed offsets in the stream, and the
 * later stages read the sentence type straight out of it, so the header's
 * width is load-bearing: the record has to land where they look. */

#define STREAM_MAX 0x191

static int a2(const bst_image *img, int c) { return bst_ph_attr2(img, c); }

static void put(bst_assembler *z, int c) {
    if (z->wp + 1 >= STREAM_MAX) { z->full = 1; return; }
    z->s[++z->wp] = (uint8_t)c;
}

/* The phrase header: a marker and, unless it is the "continues" marker, four
   bytes describing the break. */
static void header(bst_assembler *z, const uint8_t *rec) {
    put(z, rec[0]);
    z->hdr = z->wp;
    if (rec[0] == 'L') return;
    put(z, rec[4]);
    put(z, rec[5]);
    put(z, rec[1]);
    put(z, rec[2]);
}

static void command(bst_assembler *z, const uint8_t *rec) {
    if (z->hdr < 0) {
        static const uint8_t l[6] = { 'L', 0, 0, 0, 0, 0 };
        header(z, l);
    }
    if (z->wp + 0x2B < STREAM_MAX) {
        put(z, 0x7C);
        put(z, rec[0]);
        put(z, rec[3]);
        put(z, rec[2]);
        put(z, rec[5]);
        put(z, rec[4]);
        put(z, 0x7C);
        z->wp--;              /* the closing byte is a placeholder */
        z->wp++;
    }
}

/* Ends the phrase, or records how it broke if it does not. */
static void close_phrase(bst_assembler *z, uint8_t *rec) {
    int direct = 0;
    z->open = 0;
    if (z->hdr == 0) {
        if (rec[3] != 0) direct = 1;
        else {
            if (rec[0] < 0x4D) return;
            z->wp = -1;
            int b = rec[1];
            if (b != 0 && rec[2] == 0) { rec[1] = 0; rec[2] = (uint8_t)b; }
        }
    }
    if (!direct && rec[3] == 0) {
        if (rec[0] == 0x4E) z->punct = 2;
        if (rec[0] == 0x4D) z->punct = 1;
    }
    header(z, rec);
    if ((rec[0] != 0x4E && rec[0] != 0x4D) || z->hdr == 0) return;

    /* How this break follows the last one decides the contour's shape. */
    if (z->punct == 0) z->hist = 3;
    if (z->punct != 3) {
        if (z->hist == 6)                       z->hist = (z->punct == 1) ? 5 : 7;
        else if (z->punct == 5)                 z->hist = 6;
        else if (z->hist == 1 || z->hist == 5 || z->hist == 2)
                                                z->hist = (z->punct != 2) ? 2 : 3;
        else if (z->punct == 2)                 z->hist = 4;
        else                                    z->hist = 1;
    }
    if (z->s[6] == 'I') z->s[10] = (uint8_t)z->hist;
    z->s[z->hdr + 4] = 0;
    put(z, 0x5C);
    z->last = z->wp + 1;
    z->len = z->last;
    if (rec[2] == 0) rec[2] = rec[1];
    rec[1] = 0;
    memcpy(z->carry, rec, 6);
    z->done = 1;
}

/* Punctuation: the marker it closes on, the pause it leaves, the sentence
   type it sets. The type and the contour shape are bytes of the command
   record at the head of the stream rather than variables, because that is
   where the later stages read them.

   `forced` marks the break the stream's own size imposes rather than a mark
   in the text: that one ends the stream whatever the build does with a mark. */
static void punctuation(bst_assembler *z, int c, int forced) {
    int pause, tmpl, slot;
    switch (c) {
    case 0x2A: pause = 0; tmpl = 0x4D; z->punct = 3; z->s[8] = 5; slot = 0; break;
    case 0x2C: pause = 3; tmpl = 0x4E; z->punct = 1; z->s[8] = 3; slot = 1; break;
    case 0x2E: pause = 7; tmpl = 0x4E; z->punct = 2; z->s[8] = 4; slot = 2; break;
    case 0x3F: pause = 7; tmpl = 0x4E; z->punct = 2; z->s[8] = 1; slot = 3; break;
    case 0x40: pause = 7; tmpl = 0x4E; z->punct = 2; z->s[8] = 3; slot = 4; break;
    case 0x5D: pause = 1; tmpl = 0x4E; z->punct = 1; z->s[8] = 3; slot = 5; break;
    case 0x7B: pause = 7; tmpl = 0x4E; z->punct = 5; z->s[8] = 4; z->s[9] = 0x48;
               slot = 6; break;
    case 0x7C: pause = 1; tmpl = 0x4D; z->punct = 2; z->s[8] = 5; slot = 7; break;
    case 0x7D:
        pause = z->img->t.close_pause ? z->img->t.close_pause : 1;
        tmpl = 0x4E; z->punct = 2; z->s[8] = 4; slot = 8; break;
    default: return;
    }
    if (z->img->t.punct_s8[slot]) z->s[8] = z->img->t.punct_s8[slot];
    if (z->img->t.punct_s9[slot]) z->s[9] = z->img->t.punct_s9[slot];
    uint8_t rec[6] = { (uint8_t)tmpl, 0, 0, (uint8_t)c, 0, 0 };
    while (pause--) put(z, 0x2F);
    close_phrase(z, rec);
    /* The opening brace holds the phrase open the same way speak now does:
       the break is written into the stream and whatever follows is appended
       behind it, so the mark after it is what ends the phrase. */
    if (!forced && (c == 0x7C || c == 0x7B ||
                    (c == 0x7D && z->img->t.brace_ends_text)) &&
        z->img->t.speak_now_inline && z->done) {
        z->done = 0;
        z->open = (c == 0x7B) ? 2 : 1;
    }
}

/* The stress pass runs over the word, split where a boundary marker says the
   word is really two. */
static void stress_word(bst_assembler *z, uint8_t *b, int n) {
    int has_break = 0, has_open = 0;
    for (int i = 1; i < n; i++) {
        if (b[i] == 0x4B) has_break = 1;
        if (b[i] == 0x51) has_open = 1;
    }
    if (b[0] == 0x51 && has_break) {
        int p = 1;
        if (p >= n) return;
        while (b[p] != 0x4B && b[p] != 0x4C) { if (++p >= n) return; }
        bst_word_stress(z->img, b, p - 1, z->emph, z->mode);
        b[0] = 0;
        if (p + 1 < n) bst_word_stress(z->img, b + p + 1, n - p - 1, z->emph, z->mode);
    } else if (has_open && has_break) {
        int p = 1;
        if (p >= n) return;
        while (!((b[p] == 0x4B || b[p] == 0x4C) && b[p + 1] == 0x51)) {
            if (++p >= n) return;
        }
        bst_word_stress(z->img, b, p - 1, z->emph, z->mode);
        if (p + 1 < n) bst_word_stress(z->img, b + p + 1, n - p - 1, z->emph, z->mode);
        b[p + 1] = 0;
    } else {
        bst_word_stress(z->img, b, n, z->emph, z->mode);
    }
}

static int word(bst_assembler *z, uint8_t *buf) {
    if (z->hdr < 0) {
        static const uint8_t l[6] = { 'L', 0, 0, 0, 0, 0 };
        header(z, l);
    }
    int n = buf[0];
    /* A word behind a break the opening brace held open goes into the stream
       past the marker that ends it, and the build never speaks again. */
    if (z->open == 2) z->stuck = 1;
    if (z->wp + n + 0x25 >= STREAM_MAX) { punctuation(z, 0x7C, 1); return 0; }

    stress_word(z, buf + 2, n);
    if (!(z->emph & 2)) {
        if (z->emph & 1) {
            put(z, 0x51);
            if (z->emph & 4) put(z, 0x4F);
        }
    } else if (z->emph & 4) {
        put(z, 0x52);
    }
    for (int i = 0; i < n; i++) {
        int c = buf[2 + i];
        if (!(z->emph & 2) || (c != 0x4F && c != 0x50 && c != 0x51)) put(z, c);
    }
    z->emph = 0;
    z->hdr = -1;
    return 1;
}

void bst_assemble_start(bst_assembler *z) {
    z->last = -1;
    z->wp = -1;
    z->len = -1;
    z->hdr = -1;
    z->done = 0;
    z->full = 0;
    z->open = 0;
    z->stuck = 0;
    header(z, z->carry);
    static const uint8_t iv[6] = { 'I', 2, 0, 0, 0, 0 };
    command(z, iv);
    z->emph = 0;
    z->s[9] = z->img->t.hdr_shape ? z->img->t.hdr_shape : 0x4C;
}

int bst_assemble_token(bst_assembler *z, int kind, uint8_t *buf) {
    switch (kind) {
    case 1: {
        /* A mode command. Anything that is not a tilde record goes into the
           stream as a command for the later stages to read. Tilde twenty-eight
           is the one the compound splitter writes between the parts of a
           word: the part after it opens the way an emphasised word does, so
           it keeps the lesser of the two accent marks. */
        if (buf[0] != '~') command(z, buf);
        else if (((buf[3] << 8) | buf[2]) == 0x1C) z->emph |= 1;
        break;
    }
    case 3:
        word(z, buf);
        break;
    case 4:
        punctuation(z, buf[0], 0);
        break;
    case 5:
        close_phrase(z, buf);
        break;
    case 6:
        /* The end of the text. Anything not already closed is dropped. */
        return 0;
    default:
        break;
    }
    return !z->stuck && z->wp >= 0 && z->s[z->wp] == 0x5C && !z->open;
}

void bst_assemble_init(bst_assembler *z, const bst_image *img, uint8_t *stream) {
    memset(z, 0, sizeof *z);
    z->img = img;
    z->s = stream;
    z->wp = -1;
    z->hdr = -1;
    z->carry[0] = 0x4E;
    (void)a2;
}
