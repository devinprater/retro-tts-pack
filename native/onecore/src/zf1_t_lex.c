/* zf1_t_lex.c - lexicon lookup smoke test: zf1_t_lex word... */
#include "zf1_lex.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_phoneset ps; zf1_lexent e; size_t n; const uint8_t *r;
    int i, j, k;
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n);
    if (zf1_lex_init(&lx, r, n)) { printf("lex init failed\n"); return 1; }
    r = zf1_dat_get(&d, 0x29a5584b, 0x153f1b64, &n);
    zf1_phoneset_init(&ps, r, n);
    for (i = 1; i < argc; i++) {
        int len; zf_char *w = zf_from_utf8(argv[i], -1, &len);
        if (!zf1_lex_lookup(&lx, w, len, &e)) { printf("%s NOT FOUND\n", argv[i]); continue; }
        for (j = 0; j < e.n; j++) {
            zf_char *s = zf1_phones_to_str(&ps, e.p[j].ph, e.p[j].nph); char *u = zf_to_utf8(s, -1);
            printf("%s /%s/ val=%x", argv[i], u, e.p[j].val);
            for (k = 0; k < e.p[j].nattr; k++) {
                const uint8_t *a = zf1_lex_attrset(&lx, e.p[j].attrset[k]);
                printf(" [");
                for (; a && zf_rd16(a); a += 2) printf("%s ", zf1_lex_attrname(&lx, zf_rd16(a)));
                printf("]");
            }
            printf("\n"); free(s); free(u);
        }
        free(w);
    }
    return 0;
}
