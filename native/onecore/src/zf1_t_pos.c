/* zf1_t_pos.c - unit test of the POS tagger core against engine traces (tests/fe1/pos/ptconv.py output).
 *   zf1_t_pos run  FILE   : feed the PTIN node chains (engine candidates) to zf1_pos_run, compare with PTOUT
 *   zf1_t_pos cand FILE   : recompute each node's candidates from its text with the lexicon, compare with PTIN
 *   zf1_t_pos full FILE   : candidates from the lexicon + tagging (kind/preset from PTIN), compare with PTOUT */
#include "zf1_pos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_list(const char *s, uint16_t *o)
{
    int n = 0;
    if (s[0] == '-') return 0;
    while (*s) {
        o[n++] = (uint16_t)strtoul(s, (char **)&s, 10);
        if (*s == ',') s++;
    }
    return n;
}

/* oracle of container-lexicon results (LEX8 lines from the harness): hex(utf16) + found flag */
static char **g_or; static int *g_orf; static char **g_ors; static int g_orn; static zf1_pos *g_p; static zf1_lexlookup_fn g_def;
static int oracle_lookup(void *ctx, const zf_char *w, int n, uint16_t *sets, int cap)
{
    char hex[1100]; int i, k = 0;
    (void)ctx;
    for (i = 0; i < n && i < 256; i++) k += sprintf(hex + k, "%02x%02x", w[i] & 255, w[i] >> 8);
    hex[k] = 0;
    for (i = 0; i < g_orn; i++)
        if (!strcmp(g_or[i], hex)) {
            const char *s = g_ors[i]; int m = 0;
            if (!g_orf[i]) return -1;
            while (*s && m < cap) {   /* "4,6;1,3;" */
                if (*s == ';') { sets[m++] = 0; s++; continue; }
                if (*s == ',') { s++; continue; }
                sets[m++] = (uint16_t)strtoul(s, (char **)&s, 10);
            }
            return m;
        }
    return g_def((void *)g_p->lx, w, n, sets, cap);
}

int main(int argc, char **argv)
{
    zf1_dat d; zf1_lex lx; zf1_pos p; size_t n; const uint8_t *r;
    FILE *f; char line[8192];
    int chains = 0, okchains = 0, nodes = 0, badnodes = 0, shown = 0;
    const char *mode = argc > 1 ? argv[1] : "run";
    if (argc < 3) { printf("usage: zf1_t_pos run|cand|full FILE\n"); return 1; }
    if (zf1_dat_load(&d, NULL)) return 1;
    r = zf1_dat_get(&d, 0x7bd71f46, 0xf9a99c02, &n);
    zf1_lex_init(&lx, r, n);
    if (zf1_pos_init(&p, &d, &lx)) { printf("pos init failed\n"); return 1; }
    g_p = &p; g_def = p.lookup;
    if (argc > 3) {
        FILE *o = fopen(argv[3], "r"); char l2[4096];
        while (o && fgets(l2, sizeof l2, o)) {
            char hx[1100], at[2048]; int fl;
            at[0] = 0;
            if (sscanf(l2, "%d %1099s %2047s", &fl, hx, at) < 2) continue;
            g_or = realloc(g_or, sizeof(char *) * (g_orn + 1)); g_orf = realloc(g_orf, sizeof(int) * (g_orn + 1));
            g_ors = realloc(g_ors, sizeof(char *) * (g_orn + 1));
            g_or[g_orn] = _strdup(hx); g_ors[g_orn] = _strdup(at[0] == '-' ? "" : at); g_orf[g_orn++] = fl;
        }
        if (o) fclose(o);
        zf1_pos_set_lookup(&p, oracle_lookup, NULL);
    }
    printf("rules: %d lexical, %d contextual\n", p.nlex, p.nctx);
    f = fopen(argv[2], "r");
    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        int cn, i, ok = 1;
        zf1_posnode nd[256];
        uint16_t expect[256];
        zf_char texts[256][130];
        if (line[0] == 'T' && !strcmp(mode, "tag")) {   /* CPOSTaggerImpl::Tag on a word list */
            zf_wordlist wl = { 0 };
            cn = atoi(line + 2);
            for (i = 0; i < cn && i < 256; i++) {
                unsigned type, pre, outp; char hex[1024]; int k, hl;
                zf_word *w;
                if (!fgets(line, sizeof line, f)) break;
                sscanf(line, "%u %u %u %1023s", &type, &pre, &outp, hex);
                w = zf_wl_push(&wl);
                w->type = (int)type; w->pos = (uint16_t)pre; expect[i] = (uint16_t)outp;
                hl = hex[0] == '-' ? 0 : (int)strlen(hex) / 4;
                w->text = (zf_char *)calloc((size_t)hl + 1, sizeof(zf_char));
                for (k = 0; k < hl; k++) { unsigned a, b; sscanf(hex + 4 * k, "%2x%2x", &a, &b); w->text[k] = (zf_char)(a | (b << 8)); }
            }
            zf1_pos_tag(&p, &wl, 0, wl.n);
            chains++;
            for (i = 0; i < wl.n; i++) if (wl.w[i].pos != expect[i]) ok = 0;
            if (ok) okchains++;
            else if (shown < 40) {
                shown++;
                printf("TAG MISMATCH:");
                for (i = 0; i < wl.n; i++) {
                    char *u = zf_to_utf8(wl.w[i].text, -1);
                    printf(" %s/%d%s%d", u, expect[i], wl.w[i].pos == expect[i] ? "=" : "!", wl.w[i].pos);
                    free(u);
                }
                printf("\n");
            }
            zf_wl_free(&wl);
            continue;
        }
        if (line[0] != 'C' || !strcmp(mode, "tag")) continue;
        cn = atoi(line + 2);
        if (cn > 256) cn = 256;
        memset(nd, 0, sizeof nd);
        for (i = 0; i < cn; i++) {
            char c1[512], c2[512], hex[1024]; unsigned pos, kind, outp; int k, hl;
            if (!fgets(line, sizeof line, f)) break;
            sscanf(line, "%u %u %511s %511s %u %1023s", &pos, &kind, c1, c2, &outp, hex);
            nd[i].pos = (uint16_t)pos; nd[i].kind = (int)kind;
            nd[i].ncand = parse_list(c1, nd[i].cand);
            nd[i].nlexc = parse_list(c2, nd[i].lexc);
            expect[i] = (uint16_t)outp;
            hl = hex[0] == '-' ? 0 : (int)strlen(hex) / 4;
            if (hl > 128) hl = 128;
            for (k = 0; k < hl; k++) {
                unsigned a, b; sscanf(hex + 4 * k, "%2x%2x", &a, &b); texts[i][k] = (zf_char)(a | (b << 8));
            }
            texts[i][hl] = 0;
            nd[i].text = texts[i];
        }
        chains++;
        if (!strcmp(mode, "cand") || !strcmp(mode, "full")) {
            for (i = 0; i < cn; i++) {
                uint16_t lc[32], cc[32]; int c, k, m, nc = 0, nl = 0, same;
                if (nd[i].kind == 3 && nd[i].text[0] == ' ' && !nd[i].text[1]) continue;
                c = zf1_pos_candidates(&p, nd[i].text, (int)zf_strlen(nd[i].text), lc, 32);
                for (k = 0; k < c; k++) {
                    uint16_t mp;
                    if (lc[k] == p.unknown) continue;
                    lc[nl++] = lc[k];
                    mp = zf1_pos_main(&p, lc[k]);
                    for (m = 0; m < nc; m++) if (cc[m] == mp) break;
                    if (m == nc) cc[nc++] = mp;
                }
                same = nl == nd[i].nlexc && !memcmp(lc, nd[i].lexc, 2 * (size_t)nl) && nc == nd[i].ncand &&
                       !memcmp(cc, nd[i].cand, 2 * (size_t)nc);
                if (!same) {
                    char *u = zf_to_utf8(nd[i].text, -1);
                    if (!strcmp(mode, "cand") && shown < 60) {
                        printf("CAND %s engine l", u);
                        for (k = 0; k < nd[i].nlexc; k++) printf("%d,", nd[i].lexc[k]);
                        printf(" port l");
                        for (k = 0; k < nl; k++) printf("%d,", lc[k]);
                        printf("\n");
                        shown++;
                    }
                    free(u);
                    badnodes++;
                }
                nodes++;
                if (!strcmp(mode, "full")) {
                    nd[i].nlexc = nl; memcpy(nd[i].lexc, lc, 2 * (size_t)nl);
                    nd[i].ncand = nc; memcpy(nd[i].cand, cc, 2 * (size_t)nc);
                }
            }
            if (!strcmp(mode, "cand")) continue;
        }
        zf1_pos_run(&p, nd, cn);
        for (i = 0; i < cn; i++) if (nd[i].pos != expect[i]) ok = 0;
        if (ok) okchains++;
        else if (shown < 40) {
            shown++;
            printf("MISMATCH:");
            for (i = 0; i < cn; i++) {
                char *u = zf_to_utf8(nd[i].text, -1);
                printf(" %s/%d%s%d", u, expect[i], nd[i].pos == expect[i] ? "=" : "!", nd[i].pos);
                free(u);
            }
            printf("\n");
        }
    }
    if (!strcmp(mode, "cand")) printf("candidate lists: %d/%d nodes identical\n", nodes - badnodes, nodes);
    else printf("chains identical: %d/%d\n", okchains, chains);
    return 0;
}
