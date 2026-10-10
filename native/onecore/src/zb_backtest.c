/* zb_backtest - render the sentences of a golden dump through the C backend, compare every stage with the engine.
 * Usage: zb_backtest <voicepath> <dump> <out.wav> [-q]
 *   voicepath: e.g. C:/Windows/Speech_OneCore/Engines/TTS/en-US/M1033David (no extension)
 *   dump: zbtap log (binary, full stage comparison) or ziraprobe text output (matrix only, *.txt)
 * Prints per-utterance stage mismatch counts; exit code 0 iff the PCM of every utterance is bit-exact. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zb.h"

typedef struct { short *pcm; long n, cap; } Buf;
static int sink(void *user, const short *pcm, int n)
{
    Buf *b = (Buf *)user;
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->pcm = (short *)realloc(b->pcm, sizeof(short) * (size_t)b->cap);
    }
    memcpy(b->pcm + b->n, pcm, sizeof(short) * (size_t)n);
    b->n += n;
    return 0;
}

static int cmpi(const char *what, const int *a, const int *b, long n, int q)
{
    long i, bad = 0, first = -1;
    if (!a || !b) return 0;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) { if (first < 0) first = i; bad++; }
    if (bad && !q) printf("   %s: %ld/%ld differ, first @%ld got %d ref %d\n", what, bad, n, first, a[first], b[first]);
    return bad != 0;
}
static int cmpf(const char *what, const float *a, const float *b, long n, int q)
{
    long i, bad = 0, first = -1;
    if (!a || !b) return 0;
    for (i = 0; i < n; i++)
        if (memcmp(&a[i], &b[i], 4)) { if (first < 0) first = i; bad++; }
    if (bad && !q) printf("   %s: %ld/%ld differ, first @%ld got %.9g ref %.9g\n", what, bad, n, first, a[first], b[first]);
    return bad != 0;
}

int main(int argc, char **argv)
{
    char err[256];
    ZbVoice *v;
    ZbGolden g;
    Buf all = {0, 0, 0};
    int i, q = argc > 4 && !strcmp(argv[4], "-q"), nok = 0, rc;
    long totbad = 0;
    const char *ext;
    if (argc < 4) { fprintf(stderr, "usage: zb_backtest <voicepath> <dump> <out.wav> [-q]\n"); return 2; }
    v = zb_voice_load(argv[1], err, sizeof err);
    if (!v) { fprintf(stderr, "%s\n", err); return 2; }
    ext = strrchr(argv[2], '.');
    rc = (ext && !strcmp(ext, ".txt")) ? zb_load_ziraprobe(argv[2], v, &g) : zb_load_zbtap(argv[2], v, &g);
    if (rc) { fprintf(stderr, "cannot load %s\n", argv[2]); return 2; }
    for (i = 0; i < g.nutt; i++) {
        ZbTrace t;
        ZbTrace *r = g.ref ? &g.ref[i] : NULL;
        Buf b = {0, 0, 0};
        int bad = 0;
        memset(&t, 0, sizeof t);
        if (zb_synth(v, &g.utt[i], &g.sapi, sink, &b, &t)) { printf("utt %d: synth failed\n", i); bad = 1; }
        else if (r) {
            if (r->dur) bad |= cmpi("DUR", t.dur, r->dur, (long)t.nphone * t.nstate, q);
            if (getenv("ZB_SHOWDUR")) {
                int p, s;
                for (p = 0; p < t.nphone; p++) {
                    printf("    ph%2d id%2d:", p, g.utt[i].ph[p].f[ZB_F_PhoneIdentity]);
                    for (s = 0; s < t.nstate; s++) printf(" %3d", t.dur[p * t.nstate + s]);
                    printf("  | ref");
                    for (s = 0; s < t.nstate; s++) printf(" %3d", r->dur[p * t.nstate + s]);
                    printf("\n");
                }
            }
            if (!bad && r->vuv) bad |= cmpi("VUV", t.vuv, r->vuv, r->nframe, q) | (t.nframe != r->nframe);
            if (!bad) {
                if (!t.ilf0) {
                    bad |= cmpf("LF0", t.lf0, r->lf0, t.nframe, q);
                    bad |= cmpf("LSF", t.lsf, r->lsf, (long)t.nframe * t.order, q);
                    bad |= cmpf("GAIN", t.gain, r->gain, t.nframe, q);
                    bad |= cmpf("F0", t.f0, r->f0, t.nframe, q);
                    bad |= cmpf("LSFfin", t.lsf_fin, r->lsf_fin, (long)t.nframe * t.order, q);
                } else {
                    bad |= cmpi("LF0", t.ilf0, r->ilf0, t.nframe, q);
                    bad |= cmpi("F0", t.if0, r->if0, t.nframe, q);
                    bad |= cmpi("LSF", t.ilsf, r->ilsf, (long)t.nframe * t.order, q);
                    bad |= cmpi("GAIN", t.igain, r->igain, t.nframe, q);
                }
            }
        }
        if (g.pcm && g.pcm[i]) {
            long n = b.n < g.npcm[i] ? b.n : g.npcm[i], k, nb = 0, first = -1;
            for (k = 0; k < n; k++)
                if (b.pcm[k] != g.pcm[i][k]) { if (first < 0) first = k; nb++; }
            if (b.n != g.npcm[i]) nb += labs(b.n - g.npcm[i]);
            if (nb) {
                bad = 1;
                if (!q) printf("   PCM: %ld/%ld samples differ (len %ld ref %ld) first @%ld\n", nb, g.npcm[i], b.n, g.npcm[i], first);
            }
            totbad += nb;
        }
        if (!bad) nok++;
        if (!q || bad) printf("utt %d: %d phones %d frames %s\n", i, g.utt[i].nphone, t.nframe, bad ? "MISMATCH" : "ok");
        sink(&all, b.pcm, (int)b.n);
        free(b.pcm);
        zb_trace_free(&t);
    }
    zb_write_wav(argv[3], all.pcm, all.n);
    printf("%d/%d utterances bit-exact, %ld samples total, %ld differing\n", nok, g.nutt, all.n, totbad);
    free(all.pcm);
    zb_golden_free(&g);
    zb_voice_free(v);
    return nok == g.nutt ? 0 : 1;
}
