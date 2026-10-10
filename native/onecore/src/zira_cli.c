/* zira: speak text with the David / Zira / Mark library (zira_tts.h) into a WAV file.
 *
 * usage: zira [options] "text"|@file.txt out.wav
 *   --data DIR     voice data folder (default C:/Windows/Speech_OneCore/Engines/TTS/en-US, or ZIRA_DATA)
 *   --voice NAME   David | Zira | Mark        (default David)
 *   --rate N       -10..20   --volume N  0..100   --pitch N  -10..10
 *   --emotion NAME happy | sad | angry | neutral
 *   --xml          the text is SAPI XML       --events   print sentence/word/bookmark events
 */
#include "zira_tts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *f;
    const char *text;
    int events;
} sink;

static void put(FILE *f, unsigned v, int n)
{
    int i;
    for (i = 0; i < n; i++) fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static int on_audio(const int16_t *pcm, size_t n, void *user)
{
    sink *s = (sink *)user;
    size_t i;
    for (i = 0; i < n; i++) put(s->f, (unsigned)(uint16_t)pcm[i], 2);
    return 0;
}

static void on_event(const zira_event *e, void *user)
{
    sink *s = (sink *)user;
    static const char *const names[] = {"?", "sentence", "word", "bookmark", "end"};
    if (!s->events) return;
    printf("%8.3f s  %-8s", e->audio_pos / (double)ZIRA_SAMPLE_RATE, names[e->type > 0 && e->type <= 4 ? e->type : 0]);
    if (e->type == ZIRA_EV_BOOKMARK) printf(" \"%s\"", e->name ? e->name : "");
    else if (e->type != ZIRA_EV_END) printf(" \"%.*s\"", (int)e->text_len, s->text + e->text_pos);
    printf("\n");
}

static char *read_file(const char *fn)
{
    FILE *f = fopen(fn, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (b) {
        n = (long)fread(b, 1, (size_t)n, f);
        b[n] = 0;
    }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    const char *dir = getenv("ZIRA_DATA"), *voice = "David", *emotion = NULL, *text = NULL, *out = NULL;
    int i, rate = 0, volume = 100, pitch = 0, flags = 0, rc;
    char err[256];
    zira_tts *t;
    zira_callbacks cb;
    sink s;
    long pos;
    memset(&s, 0, sizeof s);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--voice") && i + 1 < argc) voice = argv[++i];
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) volume = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) pitch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--emotion") && i + 1 < argc) emotion = argv[++i];
        else if (!strcmp(argv[i], "--xml")) flags |= ZIRA_SPEAK_XML;
        else if (!strcmp(argv[i], "--events")) s.events = 1;
        else if (!text) text = argv[i];
        else if (!out) out = argv[i];
    }
    if (!text || !out) {
        fprintf(stderr, "usage: zira [--data DIR] [--voice David|Zira|Mark] [--rate N] [--volume N] [--pitch N]\n"
                        "            [--emotion happy|sad|angry] [--xml] [--events] \"text\"|@file.txt out.wav\n");
        return 1;
    }
    if (text[0] == '@') {
        char *b = read_file(text + 1);
        if (!b) {
            fprintf(stderr, "cannot read %s\n", text + 1);
            return 1;
        }
        text = b;
        if ((unsigned char)b[0] == 0xef && (unsigned char)b[1] == 0xbb && (unsigned char)b[2] == 0xbf) text += 3;
    }
    t = zira_tts_open(dir, voice, err, sizeof err);
    if (!t) {
        fprintf(stderr, "zira: %s\n", err);
        return 1;
    }
    zira_tts_set_rate(t, rate);
    zira_tts_set_volume(t, volume);
    zira_tts_set_pitch(t, pitch);
    if (emotion && strcmp(emotion, "neutral") && zira_tts_set_emotion(t, emotion)) {
        fprintf(stderr, "zira: %s has no emotion \"%s\" (have:", voice, emotion);
        for (i = 0; zira_tts_emotion_name(t, i); i++) fprintf(stderr, " %s", zira_tts_emotion_name(t, i));
        fprintf(stderr, ")\n");
        zira_tts_close(t);
        return 1;
    }
    s.f = fopen(out, "wb");
    if (!s.f) {
        fprintf(stderr, "cannot write %s\n", out);
        zira_tts_close(t);
        return 1;
    }
    s.text = text;
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, s.f);
    put(s.f, 16, 4), put(s.f, 1, 2), put(s.f, 1, 2), put(s.f, ZIRA_SAMPLE_RATE, 4), put(s.f, ZIRA_SAMPLE_RATE * 2, 4),
        put(s.f, 2, 2), put(s.f, 16, 2);
    fwrite("data\0\0\0\0", 1, 8, s.f);
    cb.audio = on_audio;
    cb.event = on_event;
    cb.user = &s;
    rc = zira_tts_speak(t, text, flags, &cb);
    pos = ftell(s.f);
    fseek(s.f, 4, SEEK_SET);
    put(s.f, (unsigned)(pos - 8), 4);
    fseek(s.f, 40, SEEK_SET);
    put(s.f, (unsigned)(pos - 44), 4);
    fclose(s.f);
    zira_tts_close(t);
    if (rc < 0) fprintf(stderr, "zira: synthesis error\n");
    else printf("%ld samples (%.2f s)\n", (pos - 44) / 2, (pos - 44) / 2 / (double)ZIRA_SAMPLE_RATE);
    return rc < 0;
}
