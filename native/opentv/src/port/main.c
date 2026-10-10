/*
 * tv: the command line front end, and the library's first consumer.
 *
 * Everything here goes through tvtts.h, so the corpus difference test --
 * which drives this program over hundreds of configurations and requires
 * byte-identical PCM -- exercises the library rather than stepping around
 * it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tvtts.h"

typedef struct {
    uint8_t *data;
    size_t   len, cap;
} bytebuf;

static void bb_append(bytebuf *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 0x10000;
        while (cap < b->len + n)
            cap *= 2;
        b->data = (uint8_t *)realloc(b->data, cap);
        if (b->data == NULL) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static int write_wav(const char *path, const uint8_t *pcm, uint32_t n,
                     uint32_t rate)
{
    uint8_t h[44];
    FILE *f = fopen(path, "wb");

    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    memcpy(h, "RIFF", 4);          put32(h + 4, 36 + n);
    memcpy(h + 8, "WAVEfmt ", 8);  put32(h + 16, 16);
    put16(h + 20, 1);              put16(h + 22, 1);
    put32(h + 24, rate);           put32(h + 28, rate * 2);
    put16(h + 32, 2);              put16(h + 34, 16);
    memcpy(h + 36, "data", 4);     put32(h + 40, n);
    fwrite(h, 1, sizeof h, f);
    fwrite(pcm, 1, n, f);
    fclose(f);
    return 0;
}

static char *load_text(const char *arg, uint32_t *len)
{
    char *t;

    if (arg[0] == '@') {
        FILE *f = fopen(arg + 1, "rb");
        long n;

        if (f == NULL) {
            fprintf(stderr, "cannot read %s\n", arg + 1);
            exit(1);
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        t = (char *)malloc((size_t)n + 1);
        if (fread(t, 1, (size_t)n, f) != (size_t)n) {
            fprintf(stderr, "short read on %s\n", arg + 1);
            exit(1);
        }
        fclose(f);
        t[n] = '\0';
        *len = (uint32_t)n;
        return t;
    }
    *len = (uint32_t)strlen(arg);
    t = (char *)malloc(*len + 1);
    memcpy(t, arg, *len + 1);
    return t;
}

struct collect {
    bytebuf  pcm;
    int      marks;
    int      verbose;
};

static int on_event(const tvtts_event *ev, void *user)
{
    struct collect *c = (struct collect *)user;

    if (ev->type == TVTTS_AUDIO)
        bb_append(&c->pcm, ev->samples, (size_t)ev->count * 2);
    else if (ev->type == TVTTS_MARK) {
        c->marks++;
        if (c->verbose)
            fprintf(stderr, "mark %u at sample %u\n",
                    (unsigned)ev->mark, (unsigned)ev->sample_pos);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int voice = 0, phone = 0, i, verbose = 0, textin_mode = 0;
    int nuls = 2, opt_preformat = 1, opt_textin = 1, opt_sing = 0;
    long opt_pitch = -1, opt_speed = -1, opt_volume = -1;
    int hifi = 0;
    const char *lex_add[16];
    int n_lex = 0;
    const char *textarg, *out;
    tvtts_synth *s;
    struct collect c;
    char *text;
    uint32_t textlen, rate;

    memset(&c, 0, sizeof c);
    setvbuf(stderr, NULL, _IONBF, 0);
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc) voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-8")) phone = 1;
        else if (!strcmp(argv[i], "-H")) hifi = 1;
        else if (!strcmp(argv[i], "-m")) verbose = 1;
        else if (!strcmp(argv[i], "-M") && i + 1 < argc)
            textin_mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-z") && i + 1 < argc) nuls = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) opt_pitch = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) opt_speed = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-V") && i + 1 < argc) opt_volume = (long)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-C")) tvtts_set_extensions(0);
        else if (!strcmp(argv[i], "-X") && i + 1 < argc)
            tvtts_set_extensions((uint32_t)strtoul(argv[++i], NULL, 0));
        else if (!strcmp(argv[i], "-P0")) opt_preformat = 0;
        else if (!strcmp(argv[i], "-T0")) opt_textin = 0;
        else if (!strcmp(argv[i], "-G")) opt_sing = 1;
        /* -N <ms>: how long a note takes to slide in from the one before */
        else if (!strcmp(argv[i], "-N") && i + 1 < argc)
            tvtts_set_portamento(atoi(argv[++i]));
        /* -B <rate>,<depth> in hundredths of a hertz; depth 0 is no waver */
        else if (!strcmp(argv[i], "-B") && i + 1 < argc) {
            const char *v = argv[++i];
            char *end;
            long r = strtol(v, &end, 10);
            long d = (*end == ',') ? strtol(end + 1, NULL, 10) : -1;

            if (d < 0) { d = r; tvtts_get_vibrato((int *)&r, NULL); }
            tvtts_set_vibrato((int)r, (int)d);
        }
        else if (!strcmp(argv[i], "-L") && i + 1 < argc && n_lex < 16)
            lex_add[n_lex++] = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (argc - i != 2 || voice < 0 || voice >= tvtts_voice_count()) {
        fprintf(stderr, "usage: tv [-v voice] [-8] [-H] [-m] [-G] [-M mode] [-p pitch] [-s wpm]"
                        " [-V volume] [-B rate,depth] [-N ms] [-C] [-X mask] [-P0] [-T0] [-z nuls]"
                        " [-L word=phonemes] <text|@file> <out.wav>\n");
        return 2;
    }
    textarg = argv[i];
    out = argv[i + 1];
    c.verbose = verbose;

    for (i = 0; i < n_lex; i++) {
        char buf[256], *eq;

        strncpy(buf, lex_add[i], sizeof buf - 1);
        buf[sizeof buf - 1] = '\0';
        eq = strchr(buf, '=');
        if (eq == NULL) {
            fprintf(stderr, "bad -L %s\n", lex_add[i]);
            return 2;
        }
        *eq = '\0';
        if (tvtts_add_lexicon(buf, eq + 1) != 0) {
            fprintf(stderr, "lexicon rejected %s\n", lex_add[i]);
            return 2;
        }
    }

    rate = phone ? 8000u : (hifi ? tvtts_sample_rate_hz(TVTTS_SR_16K) : 11025u);
    s = tvtts_create(rate);
    if (s == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    tvtts_set_compat(s, opt_preformat, opt_textin, nuls);
    if (textin_mode != 0)
        tvtts_set_textin_mode(s, textin_mode);
    tvtts_set_voice(s, voice);
    tvtts_set_pitch(s, opt_pitch >= 0 ? (int)opt_pitch : tvtts_voice_pitch(voice));
    tvtts_set_rate(s, opt_speed >= 0 ? (int)opt_speed : tvtts_voice_rate(voice));
    tvtts_set_volume(s, opt_volume >= 0 ? (uint32_t)opt_volume : 0xffffu);

    /* The corpus is bytes on disk in the engine's own encoding, so it goes
     * in unconverted; a normal caller would use the utf8 or utf16 entry. */
    text = load_text(textarg, &textlen);
    if (opt_sing)
        tvtts_sing(s, text, on_event, &c);
    else
        tvtts_speak_bytes(s, text, textlen, on_event, &c);

    fprintf(stderr, "pcm=%u bytes (%.2f s @ %u Hz)%s\n", (unsigned)c.pcm.len,
            c.pcm.len / 2.0 / rate, (unsigned)rate,
            c.marks ? " with marks" : "");
    tvtts_destroy(s);
    return write_wav(out, c.pcm.data, (uint32_t)c.pcm.len, rate) ? 1 : 0;
}
