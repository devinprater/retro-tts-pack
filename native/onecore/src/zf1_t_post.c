/* zf1_t_post.c - test driver for zf1_post (fork E).
 * Input (from tests/fe1/post/post_cases.py): blocks
 *   W9            then word lines, then "END"   -> runs zf1_post_words, prints "w <type> <i180>" per word
 *   SENT          then word lines, then "END"   -> runs zf1_post_sentence, prints type/regular/lists
 * word line: type \t i180 \t pos \t tpos \t off \t len \t pronlen \t sayasid \t text \t ne \t sayas
 * (strings with \xNN escapes as in the zftap dumps).  Output lines are compared by the python script. */
#include "zf1_post.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static zf_char *unesc(const char *s)
{
    int n;
    zf_char *w = zf_from_utf8(s, -1, &n), *o = w;
    int i = 0, k = 0;
    while (i < n) {
        if (w[i] == '\\' && i + 3 < n && w[i + 1] == 'x') {
            char h[3] = {(char)w[i + 2], (char)w[i + 3], 0};
            o[k++] = (zf_char)strtol(h, NULL, 16);
            i += 4;
        } else {
            o[k++] = w[i++];
        }
    }
    o[k] = 0;
    return o;
}

static void put(const zf_char *s)
{
    char *u;
    if (!s) return;
    u = zf_to_utf8(s, -1);
    printf("%s", u);
    free(u);
}

#define MAXW 2048
static char *fld[16];
static int split(char *l)
{
    int n = 0;
    fld[n++] = l;
    while (*l && n < 16) {
        if (*l == '\t') { *l = 0; fld[n++] = l + 1; }
        l++;
    }
    return n;
}

int main(int argc, char **argv)
{
    zf1_dat d;
    zf1_post p;
    static char line[1 << 16];
    FILE *f;
    zf_word *ws = calloc(MAXW, sizeof(zf_word));
    char said[MAXW][24];
    int nw = 0, mode = 0;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: zf1_t_post cases.txt\n"); return 1; }
    if (zf1_dat_load(&d, NULL)) { fprintf(stderr, "dat\n"); return 1; }
    zf1_post_init(&p, &d);
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!strcmp(line, "W9")) { mode = 1; nw = 0; continue; }
        if (!strcmp(line, "SENT")) { mode = 2; nw = 0; continue; }
        if (!strcmp(line, "END")) {
            int i, j;
            /* intern say-as pointers: same id -> same pointer */
            for (i = 0; i < nw; i++)
                for (j = 0; j < i; j++)
                    if (ws[i].fs.sayas && ws[j].fs.sayas && !strcmp(said[i], said[j])) {
                        free((void *)ws[i].fs.sayas);
                        ws[i].fs.sayas = ws[j].fs.sayas;
                        break;
                    }
            if (mode == 1) {
                zf_wordlist wl;
                wl.w = ws; wl.n = nw; wl.cap = MAXW;
                zf1_post_words(&p, &wl);
                printf("W9\n");
                for (i = 0; i < nw; i++) printf("w %d %d\n", ws[i].type, ws[i].i180);
            } else if (mode == 2) {
                zf_sentence s;
                zf1_postout o;
                memset(&s, 0, sizeof s);
                s.words = ws; s.nwords = nw;
                zf1_post_sentence(&p, &s, &o);
                printf("SENT type=%d\n", s.type);
                for (i = 0; i < nw; i++) { printf("r "); put(ws[i].regular); printf("\n"); }
                for (i = 0; i < o.nsegs; i++) { printf("seg %d %d ", o.segs[i].first, o.segs[i].last); put(o.segs[i].value); printf("\n"); }
                for (i = 0; i < nw; i++) if (o.word_seg[i] >= 0) printf("wseg %d -> %d..%d\n", i, o.segs[o.word_seg[i]].first, o.segs[o.word_seg[i]].last);
                for (i = 0; i < s.nquotes; i++) printf("quote %d %d\n", s.quotes[i].open, s.quotes[i].close);
                for (i = 0; i < s.npar; i++) {
                    printf("par n=%d:", s.par[i].nel);
                    for (j = 0; j < s.par[i].nel; j++) printf(" [%d..%d k=%d]", s.par[i].el[j].a, s.par[i].el[j].b, s.par[i].el[j].type);
                    printf("\n");
                }
                for (i = 0; i < s.npar; i++) free(s.par[i].el);
                free(s.par);
                free(s.quotes);
                zf1_postout_free(&o);
            }
            /* reset (keep one copy of shared say-as pointers) */
            for (i = 0; i < nw; i++) {
                int shared = 0;
                for (j = 0; j < i; j++) if (ws[j].fs.sayas == ws[i].fs.sayas) shared = 1;
                if (!shared) free((void *)ws[i].fs.sayas);
            }
            for (i = 0; i < nw; i++) {
                ws[i].fs.sayas = NULL;
                zf_word_free(&ws[i]);
            }
            nw = 0;
            mode = 0;
            continue;
        }
        if (mode && nw < MAXW && split(line) >= 11) {
            zf_word *w = &ws[nw];
            int pl, k;
            zf_word_init(w);
            w->type = atoi(fld[0]);
            w->i180 = atoi(fld[1]);
            w->pos = (uint16_t)atoi(fld[2]);
            w->tpos = (uint16_t)atoi(fld[3]);
            w->src_off = atoi(fld[4]);
            w->src_len = atoi(fld[5]);
            pl = atoi(fld[6]);
            if (pl > 0) {
                w->pron = malloc(sizeof(zf_char) * (pl + 1));
                for (k = 0; k < pl; k++) w->pron[k] = 'x';
                w->pron[pl] = 0;
            }
            strncpy(said[nw], fld[7], 23);
            said[nw][23] = 0;
            w->text = unesc(fld[8]);
            if (!w->text[0]) { free(w->text); w->text = NULL; }
            w->ne_type = unesc(fld[9]);
            if (!w->ne_type[0]) { free(w->ne_type); w->ne_type = NULL; }
            if (strcmp(fld[7], "0000000000000000") && fld[10][0]) w->fs.sayas = unesc(fld[10]);
            nw++;
        }
    }
    return 0;
}
