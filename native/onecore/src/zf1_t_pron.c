/* zf1_t_pron.c - CPronouncer unit test driver.
 * usage: zf1_t_pron pron_in.txt out.txt      (pron_in.txt from tests/fe1/pron/mkin.py)
 * Runs zf1_pron passes 1..4 (without fork C's polyphony step) on the words of every sentence and prints
 *   <line> <textHex> <prons> <i278> <i368>
 * prons = entries separated by ',', '*' = current, ids in hex separated by '.'. */
#include "zf1_pron.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static zf_char *unhex(const char *s)
{
    size_t n = strlen(s), i;
    zf_char *r;
    if (!strcmp(s, "-")) return NULL;
    r = (zf_char *)malloc((n / 4 + 1) * sizeof(zf_char));
    for (i = 0; i < n / 4; i++) {
        unsigned v;
        sscanf(s + 4 * i, "%4x", &v);
        r[i] = (zf_char)v;
    }
    r[n / 4] = 0;
    return r;
}

static void print_prons(FILE *o, const zf_word *w)
{
    int i, k;
    for (i = 0; i < w->nprons; i++) {
        if (i) fputc(',', o);
        if (i == w->cur_pron) fputc('*', o);
        for (k = 0; w->prons[i][k]; k++) fprintf(o, "%s%x", k ? "." : "", w->prons[i][k]);
    }
    if (!w->nprons) fputc('-', o);
}

static void flush(zf1_pron *p, zf_wordlist *wl, int *lines, FILE *o)
{
    int i;
    zf1_pron_pass1(p, wl);
    zf1_pron_pass2(p, wl);
    zf1_pron_pass3(p, wl);
    zf1_pron_pass4(p, wl);
    zf1_pron_finish(p, wl);
    for (i = 0; i < wl->n; i++) {
        zf_word *w = &wl->w[i];
        int k;
        fprintf(o, "%d ", lines[i]);
        for (k = 0; w->text && w->text[k]; k++) fprintf(o, "%04x", w->text[k]);
        fputc(' ', o);
        print_prons(o, w);
        fprintf(o, " %d %d\n", w->i278, w->i368);
    }
    zf_wl_free(wl);
}

int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_phoneset ps; zf1_pron p;
    size_t n; const uint8_t *r;
    FILE *f, *o;
    char line[8192];
    zf_wordlist wl = {0};
    int lines[4096];
    if (argc < 3) return 1;
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n); zf1_lex_init(&lx, r, n);
    r = zf1_dat_get(&d, 0x29a5584b, 0x153f1b64, &n); zf1_phoneset_init(&ps, r, n);
    if (zf1_pron_init(&p, &d, &lx, &ps)) { printf("pron init failed\n"); return 1; }
    f = fopen(argv[1], "rb"); o = fopen(argv[2], "wb");
    while (fgets(line, sizeof line, f)) {
        char ty[8], text[4096], pos[16], s1b0[256], s188[256], act[16], fspos[16], pre[4096];
        int i278, no;
        if (line[0] == 'S') { if (wl.n) flush(&p, &wl, lines, o); continue; }
        if (sscanf(line, "W %7s %4095s %15s %255s %255s %15s %15s %d %4095s %d", ty, text, pos, s1b0, s188, act, fspos,
                   &i278, pre, &no) != 10) continue;
        {
            zf_word *w = zf_wl_push(&wl);
            lines[wl.n - 1] = no;
            w->type = atoi(ty);
            w->text = unhex(text);
            w->pos = w->tpos = (uint16_t)atoi(pos);
            free(w->s1b0); w->s1b0 = unhex(s1b0);
            w->ne_type = unhex(s188);
            w->fs.action = atoi(act);
            w->fs.pos = (uint16_t)strtoul(fspos, NULL, 16);
            if (strcmp(pre, "-")) {
                /* preset prons: entries ',' separated, '*' current, '=' = empty list */
                char *q = pre;
                uint16_t ids[1024];
                int cur = -1, idx = 0;
                while (*q && strcmp(q, "=")) {
                    int k = 0, iscur = 0;
                    if (*q == '*') { iscur = 1; q++; }
                    while (*q && *q != ',') {
                        ids[k++] = (uint16_t)strtoul(q, &q, 16);
                        if (*q == '.') q++;
                    }
                    ids[k] = 0;
                    zf1_word_setpron(w, ids, 0x17, 1);
                    if (iscur) cur = idx;
                    idx++;
                    if (*q == ',') q++;
                }
                /* find current by content order: setpron keeps the list sorted, recompute index */
                if (cur >= 0) {
                    /* re-locate: the cur-th entry in input order */
                    char *q2 = pre; int e = 0, k = 0;
                    while (e < cur && *q2) { if (*q2 == ',') e++; q2++; }
                    if (*q2 == '*') q2++;
                    while (*q2 && *q2 != ',') { ids[k++] = (uint16_t)strtoul(q2, &q2, 16); if (*q2 == '.') q2++; }
                    ids[k] = 0;
                    zf1_word_setpron(w, ids, 0x17, 1);
                }
                w->i278 = i278;
            }
        }
        if (wl.n >= 4096) flush(&p, &wl, lines, o);
    }
    if (wl.n) flush(&p, &wl, lines, o);
    fclose(o);
    return 0;
}
