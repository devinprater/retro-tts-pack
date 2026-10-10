/* zb_vocode_test: drive the OneCore vocoder from parameter tracks in a file.
 *
 * Experiment only (not part of the voice): it answers "what does Anna sound like if her audio is
 * resynthesized by David's vocoder?".  Reads F0 / log gain / 24 LSF per 5 ms frame, runs the same
 * excitation + LPC synthesis the engine runs, writes a 16 kHz WAV.
 *
 *   zb_vocode_test <voicedir> params.bin out.wav [--no-sharpen] [--no-repair]
 *
 * params.bin: "ZVOC", int32 T, int32 p, then T * (float f0_hz, float log_gain, float lsf[p]).
 * f0 <= 3 means unvoiced.  LSF are normalized frequencies (0 .. 0.5).
 *
 * The vocoder internals are static, so this file includes the engine source directly and is linked
 * with zb_apm.c / zb_io.c only (never with zb_vocoder.o).
 */
#include "zb_vocoder.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(FILE *f, unsigned v, int n)
{
    int i;
    for (i = 0; i < n; i++) fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static void wav(const char *path, const short *pcm, int n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fwrite("RIFF", 1, 4, f);
    put(f, 36 + n * 2, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put(f, 16, 4); put(f, 1, 2); put(f, 1, 2);
    put(f, (unsigned)rate, 4); put(f, (unsigned)rate * 2, 4);
    put(f, 2, 2); put(f, 16, 2);
    fwrite("data", 1, 4, f);
    put(f, (unsigned)n * 2, 4);
    fwrite(pcm, 2, (size_t)n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir, *par, *out;
    char err[256], magic[4];
    int T, p, i, n, sharpen = 1, repair = 1;
    float *f0, *gain, *lsf, *exc;
    short *pcm;
    float hist[64];
    ZbExcState st;
    ZbVoice *v;
    FILE *f;

    if (argc < 4) {
        fprintf(stderr, "usage: zb_vocode_test <voicedir> params.bin out.wav [--no-sharpen] [--no-repair]\n");
        return 1;
    }
    dir = argv[1]; par = argv[2]; out = argv[3];
    for (i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--no-sharpen")) sharpen = 0;
        else if (!strcmp(argv[i], "--no-repair")) repair = 0;
    }
    v = zb_voice_load(dir, err, sizeof err);
    if (!v) { fprintf(stderr, "zb_vocode_test: %s\n", err); return 1; }

    f = fopen(par, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", par); return 1; }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ZVOC", 4)) { fprintf(stderr, "bad params file\n"); return 1; }
    if (fread(&T, 4, 1, f) != 1 || fread(&p, 4, 1, f) != 1) return 1;
    if (p != 24) { fprintf(stderr, "expected 24 LSF, got %d\n", p); return 1; }
    f0 = (float *)malloc(sizeof(float) * (size_t)T);
    gain = (float *)malloc(sizeof(float) * (size_t)T);
    lsf = (float *)malloc(sizeof(float) * (size_t)T * p);
    for (i = 0; i < T; i++) {
        if (fread(f0 + i, 4, 1, f) != 1 || fread(gain + i, 4, 1, f) != 1 ||
            fread(lsf + (size_t)i * p, 4, (size_t)p, f) != (size_t)p) {
            fprintf(stderr, "short params file at frame %d\n", i);
            return 1;
        }
    }
    fclose(f);

    n = T * v->shift;
    exc = (float *)malloc(sizeof(float) * (size_t)n);
    pcm = (short *)calloc((size_t)n, sizeof(short));
    v->rnd = 0x406;                                  /* the engine's per-utterance seed */
    if (sharpen) for (i = 0; i < T; i++) lsf_sharpen(&v->cfg, lsf + (size_t)i * p, p);
    if (repair) zb_lsf_repair(lsf, T, p);
    memset(&st, 0, sizeof st);
    memset(hist, 0, sizeof hist);
    excitation(v, &st, f0, T, n, v->shift, exc);     /* whole utterance as one block */
    synth(lsf, p, gain, exc, T, v->shift, hist, pcm);
    wav(out, pcm, n, v->rate);
    printf("%d frames, %d samples (%.2f s) at %d Hz\n", T, n, (double)n / v->rate, v->rate);
    return 0;
}
