/*
 * tv_cli -- the Retro TTS Pack's TruVoice front end, over OpenTV's tvtts_*.
 *
 * This keeps the command line the pack already spoke to the Windows cgrm_spk
 * utility, so app/src/retro_tts/engines/truevoice needs no change now that the
 * engine is native OpenTV instead of TV_ENG32.DLL under Unicorn:
 *
 *   tv_cli --data DIR --filename FILE --voice N --rate R --pitch P --volume V <text>
 *
 * --data is accepted and ignored: OpenTV compiles the engine's constant tables
 * in (generated/tvdata.s and generated/tvdata_es.s), so there is nothing to
 * load at run time.  --filename - writes the WAV to standard output, which is
 * how the pack reads it.
 *
 * The one trap: the original cgrm_spk took volume 0..16, but tvtts_set_volume
 * takes 0..65535 with 0xffff at full scale.  Handing the raw 0..16 straight to
 * the engine leaves it all but silent, so it is scaled by 65535/16.
 */
#include <stdint.h>
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
            fprintf(stderr, "tv_cli: out of memory\n");
            exit(1);
        }
        b->cap = cap;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;      p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;      p[1] = (uint8_t)(v >> 8);
}

static int write_wav(FILE *f, const uint8_t *pcm, uint32_t n, uint32_t rate)
{
    uint8_t h[44];

    memcpy(h, "RIFF", 4);           put32(h + 4, 36 + n);
    memcpy(h + 8, "WAVEfmt ", 8);   put32(h + 16, 16);
    put16(h + 20, 1);               put16(h + 22, 1);
    put32(h + 24, rate);            put32(h + 28, rate * 2);
    put16(h + 32, 2);               put16(h + 34, 16);
    memcpy(h + 36, "data", 4);      put32(h + 40, n);

    if (fwrite(h, 1, sizeof h, f) != sizeof h)
        return -1;
    return fwrite(pcm, 1, n, f) == n ? 0 : -1;
}

static int on_event(const tvtts_event *ev, void *user)
{
    bytebuf *b = (bytebuf *)user;

    if (ev->type == TVTTS_AUDIO)
        bb_append(b, ev->samples, (size_t)ev->count * 2);
    return 0;
}

int main(int argc, char **argv)
{
    const char *filename = "-";
    const char *text = NULL;
    long voice = 0, rate = -1, pitch = -1, volume = -1;
    tvtts_synth *s;
    bytebuf pcm;
    FILE *out;
    uint32_t vol;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            i++;                                /* compiled in: accepted, ignored */
        else if (!strcmp(argv[i], "--filename") && i + 1 < argc)
            filename = argv[++i];
        else if (!strcmp(argv[i], "--voice") && i + 1 < argc)
            voice = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc)
            rate = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc)
            pitch = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc)
            volume = strtol(argv[++i], NULL, 10);
        else if (text == NULL)
            text = argv[i];
        else {
            fprintf(stderr, "tv_cli: unexpected argument %s\n", argv[i]);
            return 2;
        }
    }
    if (text == NULL) {
        fprintf(stderr, "usage: tv_cli --data DIR --filename FILE --voice N "
                        "--rate R --pitch P --volume V <text>\n");
        return 2;
    }

    s = tvtts_create(11025);
    if (s == NULL) {
        fprintf(stderr, "tv_cli: out of memory\n");
        return 1;
    }
    tvtts_set_voice(s, (int)voice);
    /* Rate is 46..253 words per minute; the pack sends 50..250. */
    tvtts_set_rate(s, rate >= 0 ? (int)rate : tvtts_voice_rate((int)voice));
    /* Pitch is 50..400; the pack sends the voice's own default at its neutral. */
    tvtts_set_pitch(s, pitch >= 0 ? (int)pitch : tvtts_voice_pitch((int)voice));
    vol = volume < 0 ? 0xffffu : (uint32_t)(volume * 65535 / 16);
    if (vol > 0xffffu)
        vol = 0xffffu;
    tvtts_set_volume(s, vol);

    memset(&pcm, 0, sizeof pcm);
    tvtts_speak_bytes(s, text, (uint32_t)strlen(text), on_event, &pcm);
    tvtts_destroy(s);

    if (pcm.len == 0) {
        fprintf(stderr, "tv_cli: no audio produced\n");
        free(pcm.data);
        return 1;
    }
    if (!strcmp(filename, "-")) {
        out = stdout;
    } else if ((out = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "tv_cli: cannot write %s\n", filename);
        free(pcm.data);
        return 1;
    }
    if (write_wav(out, pcm.data, (uint32_t)pcm.len, 11025) != 0) {
        fprintf(stderr, "tv_cli: short write\n");
        if (out != stdout)
            fclose(out);
        free(pcm.data);
        return 1;
    }
    if (out != stdout)
        fclose(out);
    free(pcm.data);
    return 0;
}
