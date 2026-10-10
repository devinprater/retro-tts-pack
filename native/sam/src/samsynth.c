/* samsynth: render a segment file with libsam.
 *
 * A segment file is a sequence of 0x140-byte records in the original engine's layout
 * (tools/extract_segments.py pulls them out of a samtap log):
 *   +0x00 i32 unit, +0x04 f32 duration (s), +0x10 i32 knots, +0x14 f32 t[20], +0x64 f32 f0[20], +0xB4 f32 amp[20]
 *
 * usage: samsynth Sam.spd in.seg out.wav [--pitch x] [--speed x] [--gain x] [--vibrato depth rate] [--no-reverse]
 */
#include "sam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEG_SIZE 0x140

static uint32_t rd_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float rd_f32(const unsigned char *p)
{
    uint32_t u = rd_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void put_u32(FILE *f, uint32_t v)
{
    unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, unsigned v)
{
    unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, f);
}

static void write_wav_header(FILE *f, int rate, uint32_t data_bytes)
{
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 1);
    put_u32(f, (uint32_t)rate);
    put_u32(f, (uint32_t)rate * 2);
    put_u16(f, 2);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);
}

typedef struct {
    FILE *f;
    uint32_t samples;
} sink;

static void on_pcm(const int16_t *pcm, size_t n, void *user)
{
    sink *s = user;
    size_t i;
    for (i = 0; i < n; i++) put_u16(s->f, (uint16_t)pcm[i]);
    s->samples += (uint32_t)n;
}

int main(int argc, char **argv)
{
    char err[256];
    sam_voice *v;
    sam_synth *syn;
    sam_params p;
    FILE *in;
    sink out;
    unsigned char rec[SEG_SIZE];
    int i, count = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: samsynth voice.spd in.seg out.wav [--pitch x] [--speed x] [--gain x] "
                        "[--vibrato depth rate] [--no-reverse]\n");
        return 1;
    }
    sam_params_default(&p);
    for (i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--pitch") && i + 1 < argc) p.pitch_scale = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) p.speed = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc) p.gain = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--vibrato") && i + 2 < argc) {
            p.vibrato = (float)atof(argv[++i]);
            p.vibrato_rate = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--no-reverse")) p.no_reverse = 1;
        else {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 1;
        }
    }
    v = sam_voice_load(argv[1], err, sizeof err);
    if (!v) {
        fprintf(stderr, "voice: %s\n", err);
        return 1;
    }
    in = fopen(argv[2], "rb");
    out.f = fopen(argv[3], "wb");
    out.samples = 0;
    if (!in || !out.f) {
        fprintf(stderr, "cannot open files\n");
        return 1;
    }
    write_wav_header(out.f, sam_voice_rate(v), 0);
    syn = sam_synth_new(v, &p);
    while (fread(rec, 1, SEG_SIZE, in) == SEG_SIZE) {
        sam_segment g;
        memset(&g, 0, sizeof g);
        int k;
        g.unit = (int)rd_u32(rec);
        g.dur = rd_f32(rec + 4);
        g.n_knots = (int)rd_u32(rec + 0x10);
        if (g.n_knots > SAM_MAX_KNOTS) g.n_knots = SAM_MAX_KNOTS;
        for (k = 0; k < SAM_MAX_KNOTS; k++) {
            g.t[k] = rd_f32(rec + 0x14 + 4 * k);
            g.f0[k] = rd_f32(rec + 0x64 + 4 * k);
            g.amp[k] = rd_f32(rec + 0xB4 + 4 * k);
        }
        if (sam_synth_segment(syn, &g, on_pcm, &out) != 0) fprintf(stderr, "segment %d (unit %d) failed\n", count, g.unit);
        count++;
    }
    fseek(out.f, 0, SEEK_SET);
    write_wav_header(out.f, sam_voice_rate(v), out.samples * 2);
    fclose(out.f);
    fclose(in);
    sam_synth_free(syn);
    sam_voice_free(v);
    printf("%d segments, %u samples\n", count, out.samples);
    return 0;
}
