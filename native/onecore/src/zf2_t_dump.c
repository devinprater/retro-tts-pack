/* zf2_t_dump.c - test driver: feed the engine's own tree (harness/zftap2.exe dump, stage "IN" = the zf1 boundary)
 * into zf2 and print the feature matrix in the dump's "F" format, for tests/fe2/compare.py.
 *
 * usage: zf2_t_dump <voicepath> <features.txt> <dump.txt> [stages]
 *   voicepath    e.g. C:/Windows/Speech_OneCore/Engines/TTS/en-US/M1033David
 *   features.txt the voice's APM feature paths, one per line (tests/fe2/feat_<voice>.txt)
 *   stages       also print zf2's internal tree after the rebuild ("UNIT") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zf2_int.h"

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

/* value of key=... in a line (space separated); returns pointer to the value and its length */
static const char *fld(const char *ln, const char *key, int *len)
{
    size_t kl = strlen(key);
    const char *p = ln;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == ln || p[-1] == ' ') && p[kl] == '=') {
            const char *v = p + kl + 1, *e = v;
            while (*e && *e != ' ' && *e != '\n' && *e != '\r') e++;
            *len = (int)(e - v);
            return v;
        }
        p += kl;
    }
    *len = 0;
    return NULL;
}
static int fint(const char *ln, const char *key, int def)
{
    int n;
    const char *v = fld(ln, key, &n);
    return v ? (int)strtol(v, NULL, 10) : def;
}
/* decode \s, \\, \xNN escapes and UTF-8 into UTF-16 */
static zf_char *fstr(const char *ln, const char *key)
{
    int n, i = 0, o = 0;
    const char *v = fld(ln, key, &n);
    zf_char *r;
    if (!v || (n == 6 && !strncmp(v, "(null)", 6))) return NULL;
    r = (zf_char *)calloc((size_t)n + 1, sizeof(zf_char));
    if (n == 7 && !strncmp(v, "(empty)", 7)) return r;
    while (i < n) {
        unsigned char c = (unsigned char)v[i];
        if (c == '\\' && i + 1 < n) {
            if (v[i + 1] == 's') { r[o++] = ' '; i += 2; continue; }
            if (v[i + 1] == '\\') { r[o++] = '\\'; i += 2; continue; }
            if (v[i + 1] == 'x' && i + 3 < n) { char h[3] = {v[i + 2], v[i + 3], 0}; r[o++] = (zf_char)strtol(h, NULL, 16); i += 4; continue; }
        }
        if (c < 0x80) { r[o++] = c; i++; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < n) { r[o++] = (zf_char)(((c & 0x1f) << 6) | (v[i + 1] & 0x3f)); i += 2; }
        else if ((c & 0xf0) == 0xe0 && i + 2 < n) { r[o++] = (zf_char)(((c & 0x0f) << 12) | ((v[i + 1] & 0x3f) << 6) | (v[i + 2] & 0x3f)); i += 3; }
        else { r[o++] = '?'; i++; }
    }
    r[o] = 0;
    return r;
}

static void parse_word(const char *ln, zf_word *w)
{
    int n;
    const char *v;
    memset(w, 0, sizeof *w);
    w->type = fint(ln, "i88", 0);
    {
        const char *l = fld(ln, "lang", &n);
        w->lang = l ? (uint16_t)strtol(l, NULL, 16) : 0x409;
    }
    w->pos = (uint16_t)fint(ln, "pos", -1);
    w->tpos = (uint16_t)fint(ln, "tpos", -1);
    w->i84 = fint(ln, "i84", 0);
    w->text = fstr(ln, "t");
    w->i180 = fint(ln, "i180", 0);
    w->silence_ms = fint(ln, "i210", 0);
    w->i214 = fint(ln, "i214", 0);
    w->i228 = fint(ln, "i228", 0);
    w->i22c = fint(ln, "i22c", 0);
    w->i230 = fint(ln, "i230", 0);
    w->i234 = fint(ln, "i234", 0);
    w->tone = fint(ln, "i238", 0);
    w->tone_lock = fint(ln, "i23c", 0);
    w->i240 = fint(ln, "i240", 0);
    w->bi = fint(ln, "i244", 2);
    w->i248 = fint(ln, "i248", 0);
    w->bi_lock = fint(ln, "i24c", 0);
    w->pause_class = fint(ln, "i250", 0);
    w->emphasis = fint(ln, "i254", 0);
    w->emph_lock = fint(ln, "i258", 0);
    w->src_off = fint(ln, "i270", 0);
    w->src_len = fint(ln, "i274", 0);
    w->i278 = fint(ln, "i278", 0);
    w->i27c = fint(ln, "i27c", 0);
    w->i2b8 = fint(ln, "i2b8", 0);
    w->i2bc = fint(ln, "i2bc", 0);
    w->i2f0 = fint(ln, "i2f0", 0);
    w->s2f8 = fstr(ln, "bm");
    w->i368 = fint(ln, "i368", 0);
    w->i36c = fint(ln, "i36c", 0);
    w->pron = fstr(ln, "pron");
    w->ne_type = fstr(ln, "ne");
    w->cur_pron = -1;
    v = fld(ln, "prid", &n);
    if (v && n && strncmp(v, "none", 4) && v[0] != '-') {
        zf_char *ids = (zf_char *)calloc((size_t)n + 1, sizeof(zf_char));
        int k = 0;
        const char *p = v, *e = v + n;
        while (p < e) {
            ids[k++] = (zf_char)strtol(p, (char **)&p, 10);
            if (p < e && *p == '.') p++;
        }
        ids[k] = 0;
        w->nprons = 1;
        w->prons = (zf_char **)malloc(sizeof(zf_char *));
        w->prons[0] = ids;
        w->cur_pron = 0;
    }
    {
        const char *g = fld(ln, "g2e8", &n);
        w->group = g ? (int)(strtoull(g, NULL, 16) & 0x7fffffff) : 0;
    }
    w->attached_pause = fint(ln, "a2b0", -1);
    w->ne_obj = 0;
    v = fld(ln, "fs", &n);
    if (v) {
        const char *c = memchr(v, ':', (size_t)n);
        unsigned d[24] = {0};
        int k = 0;
        if (c) {
            const char *p = c + 1;
            while (k < 24 && p < v + n) {
                d[k++] = (unsigned)strtoul(p, (char **)&p, 16);
                if (*p == ',') p++; else break;
            }
        }
        w->fs.action = (int)d[0];
        w->fs.lang = (uint16_t)d[1];
        w->fs.emph = (int)d[2];
        w->fs.silence_ms = (int)d[3];
        w->fs.pos = (uint16_t)d[14];
        w->fs.sayas = fstr(ln, "fsa");
        {
            const char *pr = fld(ln, "fpr", &n);
            if (pr) {
                w->fs.has_prosody = 1;
                sscanf(pr, "%f,%f,%f", &w->fs.pitch_middle, &w->fs.rate, &w->fs.volume);
            }
        }
    }
}

static void free_sentence(zf_sentence *s)
{
    int i, k;
    for (i = 0; i < s->nwords; i++) {
        zf_word *w = &s->words[i];
        free(w->text);
        free(w->pron);
        free(w->ne_type);
        free(w->s2f8);
        free((void *)w->fs.sayas);
        for (k = 0; k < w->nprons; k++) free(w->prons[k]);
        free(w->prons);
    }
    free(s->words);
    free(s->quotes);
    for (i = 0; i < s->npar; i++) free(s->par[i].el);
    free(s->par);
    memset(s, 0, sizeof *s);
}

/* voices: the engine can switch voice per fragment (SAPI <lang langid=...> selects the default voice of that
 * language, i.e. David for 409), so each sentence is run with the voice whose feature count matches the engine's
 * matrix for that sentence (looked up in the dump after the sentence's IN stage). */
#define MAXV 4
static zf2_voice *g_v[MAXV];
static char *g_paths[MAXV][64];
static int g_npaths[MAXV], g_nv;

static int pick_voice(const char *after)
{
    const char *q = after;
    while (q && (q = strstr(q, "\nFX ")) != NULL) {
        int n = atoi(q + 4), k;
        if (n > 1) {
            for (k = 0; k < g_nv; k++) if (g_npaths[k] == n) return k;
            return 0;
        }
        q += 4;
    }
    return 0;
}

int main(int argc, char **argv)
{
    zf2_voice *v;
    char *dump, *p, **paths;
    int npaths, stages = 0, in_stage = 0, i;
    zf_sentence sent;
    int have = 0;
    if (argc < 4) {
        fprintf(stderr, "usage: zf2_t_dump voicepath features.txt [voicepath2 features2.txt ...] dump.txt [stages]\n");
        return 1;
    }
    for (i = 1; i + 2 < argc && g_nv < MAXV;) {
        char *feats = slurp(argv[i + 1]);
        g_v[g_nv] = zf2_voice_load(argv[i]);
        if (!g_v[g_nv] || !feats) { fprintf(stderr, "cannot load %s\n", argv[i]); return 1; }
        for (p = strtok(feats, "\r\n"); p && g_npaths[g_nv] < 64; p = strtok(NULL, "\r\n"))
            if (*p) g_paths[g_nv][g_npaths[g_nv]++] = p;
        g_nv++;
        i += 2;
        if (i + 1 >= argc || !strcmp(argv[i + 1], "stages")) break;
    }
    dump = slurp(argv[i]);
    if (i + 1 < argc && !strcmp(argv[i + 1], "stages")) stages = 1;
    if (!dump) { fprintf(stderr, "cannot load dump\n"); return 1; }
    memset(&sent, 0, sizeof sent);
    p = dump;
    for (;;) {
        char *e = strchr(p, '\n'), *ln = p;
        int flush = 0;
        if (e) *e = 0;
        if (!e || !strncmp(ln, "STAGE ", 6) || !strncmp(ln, "LINE ", 5) || !strncmp(ln, "U ", 2)) flush = 1;
        if (flush && have) {
            zf2_sent *s;
            int vi = pick_voice(e ? e + 1 : NULL);
            v = g_v[vi];
            paths = g_paths[vi];
            npaths = g_npaths[vi];
            s = zf2_run(v, &sent);
            if (s) {
                zf2_value *vals = (zf2_value *)malloc(sizeof(zf2_value) * (size_t)(zf2_nphones(s) + 1));
                int f, k;
                if (stages) zf2_dump(s, stdout, "MYUNIT");
                printf("MYFX %d\n", npaths);
                for (f = 0; f < npaths; f++) {
                    int r = zf2_eval_path(s, paths[f], vals);
                    printf("F %d cnt=%d:", f, zf2_nphones(s));
                    if (r) printf(" UNSUPPORTED");
                    else
                        for (k = 0; k < zf2_nphones(s); k++) {
                            if (vals[k].kind < 0) printf(" N");
                            else printf(" %d:%d", vals[k].kind, vals[k].value);
                        }
                    printf("\n");
                }
                free(vals);
                {
                    ZbUtt u;
                    zf2_events ev = {0, 0, NULL};
                    zf2_features(s, &u);
                    for (k = 0; k < u.nphone; k++) {
                        const ZbPhoneCtl *c = &u.ph[k].c;
                        unsigned r, e;
                        memcpy(&r, &c->pitch_range, 4);
                        e = (unsigned)c->emph;
                        printf("CTL %d %x,%x,%x,%x,%x,%x,%x\n", k, (unsigned)c->volume, (unsigned)c->rate, (unsigned)c->pitch, r, e,
                               (unsigned)c->group_ms, (unsigned)c->phone_ms);
                    }
                    zf2_utt_free(&u);
                    zf2_make_events(s, &ev);
                    for (k = 0; k < ev.n; k++) {
                        printf("MYEV %d %lld %lld %d", ev.ev[k].id, ev.ev[k].wparam, ev.ev[k].lparam,
                               ev.ev[k].phone * 8 + ev.ev[k].sub);
                        if (ev.ev[k].str) {
                            const zf_char *q = ev.ev[k].str;
                            putchar(' ');
                            if (!*q) printf("(empty)");
                            for (; *q; q++) if (*q == ' ') printf("\\s"); else putchar(*q < 128 ? (char)*q : '?');
                        }
                        putchar('\n');
                    }
                    zf2_events_free(&ev);
                }
                zf2_sent_free(s);
            } else
                printf("MYFX FAIL\n");
            free_sentence(&sent);
            have = 0;
        }
        if (!strncmp(ln, "LINE ", 5) || !strncmp(ln, "U ", 2)) printf("%s\n", ln);
        if (!strncmp(ln, "STAGE ", 6)) in_stage = !strncmp(ln + 6, "IN", 2) && (ln[8] == 0 || ln[8] == '\r');
        if (in_stage) {
            if (!strncmp(ln, "SENT ", 5)) {
                memset(&sent, 0, sizeof sent);
                sent.type = fint(ln, "type", 0);
                sent.emotion = fint(ln, "emo", 1);
                sent.src_off = fint(ln, "a0", 0);
                sent.src_len = fint(ln, "a4", 0);
                sent.words = (zf_word *)calloc(4096, sizeof(zf_word));
                have = 1;
            } else if (have && !strncmp(ln, "W ", 2)) {
                parse_word(ln, &sent.words[sent.nwords++]);
            } else if (have && !strncmp(ln, "QP ", 3)) {
                int a, b;
                if (sscanf(ln + 3, "%d %d", &a, &b) == 2) {
                    sent.quotes = (struct zf_quote *)realloc(sent.quotes, sizeof(struct zf_quote) * (size_t)(sent.nquotes + 1));
                    sent.quotes[sent.nquotes].open = a;
                    sent.quotes[sent.nquotes].close = b;
                    sent.nquotes++;
                }
            } else if (have && !strncmp(ln, "PS ", 3)) {
                const char *c = strchr(ln, ':');
                struct zf_parstruct *ps;
                sent.par = (struct zf_parstruct *)realloc(sent.par, sizeof(struct zf_parstruct) * (size_t)(sent.npar + 1));
                ps = &sent.par[sent.npar++];
                ps->nel = 0;
                ps->el = (struct zf_parel *)malloc(sizeof(struct zf_parel) * 256);
                while (c && ps->nel < 256) {
                    int a, b, t;
                    c++;
                    if (sscanf(c, " %d,%d,%d", &a, &b, &t) != 3) break;
                    ps->el[ps->nel].a = a;
                    ps->el[ps->nel].b = b;
                    ps->el[ps->nel].type = t;
                    ps->nel++;
                    c = strchr(c + 1, ' ');
                }
            }
        }
        if (!e) break;
        p = e + 1;
    }
    for (i = 0; i < g_nv; i++) zf2_voice_free(g_v[i]);
    return 0;
}
