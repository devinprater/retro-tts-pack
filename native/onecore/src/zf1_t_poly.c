/* zf1_t_poly.c - unit test of CPolyphony + homograph CRF (zf1_poly.c) against engine dumps
 * (tests/fe1/pos/polyconv.py output).  The final engine state of each sentence is loaded, the words changed by
 * CPolyphony/CRF (source 13/22) get source 1 again, zf1_poly_apply runs, and source / current pron /
 * prev+next characters / CRF probability are compared.   zf1_t_poly FILE [-v] */
#include "zf1_pos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static zf_char *hexs(const char *h)
{
    int n, k;
    zf_char *s;
    if (h[0] == '-') return NULL;
    n = (int)strlen(h) / 4;
    s = (zf_char *)malloc(sizeof(zf_char) * (size_t)(n + 1));
    for (k = 0; k < n; k++) { unsigned a, b; sscanf(h + 4 * k, "%2x%2x", &a, &b); s[k] = (zf_char)(a | (b << 8)); }
    s[n] = 0;
    return s;
}

typedef struct { float prob[512]; int has[512]; } trace_t;
static void tr(void *ctx, int wi, float prob, int label) { trace_t *t = (trace_t *)ctx; (void)label; if (wi < 512) { t->prob[wi] = prob; t->has[wi] = 1; } }

int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_pos p; size_t n; const uint8_t *r;
    FILE *f; static char line[65536];
    int verbose = argc > 2, sents = 0, oksents = 0, words = 0, badwords = 0, lineno = 0, shown = 0;
    int crfn = 0, crfbad = 0, regn = 0, regbad = 0, regshown = 0;
    if (argc < 2) return 1;
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n);
    zf1_lex_init(&lx, r, n);
    zf1_pos_init(&p, &d, &lx);
    f = fopen(argv[1], "r");
    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        int cnt, i, ok = 1, skip0 = 0;
        zf_wordlist wl = { 0 };
        int expsrc[512], expcur[512];
        zf_char *expcurs[512], *exppc[512], *expnc[512];
        float expf[512];
        trace_t trc;
        if (line[0] == 'L') { lineno = atoi(line + 2); continue; }
        if (line[0] != 'S') continue;
        cnt = atoi(line + 2);
        for (i = 0; i < cnt; i++) {
            char prons[16384], ci[2048], tx[2048], ne[2048], pc[64], nc[64], rg[2048];
            int type, pos, act, src, so, sl, cur, np;
            float f320;
            zf_word *w;
            if (!fgets(line, sizeof line, f)) break;
            sscanf(line, "%d %d %d %d %d %d %d %d %16383s %2047s %2047s %2047s %f %63s %63s %2047s", &type, &pos, &act, &src,
                   &so, &sl, &cur, &np, prons, ci, tx, ne, &f320, pc, nc, rg);
            {   /* RegularText check on the committed sentence (all words incl. SIL) */
                zf_wordlist one = { 0 };
                zf_word *x = zf_wl_push(&one);
                zf_char *er = hexs(rg);
                x->text = hexs(tx);
                x->pos = (uint16_t)pos;
                zf1_pos_regular(&p, &one, 0, 1);
                regn++;
                if (zf_strcmp(x->regular, er)) {
                    regbad++;
                    if (verbose && regshown++ < 20) { char *a = zf_to_utf8(x->text, -1), *b = zf_to_utf8(x->regular, -1), *e2 = zf_to_utf8(er, -1); printf("REG %s pos %d: %s vs %s\n", a, pos, b ? b : "", e2 ? e2 : ""); free(a); free(b); free(e2); }
                }
                free(er);
                zf_wl_free(&one);
            }
            if (i == 0 && type == 3) { skip0 = 1; continue; }   /* sentence-initial SIL is added after Analyze */
            w = zf_wl_push(&wl);
            w->type = type;   /* punctuation is typed (1) by step 9 (0x1800200dc) before CPronouncer */
            w->pos = (uint16_t)pos;
            w->fs.action = act;
            w->src_off = so; w->src_len = sl;
            w->ci = hexs(ci); w->text = hexs(tx); w->ne_type = hexs(ne);
            if (np > 0) {
                char *q = prons;
                int k = 0;
                w->prons = (zf_char **)calloc((size_t)np, sizeof(zf_char *));
                while (*q && k < np) {
                    zf_char buf[512]; int m = 0;
                    if (*q == 'x') { q++; }
                    else while (*q && *q != ',') { buf[m++] = (zf_char)strtoul(q, &q, 16); if (*q == '.') q++; }
                    buf[m] = 0;
                    w->prons[k++] = zf_strdup(buf) ? zf_strdup(buf) : NULL;
                    if (!w->prons[k - 1]) { w->prons[k - 1] = (zf_char *)calloc(1, sizeof(zf_char)); }
                    if (*q == ',') q++;
                }
                w->nprons = k;
                w->cur_pron = cur;
            }
            expsrc[wl.n - 1] = src;
            expcur[wl.n - 1] = cur;
            expcurs[wl.n - 1] = (cur >= 0 && w->prons) ? zf_strdup(w->prons[cur]) : NULL;
            exppc[wl.n - 1] = hexs(pc);
            expnc[wl.n - 1] = hexs(nc);
            expf[wl.n - 1] = f320;
            w->i278 = (src == 13 || src == 22) ? 1 : src;
        }
        (void)skip0;
        memset(&trc, 0, sizeof trc);
        p.crf_trace = tr;
        p.crf_trace_ctx = &trc;
        zf1_poly_apply(&p, &wl);
        sents++;
        for (i = 0; i < wl.n; i++) {
            zf_word *w = &wl.w[i];
            const zf_char *gc = (w->cur_pron >= 0 && w->prons) ? w->prons[w->cur_pron] : NULL;
            int bad = 0;
            words++;
            if (w->i278 != expsrc[i]) bad |= 1;
            if (zf_strcmp(gc, expcurs[i]) != 0) bad |= 2;
            if (zf_strcmp(w->prev_chars, exppc[i]) != 0 || zf_strcmp(w->next_chars, expnc[i]) != 0) bad |= 4;
            if (trc.has[i]) {
                crfn++;
                if (trc.prob[i] != expf[i]) { bad |= 8; crfbad++; }
            }
            if (bad) {
                badwords++;
                ok = 0;
                if (verbose && shown < 80) {
                    char *u = zf_to_utf8(w->text, -1);
                    printf("L%d w%d %s bad=%d src %d/%d prob %.9g/%.9g\n", lineno, i, u, bad, w->i278, expsrc[i],
                           trc.has[i] ? trc.prob[i] : -1.0, expf[i]);
                    free(u);
                    shown++;
                }
            }
        }
        if (ok) oksents++;
        zf_wl_free(&wl);
    }
    printf("RegularText: %d/%d identical\n", regn - regbad, regn);
    printf("sentences identical: %d/%d, words bad %d/%d, CRF evaluations %d (prob mismatch %d)\n", oksents, sents,
           badwords, words, crfn, crfbad);
    return 0;
}
