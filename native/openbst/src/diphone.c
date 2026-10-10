#include "bst_text.h"

/* Diphone expansion: an allophone index to the acoustic targets behind it.
 *
 * The inventory is indexed by the previous sound times forty-eight plus the
 * current one, so every sound is stored in the context of the one before it,
 * and that is where the engine's coarticulation comes from. An entry holds one
 * sub-sequence per position the sound covers; records are variable length,
 * chosen by bits four to six of the first byte, and bit seven ends a
 * sub-sequence. The three-byte forms carry one nine-bit target index and the
 * five-byte form carries two, naming both ends of a glide. */

static const int RECLEN[8] = { 1, 3, 1, 3, 3, 5, 1, 1 };

int bst_diphone_count(const bst_image *img) {
    return (int)((img->t.diph_offsets_end - img->t.diph_offsets) / 2);
}

int bst_diphone(const bst_image *img, int index, int positions,
                uint16_t *out, int max) {
    int n = bst_diphone_count(img);
    if (index < 0 || index >= n) return 0;
    unsigned entry = (unsigned)bst_u16(img, img->t.diph_offsets, index);
    if (!entry) return 0;

    uint32_t p = img->t.diph_records + entry;
    int k = 0;
    for (int g = 0; g < positions; g++) {
        /* A sub-sequence is at most a couple of dozen records; the bound is
           only there so a corrupt entry cannot run off the section. */
        for (int guard = 0; guard < 32; guard++) {
            const uint8_t *r = bst_at(img, p, 1);
            if (!r) return k;
            int b = r[0];
            int type = (b & 0x70) >> 4;
            int len = RECLEN[type];
            if (!bst_at(img, p, (size_t)len)) return k;
            if (type == 1 || type == 3 || type == 4 || type == 5) {
                if (k < max) out[k] = (uint16_t)(((r[2] << 8) | r[1]) & 0x1FF);
                k++;
            }
            if (type == 5) {
                if (k < max) out[k] = (uint16_t)(((r[4] << 8) | r[3]) & 0x1FF);
                k++;
            }
            p += (uint32_t)len;
            if (b & 0x80) break;
        }
    }
    return k > max ? max : k;
}

/* The ten reflection coefficients a target names, in the Q8 form the lattice
   wants. */
void bst_target_coeffs(const bst_image *img, int voice, int target, int16_t k[10]) {
    uint32_t w = img->t.voice_stride ? img->t.voice_stride : 1;
    uint32_t base = img->t.voices +
                    (uint32_t)(voice * (int)img->t.voice_span + target) * 10 * w;
    for (int i = 0; i < 10; i++)
        k[i] = (int16_t)((int8_t)bst_u8(img, base, (unsigned)i * w) * 2);
}
