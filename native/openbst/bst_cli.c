/*
 * bst_cli -- the Retro TTS Pack's BeSTspeech front end, over openbst's bst_*.
 *
 * The pack used to reach this engine through the original b32_tts.dll and the
 * twelve dll_*.dll language modules, both emulated.  openbst is the same engine
 * in portable C with its tables compiled in, so this needs no DLL, no voice
 * file and no emulator, and it runs on any CPU the pack supports.
 *
 *   bst_cli --build NAME [--voice N] [--rate N] [--pitch N] [--top N]
 *           [--level N] [--exc N] [--unvoiced N] [--gain-db DB]
 *           [--filename FILE] <text>
 *
 * --gain-db scales the samples by that many decibels, rounding to nearest
 * even and clipping, which is what the language builds need to sit at the
 * same level as the classic one.
 *
 * --filename - writes the WAV to standard output, which is how the pack reads
 * it.  --list names the builds this front end offers.
 *
 * The six 1998 modules are refused here as well as left out of the pack's
 * voice list: openbst's own notes record divergence in them that no test
 * covers, so the pack does not offer them.  See README.md beside this file.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bst.h"

#define IS_OPT(s, n) (!strcmp((s), (n)))

/* What a text may be worth in samples, so one pass can size its buffer
   without asking the engine to say the whole utterance twice.  The slowest
   build measured at the slowest rate gave 2308 samples per byte of text, so
   4096 has better than a third in hand; the floor covers a one-character
   utterance, and a text that somehow overruns the ceiling falls back to
   bst_length rather than losing audio. */
#define SAMPLES_PER_BYTE 4096
#define SAMPLES_FLOOR    32768
#define SAMPLES_CEILING  8388608

/* The 1998 generation, by name.  All six module names begin with the year. */
static int blocked(const char *name)
{
    return name != NULL && strncmp(name, "1998", 4) == 0;
}

static void wav_header(FILE *f, long n, int rate)
{
    unsigned char h[44] = "RIFF\0\0\0\0WAVEfmt \20\0\0\0\1\0\1\0";
    unsigned long bytes = (unsigned long)n * 2;
    unsigned long riff  = bytes + 36;
    unsigned long brate = (unsigned long)rate * 2;
    int i;

    for (i = 0; i < 4; i++) {
        h[4 + i]  = (unsigned char)(riff >> (8 * i));
        h[24 + i] = (unsigned char)((unsigned long)rate >> (8 * i));
        h[28 + i] = (unsigned char)(brate >> (8 * i));
        h[40 + i] = (unsigned char)(bytes >> (8 * i));
    }
    h[32] = 2;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    fwrite(h, 1, 44, f);
}

int main(int argc, char **argv)
{
    const char *build = NULL, *filename = "-", *text = NULL;
    int list = 0;
    int voice = 0, rate = 0, pitch = 0, top = 0, level = 0, exc = 0, unv = 0;
    int has_voice = 0, has_rate = 0, has_pitch = 0, has_top = 0, has_level = 0;
    int has_exc = 0, has_unv = 0, has_gain = 0;
    double gain_db = 0.0;
    bst *h;
    int16_t *pcm;
    long cap, got;
    int sample_rate;
    FILE *out;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;

        if (IS_OPT(a, "--list")) {
            list = 1;
            continue;
        }
        /* An argument is an option only when its name matches one, so text
           that begins with a dash -- an Orca bullet, say -- still reaches the
           engine. */
        if (!IS_OPT(a, "--build") && !IS_OPT(a, "--filename") &&
            !IS_OPT(a, "--voice") && !IS_OPT(a, "--rate") && !IS_OPT(a, "--pitch") &&
            !IS_OPT(a, "--top") && !IS_OPT(a, "--level") && !IS_OPT(a, "--exc") &&
            !IS_OPT(a, "--unvoiced") && !IS_OPT(a, "--gain-db")) {
            if (text == NULL) {
                text = a;
                continue;
            }
            fprintf(stderr, "bst_cli: unexpected argument %s\n", a);
            return 2;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "bst_cli: %s needs a value\n", a);
            return 2;
        }
        v = argv[++i];
        if      (IS_OPT(a, "--build"))    build = v;
        else if (IS_OPT(a, "--filename")) filename = v;
        else if (IS_OPT(a, "--voice"))    { voice = atoi(v); has_voice = 1; }
        else if (IS_OPT(a, "--rate"))     { rate = atoi(v); has_rate = 1; }
        else if (IS_OPT(a, "--pitch"))    { pitch = (int)strtol(v, NULL, 0); has_pitch = 1; }
        else if (IS_OPT(a, "--top"))      { top = (int)strtol(v, NULL, 0); has_top = 1; }
        else if (IS_OPT(a, "--level"))    { level = (int)strtol(v, NULL, 0); has_level = 1; }
        else if (IS_OPT(a, "--exc"))      { exc = (int)strtol(v, NULL, 0); has_exc = 1; }
        else if (IS_OPT(a, "--unvoiced")) { unv = (int)strtol(v, NULL, 0); has_unv = 1; }
        else if (IS_OPT(a, "--gain-db"))  { gain_db = strtod(v, NULL); has_gain = 1; }
    }

    if (list) {
        const char *names[64];
        int n = bst_builds(names, 64), k;

        for (k = 0; k < n && k < 64; k++)
            if (!blocked(names[k]))
                printf("%s\n", names[k]);
        return 0;
    }
    if (build == NULL || text == NULL) {
        fprintf(stderr, "usage: bst_cli --build NAME [--voice N] [--rate N] "
                        "[--pitch N] [--top N] [--level N] [--exc N] "
                        "[--unvoiced N] [--gain-db DB] [--filename FILE] <text>\n");
        return 2;
    }
    if (blocked(build)) {
        fprintf(stderr, "bst_cli: the %s build is not offered by this pack\n", build);
        return 2;
    }

    h = bst_open(build);
    if (h == NULL) {
        fprintf(stderr, "bst_cli: cannot open build %s\n", build);
        return 1;
    }
    if (has_voice) bst_set(h, "voice", voice);
    if (has_rate)  bst_set(h, "rate", rate);
    if (has_pitch) bst_set(h, "pitch", pitch);
    if (has_top)   bst_set(h, "top", top);
    if (has_level) bst_set(h, "level", level);
    if (has_exc)   bst_set(h, "exc", exc);
    if (has_unv)   bst_set(h, "unvoiced", unv);

    /* One synthesis, not two.  bst_length runs the whole engine to count the
       samples and bst_say then runs it again to produce them, so the wait
       before any sound is paid twice.  bst_say reports how much it made, so a
       buffer sized from the text is enough, and only a text that does overrun
       it asks for the exact size and says itself again. */
    cap = (long)strlen(text) * SAMPLES_PER_BYTE + SAMPLES_FLOOR;
    if (cap > SAMPLES_CEILING)
        cap = SAMPLES_CEILING;
    pcm = (int16_t *)malloc((size_t)cap * sizeof *pcm);
    if (pcm == NULL) {
        fprintf(stderr, "bst_cli: out of memory\n");
        bst_close(h);
        return 1;
    }
    got = bst_say(h, text, pcm, cap);
    if (got == cap) {
        long need = bst_length(h, text);
        if (need > 0) {
            int16_t *larger = (int16_t *)realloc(pcm, (size_t)need * sizeof *pcm);
            if (larger == NULL) {
                fprintf(stderr, "bst_cli: out of memory\n");
                free(pcm);
                bst_close(h);
                return 1;
            }
            pcm = larger;
            got = bst_say(h, text, pcm, need);
        }
    }
    sample_rate = bst_rate(h);
    bst_close(h);
    if (got <= 0) {
        fprintf(stderr, "bst_cli: no audio produced\n");
        free(pcm);
        return 1;
    }

    /* Round to nearest even, like Python's round() on the same double, so
       moving this out of Python leaves the samples untouched. */
    if (has_gain && gain_db != 0.0) {
        double factor = pow(10.0, gain_db / 20.0);
        long k;
        for (k = 0; k < got; k++) {
            long value = lrint((double)pcm[k] * factor);
            if (value > 32767)
                value = 32767;
            else if (value < -32768)
                value = -32768;
            pcm[k] = (int16_t)value;
        }
    }

    out = stdout;
    if (strcmp(filename, "-") != 0) {
        out = fopen(filename, "wb");
        if (out == NULL) {
            fprintf(stderr, "bst_cli: cannot write %s\n", filename);
            free(pcm);
            return 1;
        }
    }
    wav_header(out, got, sample_rate);
    if (fwrite(pcm, sizeof *pcm, (size_t)got, out) != (size_t)got) {
        fprintf(stderr, "bst_cli: short write\n");
        if (out != stdout)
            fclose(out);
        free(pcm);
        return 1;
    }
    if (out != stdout)
        fclose(out);
    free(pcm);
    return 0;
}
