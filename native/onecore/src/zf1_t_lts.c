/* zf1_t_lts.c - raw CART LTS over a word list, same output format as harness/zftap1_pron.exe ... lts
 * usage: zf1_t_lts words.txt out.txt */
#include "zf1_lts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    zf1_dat d; zf1_cart c; size_t n; const uint8_t *r;
    char line[4096]; FILE *f, *o;
    if (argc < 3) return 1;
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0xac4aefcf, 0xd18325ec, &n);
    if (zf1_cart_init(&c, r, n)) { printf("cart init failed\n"); return 1; }
    f = fopen(argv[1], "rb"); o = fopen(argv[2], "wb");
    while (fgets(line, sizeof line, f)) {
        int len = (int)strlen(line), wl; zf_char *w; zf_char out[0x1000]; char *u, *uw;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;
        w = zf_from_utf8(line, len, &wl);
        zf1_cart_predict(&c, w, out, 0xc01);
        u = zf_to_utf8(out, -1); uw = zf_to_utf8(w, -1);
        fprintf(o, "%s\t0\t%s\n", uw, u);
        free(u); free(uw); free(w);
    }
    fclose(o);
    return 0;
}
