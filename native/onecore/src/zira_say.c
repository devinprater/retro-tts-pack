/* zira_say: Microsoft David / Zira / Mark (the Windows OneCore voices) text to speech, from the original data.
 *
 * usage: zira_say [--voice David|Zira|Mark] [--dir DIR] [--rate N] [--volume N] [--xml|--no-xml] "text"|@file out.wav
 *   DIR = the folder with MSTTSLocEnUS.dat and M1033<Voice>.{APM,BEP,INI}
 *         (default C:/Windows/Speech_OneCore/Engines/TTS/en-US)
 *   Text containing '<' is read as SAPI XML unless --no-xml.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zb.h"
#include "zf.h"
#include "zf2.h"

#define DEF_DIR "C:/Windows/Speech_OneCore/Engines/TTS/en-US"

typedef struct {
    short *d;
    long n, cap;
} pcmbuf;

static int sink(void *user, const short *pcm, int n)
{
    pcmbuf *b = user;
    if (b->n + n > b->cap) {
        long c = b->cap ? b->cap : 1 << 16;
        short *p;
        while (c < b->n + n) c *= 2;
        p = realloc(b->d, (size_t)c * sizeof *p);
        if (!p) return 1;
        b->d = p;
        b->cap = c;
    }
    memcpy(b->d + b->n, pcm, (size_t)n * sizeof *pcm);
    b->n += n;
    return 0;
}

static int zb_stricmp_ascii(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return x - y;
    }
    return *a - *b;
}

static char *slurp(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b) {
        n = (long)fread(b, 1, (size_t)n, f);
        b[n] = 0;
    }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    const char *dir = DEF_DIR, *voice = "David", *text = NULL, *out = NULL, *emotion = NULL;
    char vpath[1024], dat[1024], err[256];
    int i, xml = -1;
    ZbSapi sapi;
    ZbVoice *vb;
    zf2_voice *vf;
    zf1_engine *e;
    zf_sentence s;
    pcmbuf pcm = {0, 0, 0};
    memset(&sapi, 0, sizeof sapi);
    sapi.rate = 0;
    sapi.volume = 100;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--emotion") && i + 1 < argc) emotion = argv[++i];
        else if (!strcmp(argv[i], "--voice") && i + 1 < argc) voice = argv[++i];
        else if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) sapi.rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) sapi.volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--xml")) xml = 1;
        else if (!strcmp(argv[i], "--no-xml")) xml = 0;
        else if (!text) text = argv[i];
        else if (!out) out = argv[i];
    }
    if (!text || !out) {
        fprintf(stderr, "usage: zira_say [--voice David|Zira|Mark] [--dir DIR] [--rate N] [--volume N] "
                        "[--emotion happy|sad|angry] [--xml|--no-xml] \"text\"|@file out.wav\n");
        return 1;
    }
    if (text[0] == '@') {
        char *b = slurp(text + 1);
        if (!b) {
            fprintf(stderr, "cannot read %s\n", text + 1);
            return 1;
        }
        text = b;
        if ((unsigned char)b[0] == 0xef && (unsigned char)b[1] == 0xbb && (unsigned char)b[2] == 0xbf) text += 3;
    }
    if (xml < 0) xml = strchr(text, '<') != NULL; /* like SAPI's SPF_DEFAULT / our harnesses */
    snprintf(vpath, sizeof vpath, "%s/M1033%s", dir, voice);
    snprintf(dat, sizeof dat, "%s/MSTTSLocEnUS.dat", dir);
    vb = zb_voice_load(vpath, err, sizeof err);
    if (!vb) {
        fprintf(stderr, "zira_say: %s\n", err);
        return 1;
    }
    if (emotion && zb_emotion(vb, emotion, &sapi.style)) {
        fprintf(stderr, "zira_say: voice %s has no emotion \"%s\"\n", voice, emotion);
        return 1;
    }
    vf = zf2_voice_load(vpath);
    e = zf1_open(dat);
    if (!vf || !e) {
        fprintf(stderr, "zira_say: cannot load %s or %s\n", vpath, dat);
        return 1;
    }
    {
        /* SAPI hands <lang langid="...">...</lang> to the language's default voice (David for en-US) as a separate
           job; everything else goes to the selected voice */
        ZbVoice *db = NULL;
        zf2_voice *df = NULL;
        char *work = malloc(strlen(text) + 1), *p;
        /* SAPI keeps the voice's other attributes (gender) when it switches language: a male voice goes to the first
           male en-US voice (David), Zira (female) stays herself */
        int male = zb_stricmp_ascii(voice, "Zira") != 0;
        if (!work) return 1;
        strcpy(work, text);
        p = work;
        while (*p) {
            char *tag = xml && male ? strstr(p, "<lang") : NULL, *piece = p, *end = NULL;
            int david = 0;
            if (tag == p) { /* a <lang> group: speak its content with David */
                char *gt = strchr(p, '>'), *close = gt ? strstr(gt, "</lang>") : NULL;
                if (gt && close) {
                    *close = 0;
                    piece = gt + 1;
                    end = close + 7;
                    david = 1;
                }
            }
            if (!david) { /* plain part up to the next <lang> */
                if (tag) {
                    *tag = 0;
                    end = tag;
                } else {
                    end = p + strlen(p);
                }
            }
            if (david && !db) {
                char dp[1024];
                snprintf(dp, sizeof dp, "%s/M1033David", dir);
                db = zb_voice_load(dp, err, sizeof err);
                df = zf2_voice_load(dp);
            }
            if (*piece) {
                ZbVoice *ub = david && db ? db : vb;
                zf2_voice *uf = david && df ? df : vf;
                zf1_speak_utf8(e, piece, xml);
                while (zf1_next_sentence(e, &s) == 1) {
                    ZbUtt u;
                    memset(&u, 0, sizeof u);
                    if (zf2_process(uf, &s, &u, NULL) == 0 && u.nphone > 0) {
                        if (zb_synth(ub, &u, &sapi, sink, &pcm, NULL)) fprintf(stderr, "zira_say: synthesis error\n");
                    }
                    zf2_utt_free(&u);
                    zf_sentence_free(&s);
                }
            }
            if (end == tag && tag) { /* restore the '<' we cut at and continue with the <lang> group */
                *tag = '<';
                p = tag;
            } else {
                p = end;
            }
        }
        if (df) zf2_voice_free(df);
        if (db) zb_voice_free(db);
        free(work);
    }
    if (zb_write_wav(out, pcm.d, pcm.n)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("%ld samples (%.2f s)\n", pcm.n, pcm.n / 16000.0);
    zf1_close(e);
    zf2_voice_free(vf);
    zb_voice_free(vb);
    return 0;
}
