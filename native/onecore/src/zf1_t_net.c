/* zf1_t_net.c - TransducerNetwork runtime test driver (fork B).
 * zf1_t_net <queries.txt> <out.txt> [trace]
 *   N<TAB>category<TAB>text  -> "R hr=00000000 out=..."  (CTextNormalizer::Normalize emulation)
 *   P<TAB>category<TAB>text  -> leaves of the best parse
 */
#include "zf1_fst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static void put(const zf_char *s, int n)
{
    char *u;
    zf_char tmp[4096];
    int i, k = 0;
    if (n < 0) n = (int)zf_strlen(s);
    for (i = 0; i < n && k < 4000; i++) {
        zf_char c = s[i];
        if (c < 0x20 || c == '\\' || c == '"' || c == 0x7f) { char b[8]; int j; sprintf(b, "\\x%02x", c); for (j = 0; b[j]; j++) tmp[k++] = b[j]; }
        else tmp[k++] = c;
    }
    u = zf_to_utf8(tmp, k);
    fputs(u, out);
    free(u);
}

static zf_char *normalize(zf1_net *net, const zf_char *text, int len, const zf_char *cat)
{
    zf1_netres r;
    zf_char *res = NULL;
    static const zf_char def[] = {'s','p',':','d','e','f','a','u','l','t',0};
    zf1_net_parse(net, text, len, 0, 2, cat, 0, &r);
    if (!r.found || r.len < len) {
        zf1_netres_free(&r);
        zf1_net_parse(net, text, len, 0, 2, def, 0, &r);
    }
    if (r.found && r.len == len) res = zf1_netres_text(&r);
    zf1_netres_free(&r);
    return res;
}

int main(int argc, char **argv)
{
    zf1_dat d;
    size_t n;
    const uint8_t *b;
    zf1_net *net;
    FILE *f;
    static char line[65536];
    if (argc < 3) return 1;
    if (argc > 3) zf1_net_trace = atoi(argv[3]);
    if (zf1_dat_load(&d, NULL)) { printf("dat\n"); return 1; }
    b = zf1_dat_get(&d, 0x7d5841ab, 0xea8d0701, &n);
    net = zf1_net_load(b, n);
    if (!net) { printf("net load failed\n"); return 1; }
    f = fopen(argv[1], "rb");
    out = fopen(argv[2], "wb");
    while (fgets(line, sizeof line, f)) {
        int L = (int)strlen(line), wl;
        zf_char *w, *t, *cat;
        while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
        if (L < 3 || line[1] != '\t' || (line[0] != 'N' && line[0] != 'P')) continue;
        w = zf_from_utf8(line, L, &wl);
        fputs("Q ", out); put(w, wl); fputc('\n', out);
        cat = w + 2;
        t = cat;
        while (*t && *t != '\t') t++;
        if (!*t) { free(w); continue; }
        *t++ = 0;
        if (line[0] == 'N') {
            zf_char *r = normalize(net, t, (int)zf_strlen(t), cat);
            fputs("R hr=00000000 out=", out);
            if (r) {
                zf_char *p = r, *s = r;
                int first = 1;
                for (;; p++) {
                    if (*p == ' ' || !*p) {
                        if (p > s) { fputs(first ? "\"" : " \"", out); put(s, (int)(p - s)); fputc('"', out); first = 0; }
                        if (!*p) break;
                        s = p + 1;
                    }
                }
                free(r);
            }
            fputc('\n', out);
        } else {
            zf1_netres r;
            int i;
            zf1_net_parse(net, t, (int)zf_strlen(t), 0, 2, cat, 0, &r);
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
    fclose(out);
    return 0;
}
