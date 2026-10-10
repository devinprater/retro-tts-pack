/* zf1_t_oov.c - OOV splitter unit test (compare columns 1-3 with harness/zftap1_pron.exe ... oov) */
#include "zf1_pron.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_phoneset ps; zf1_pron p; size_t n; const uint8_t *r;
    char line[4096]; FILE *f, *o;
    if (argc < 3 || zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n); zf1_lex_init(&lx, r, n);
    r = zf1_dat_get(&d, 0x29a5584b, 0x153f1b64, &n); zf1_phoneset_init(&ps, r, n);
    zf1_pron_init(&p, &d, &lx, &ps);
    f = fopen(argv[1], "rb"); o = fopen(argv[2], "wb");
    while (fgets(line, sizeof line, f)) {
        int len = (int)strlen(line), wl, k, rr; zf_char *w; char *u; zf_word word; uint16_t buf[0x200];
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;
        w = zf_from_utf8(line, len, &wl);
        zf_word_init(&word); word.text = w;
        memset(buf, 0, sizeof buf);
        rr = zf1_pron_oov_split(&p, &word, buf, 0x180);
        u = zf_to_utf8(w, -1);
        fprintf(o, "%s\t%x\t", u, rr);
        for (k = 0; buf[k]; k++) fprintf(o, "%s%x", k ? "." : "", buf[k]);
        memset(buf, 0, sizeof buf);
        rr = zf1_pron_compound(&p, w, buf, 0x180);
        fprintf(o, "\t%x\t", rr);
        for (k = 0; buf[k]; k++) fprintf(o, "%s%x", k ? "." : "", buf[k]);
        fprintf(o, "\n");
        free(u); zf_word_free(&word);
    }
    fclose(o);
    return 0;
}
