/* zf1_t_tn.c - TN / NE test driver (fork B).  Same query / output format as harness/zftap1_tn.exe:
 *   zf1_t_tn <queries.txt> <out.txt> [trace]
 *   N<TAB>category<TAB>text   -> R hr=0000000X out="w1" "w2" ...
 *   E<TAB>text / F<TAB>text   -> FindNext loop (F = NE FST pre-pass disabled)
 *   M<TAB>text                -> NE FST matcher matches (like harness/zftap1_tn.exe M)
 *   C<TAB>category<TAB>text   -> FindNext loop with a category
 *   P<TAB>category<TAB>text   -> leaves of the best parse (debug)
 */
#include "zf1_tn.h"
#include "zf1_fstpm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *out;

static void put(const zf_char *s, int n)
{
    char *u;
    zf_char *tmp;
    int i, k = 0;
    if (n < 0) n = (int)zf_strlen(s);
    tmp = (zf_char *)malloc(sizeof(zf_char) * (size_t)(n * 4 + 1));
    for (i = 0; i < n; i++) {
        zf_char c = s[i];
        if (c < 0x20 || c == '\\' || c == '"' || c == 0x7f) {
            char b[8];
            int j;
            sprintf(b, "\\x%02x", c);
            for (j = 0; b[j]; j++) tmp[k++] = (zf_char)b[j];
        } else {
            tmp[k++] = c;
        }
    }
    u = zf_to_utf8(tmp, k);
    fputs(u, out);
    free(u);
    free(tmp);
}

static void do_find(zf1_tn *t, const zf_char *cat, const zf_char *text)
{
    int n = (int)zf_strlen(text), pos = 0;
    while (pos < n) {
        int pre = 0, len = 0, rc;
        zf_char c[64];
        uint16_t attr = 0;
        rc = zf1_ne_findnext(t, text + pos, n - pos, cat, &pre, &len, c, 64, &attr);
        fprintf(out, "R hr=%x", rc);
        if (rc != 0) { fputc('\n', out); break; }
        fprintf(out, " at=%d len=%d type=\"", pos + pre, len);
        put(c, -1);
        fprintf(out, "\" attr=%u \"", attr == 0xFFFF ? 0 : attr);
        put(text + pos + pre, len);
        fprintf(out, "\"\n");
        if (!len) break;
        pos += pre + len;
    }
}

int main(int argc, char **argv)
{
    zf1_dat d;
    zf1_tn t;
    FILE *f;
    static char line[65536];
    if (argc < 3) return 1;
    if (argc > 3) zf1_net_trace = atoi(argv[3]);
    if (zf1_dat_load(&d, NULL)) { printf("dat\n"); return 1; }
    if (zf1_tn_init(&t, &d)) { printf("tn init failed\n"); return 1; }
    f = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    if (!f || !out) return 1;
    while (fgets(line, sizeof line, f)) {
        int L = (int)strlen(line), wl;
        zf_char *w, *t2, *cat;
        char k = line[0];
        while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        if (L < 3 || line[1] != '\t' || !strchr("NEFCPRXM", k)) continue;
        w = zf_from_utf8(line, L, &wl);
        fputs("Q ", out); put(w, wl); fputc('\n', out);
        if (k == 'R') {
            zf1_netres r;
            int i;
            zf1_net_parse(t.net, w + 2, (int)zf_strlen(w + 2), 0, 1, NULL, 3, &r);
            fprintf(out, "R found=%d len=%d cost=%d edges=%d cat=", r.found, r.len, r.cost, r.nedges);
            if (r.category) put(r.category, -1);
            fputc('\n', out);
            for (i = 0; i < r.nleaves; i++) {
                fprintf(out, "  L rule=%d arc=%d %d-%d glue=%d cost=%d \"", r.leaves[i].rule, r.leaves[i].arc,
                        r.leaves[i].start, r.leaves[i].end, r.leaves[i].glue, r.leaves[i].cost);
                if (r.leaves[i].len) put(r.leaves[i].text, r.leaves[i].len);
                fputs("\"\n", out);
            }
            zf1_netres_free(&r);
            free(w);
            continue;
        }
        if (k == 'X') {
            /* NE segmentation + step-7 expansion of every entity: print the TN-produced words */
            zf1_tnseg *segs;
            int ns = zf1_tn_segment(&t, w + 2, (int)zf_strlen(w + 2), NULL, &segs), i, j;
            zf_wordlist wl;
            memset(&wl, 0, sizeof wl);
            for (i = 0; i < ns; i++) {
                zf_word *tok;
                if (!segs[i].entity) continue;
                tok = zf_wl_push(&wl);
                tok->text = zf_strndup(w + 2 + segs[i].off, (size_t)segs[i].len);
                zf_setstr(&tok->ne_type, segs[i].cat);
                tok->src_off = segs[i].off;
                tok->src_len = segs[i].len;
                zf1_tn_domain(tok);
            }
            zf1_tn_expand(&t, &wl, NULL, NULL);
            for (j = 0; j < wl.n; j++) {
                zf_word *x = &wl.w[j];
                fputs("W text=\"", out); put(x->text, -1);
                fprintf(out, "\" i214=%d i248=%d i27c=%d i270=%d i274=%d s188=\"", x->i214, x->i248, x->i27c, x->src_off, x->src_len);
                if (x->ne_type) put(x->ne_type, -1);
                fputs("\" s1b0=\"", out); put(x->s1b0, -1); fputs("\"\n", out);
            }
            zf_wl_free(&wl);
            free(segs);
            free(w);
            continue;
        }
        if (k == 'M') {
            int n = (int)zf_strlen(w + 2), i, from = 0, ok = 1, km;
            char *b = (char *)malloc((size_t)n + 1);
            zf1_pm_match m[32];
            for (i = 0; i < n; i++) { if (w[2 + i] >= 0x80) ok = 0; b[i] = (char)w[2 + i]; }
            fprintf(out, "R apply hr=%08x\n", ok ? 0 : 0x80048019u);
            while (ok && t.fst && (km = zf1_pm_next((zf1_pm *)t.fst, b, n, from, m, 32)) > 0) {
                for (i = 0; i < km; i++) fprintf(out, "M off=%d len=%d out=\"<%s>\"\n", m[i].off, m[i].len, m[i].tag);
                from = m[0].off + m[0].len;
            }
            free(b);
            free(w);
            continue;
        }
        if (k == 'E' || k == 'F') {
            clock_t c0 = clock();
            void *fst = t.fst;
            if (k == 'F') t.fst = NULL;
            do_find(&t, NULL, w + 2);
            t.fst = fst;
            if (zf1_net_trace & 4) {
                extern long zf1_net_stat_edges, zf1_net_stat_parses;
                fprintf(stderr, "%ld %ld %ld\n", (long)(clock() - c0), zf1_net_stat_edges, zf1_net_stat_parses);
                zf1_net_stat_edges = zf1_net_stat_parses = 0;
            }
            free(w);
            continue;
        }
        cat = w + 2;
        t2 = cat;
        while (*t2 && *t2 != '\t') t2++;
        if (!*t2) { free(w); continue; }
        *t2++ = 0;
        if (k == 'N') {
            zf_char **words;
            int nw, rc, i;
            rc = zf1_tn_normalize(&t, t2, (int)zf_strlen(t2), cat, &words, &nw);
            fprintf(out, "R hr=%08x out=", rc);
            for (i = 0; i < nw; i++) { fputs(i ? " \"" : "\"", out); put(words[i], -1); fputc('"', out); }
            fputc('\n', out);
            zf1_tn_words_free(words, nw);
        } else if (k == 'C') {
            do_find(&t, cat, t2);
        } else {
            zf1_netres r;
            int i;
            zf1_net_parse(t.net, t2, (int)zf_strlen(t2), 0, 2, cat, 0, &r);
            fprintf(out, "R found=%d len=%d cost=%d edges=%d cat=", r.found, r.len, r.cost, r.nedges);
            if (r.category) put(r.category, -1);
            fputc('\n', out);
            for (i = 0; i < r.nleaves; i++) {
                fprintf(out, "  L rule=%d arc=%d %d-%d glue=%d cost=%d \"", r.leaves[i].rule, r.leaves[i].arc,
                        r.leaves[i].start, r.leaves[i].end, r.leaves[i].glue, r.leaves[i].cost);
                if (r.leaves[i].len) put(r.leaves[i].text, r.leaves[i].len);
                fputs("\"\n", out);
            }
            zf1_netres_free(&r);
        }
        free(w);
    }
    { extern long long zf1_st_act, zf1_st_comp, zf1_st_gather, zf1_st_term, zf1_st_lex; extern long zf1_net_stat_edges; fprintf(stderr, "act %lld comp %lld gather %lld term %lld lex %lld\n", zf1_st_act, zf1_st_comp, zf1_st_gather, zf1_st_term, zf1_st_lex); }
    fclose(out);
    zf1_tn_free(&t);
    zf1_dat_free(&d);
    return 0;
}
