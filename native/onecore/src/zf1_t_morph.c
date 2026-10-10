/* zf1_t_morph.c - CEnMorph unit test: same output format as harness/zftap1_pron.exe ... morph
 * usage: zf1_t_morph words.txt out.txt */
#include "zf1_morph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_phoneset ps; size_t n; const uint8_t *r;
    struct zf1_morph *m; char line[4096]; FILE *f, *o;
    zf1_lexhit *h = (zf1_lexhit *)malloc(sizeof *h);
    if (argc < 3 || zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n); zf1_lex_init(&lx, r, n);
    r = zf1_dat_get(&d, 0x29a5584b, 0x153f1b64, &n); zf1_phoneset_init(&ps, r, n);
    m = zf1_morph_create(&d, &lx, &ps);
    if (!m) { printf("morph init failed\n"); return 1; }
    f = fopen(argv[1], "rb"); o = fopen(argv[2], "wb");
    while (fgets(line, sizeof line, f)) {
        int len = (int)strlen(line), wl, i, j, k; zf_char *w; char *u;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;
        w = zf_from_utf8(line, len, &wl);
        u = zf_to_utf8(w, -1);
        if (!zf1_morph_lookup(m, w, wl, h)) fprintf(o, "%s\t80048017\n", u);
        else {
            fprintf(o, "%s\t0", u);
            for (i = 0; i < h->n; i++) {
                fprintf(o, " {%x ", h->p[i].source);
                for (k = 0; h->p[i].ph[k]; k++) fprintf(o, "%s%x", k ? "." : "", h->p[i].ph[k]);
                for (j = 0; j < h->p[i].nset; j++) {
                    fprintf(o, " [");
                    for (k = 0; h->p[i].set[j][k]; k++) {
                        uint16_t id = h->p[i].set[j][k], par = zf_rd16(lx.attrdefs + 10 * (size_t)id + 4);
                        fprintf(o, "%s%x", k ? "," : "", (unsigned)par | ((unsigned)id << 16));
                    }
                    fprintf(o, "]");
                }
                fprintf(o, "}");
            }
            fprintf(o, "\n");
        }
        free(w); free(u);
    }
    fclose(o);
    return 0;
}
