/* zf1_t_e2e.c - end-to-end text frontend test: corpus text -> zf1 -> zf2 -> feature matrix / phone controls /
 * events in the format of zf2_t_dump (for tests/fe2/compare.py-style comparison against harness/zftap2 dumps).
 *
 * usage: zf1_t_e2e <voicepath> <features.txt> <corpus.txt>
 * Note: the engine renders text after a SAPI <lang langid="409"> tag with David even when another voice is
 * selected; this driver always uses the given voice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zf2.h"

static char *slurp(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    n = (long)fread(b, 1, (size_t)n, f);
    b[n] = 0;
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    zf2_voice *v;
    zf1_engine *e;
    char *feats, *corpus, *p, *paths[64];
    int npaths = 0, lineno = 0;
    if (argc < 4) { fprintf(stderr, "usage: zf1_t_e2e voicepath features.txt corpus.txt\n"); return 1; }
    v = zf2_voice_load(argv[1]);
    feats = slurp(argv[2]);
    corpus = slurp(argv[3]);
    e = zf1_open(NULL);
    if (!v || !feats || !corpus || !e) { fprintf(stderr, "init failed\n"); return 1; }
    for (p = strtok(feats, "\r\n"); p && npaths < 64; p = strtok(NULL, "\r\n")) if (*p) paths[npaths++] = p;
    p = corpus;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        char *eol = p, save;
        int len;
        while (*eol && *eol != '\n') eol++;
        len = (int)(eol - p);
        if (len && p[len - 1] == '\r') len--;
        lineno++;
        if (len > 0 && !(len >= 2 && p[0] == '#' && p[1] == '#')) {
            zf_sentence s;
            int k, un = 0;
            save = p[len];
            p[len] = 0;
            printf("LINE %d ", lineno);
            for (k = 0; k < len; k++) if (p[k] == ' ') printf("\\s"); else putchar(p[k]);
            printf("\n");
            /* harness/zftap2 speaks lines containing '<' with SPF_IS_XML, others with SPF_IS_NOT_XML */
            zf1_speak_utf8(e, p, strchr(p, '<') ? 1 : 0);
            p[len] = save;
            while (zf1_next_sentence(e, &s) == 1) {
                zf2_sent *zs = zf2_run(v, &s);
                printf("U %d\n", un++);
                if (zs && getenv("ZF_STAGES")) zf2_dump(zs, stdout, "MYUNIT");
                if (zs) {
                    zf2_value *vals = (zf2_value *)malloc(sizeof(zf2_value) * (size_t)(zf2_nphones(zs) + 1));
                    int f;
                    printf("MYFX %d\n", npaths);
                    for (f = 0; f < npaths; f++) {
                        int r = zf2_eval_path(zs, paths[f], vals);
                        printf("F %d cnt=%d:", f, zf2_nphones(zs));
                        if (r) printf(" UNSUPPORTED");
                        else
                            for (k = 0; k < zf2_nphones(zs); k++) {
                                if (vals[k].kind < 0) printf(" N");
                                else printf(" %d:%d", vals[k].kind, vals[k].value);
                            }
                        printf("\n");
                    }
                    free(vals);
                    zf2_sent_free(zs);
                } else {
                    printf("MYFX FAIL\n");
                }
                zf_sentence_free(&s);
            }
        }
        p = *eol ? eol + 1 : eol;
    }
    zf1_close(e);
    zf2_voice_free(v);
    return 0;
}
