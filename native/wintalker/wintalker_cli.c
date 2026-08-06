#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long (__cdecl *open_fn)(void **);
typedef long (__cdecl *close_fn)(void *);
typedef void (__cdecl *lex_fn)(const char *);
typedef long (__cdecl *voice_fn)(void *, short);
typedef void (__cdecl *set_fn)(void *, long);
typedef long (__cdecl *size_fn)(void);
typedef long (__cdecl *speak_fn)(void *, const char *, long, long);
typedef long (__cdecl *render_fn)(void *, short *, long, long *);

struct api {
    HMODULE dll;
    void *voice;
    close_fn close;
    voice_fn use_voice;
    set_fn set_rate;
    set_fn set_pitch;
    set_fn set_pause;
    size_fn buffer_size;
    speak_fn speak;
    render_fn render;
};

static void put_u16(FILE *out, uint16_t value) {
    fputc(value & 255, out); fputc(value >> 8, out);
}
static void put_u32(FILE *out, uint32_t value) {
    put_u16(out, value & 65535); put_u16(out, value >> 16);
}
static int get_u32(FILE *in, uint32_t *value) {
    unsigned char data[4];
    if (fread(data, 1, 4, in) != 4) return 0;
    *value = data[0] | ((uint32_t)data[1] << 8) |
             ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return 1;
}
static void write_wav(FILE *out, const unsigned char *pcm, size_t size) {
    fwrite("RIFF", 1, 4, out); put_u32(out, (uint32_t)size + 36);
    fwrite("WAVEfmt ", 1, 8, out); put_u32(out, 16);
    put_u16(out, 1); put_u16(out, 1); put_u32(out, 22050);
    put_u32(out, 44100); put_u16(out, 2); put_u16(out, 16);
    fwrite("data", 1, 4, out); put_u32(out, (uint32_t)size);
    fwrite(pcm, 1, size, out);
}

#define LOAD(dll, name, type) (type)GetProcAddress((dll), "_" #name)
static int initialize(struct api *api, const char *dll_path, const char *lex_path) {
    memset(api, 0, sizeof(*api));
    api->dll = LoadLibraryA(dll_path);
    if (!api->dll) return 0;
    lex_fn set_lex = LOAD(api->dll, SetLexPath, lex_fn);
    open_fn open_render = LOAD(api->dll, OpenSpeechRender, open_fn);
    api->close = LOAD(api->dll, CloseSpeech, close_fn);
    api->use_voice = LOAD(api->dll, UseVoice, voice_fn);
    api->set_rate = LOAD(api->dll, SetSpeechRate, set_fn);
    api->set_pitch = LOAD(api->dll, SetSpeechPitch, set_fn);
    api->set_pause = LOAD(api->dll, SetPausePercent, set_fn);
    api->buffer_size = LOAD(api->dll, GetMinBufferSize, size_fn);
    api->speak = LOAD(api->dll, SpeakBufferRender, speak_fn);
    api->render = LOAD(api->dll, RenderNext, render_fn);
    if (!set_lex || !open_render || !api->close || !api->use_voice ||
        !api->set_rate || !api->set_pitch || !api->buffer_size ||
        !api->speak || !api->render) return 0;
    set_lex(lex_path);
    if (open_render(&api->voice) || !api->voice) return 0;
    api->use_voice(api->voice, 0);
    if (api->set_pause) api->set_pause(api->voice, 30);
    return 1;
}

static unsigned char *render(
    struct api *api, const char *text, uint32_t length,
    int32_t rate, int32_t pitch, size_t *output_size
) {
    api->set_rate(api->voice, rate);
    api->set_pitch(api->voice, pitch);
    if (api->speak(api->voice, text, (long)length, 0)) return NULL;
    long capacity = api->buffer_size(), result = 0;
    short *chunk = malloc((size_t)capacity * 2);
    unsigned char *pcm = NULL;
    size_t size = 0;
    if (!chunk) return NULL;
    do {
        long written = 0;
        result = api->render(api->voice, chunk, capacity, &written);
        if (result < 0 || written < 0 || written > capacity) break;
        if (written) {
            unsigned char *grown = realloc(pcm, size + (size_t)written * 2);
            if (!grown) { result = -1; break; }
            pcm = grown;
            memcpy(pcm + size, chunk, (size_t)written * 2);
            size += (size_t)written * 2;
        }
    } while (result == 0);
    free(chunk);
    if (result < 0 || !size) { free(pcm); return NULL; }
    *output_size = size;
    return pcm;
}

static int server(struct api *api) {
    for (;;) {
        uint32_t length, rate, pitch;
        if (!get_u32(stdin, &length)) return feof(stdin) ? 0 : 1;
        if (!get_u32(stdin, &rate) || !get_u32(stdin, &pitch) || length > 1048576)
            return 1;
        char *text = malloc((size_t)length + 1);
        if (!text || fread(text, 1, length, stdin) != length) { free(text); return 1; }
        text[length] = 0;
        size_t pcm_size = 0;
        unsigned char *pcm = render(api, text, length, (int32_t)rate, (int32_t)pitch, &pcm_size);
        free(text);
        if (!pcm) { put_u32(stdout, 0); fflush(stdout); continue; }
        put_u32(stdout, (uint32_t)pcm_size + 44);
        write_wav(stdout, pcm, pcm_size);
        fflush(stdout);
        free(pcm);
    }
}

int main(int argc, char **argv) {
    int persistent = argc == 4 && !strcmp(argv[1], "--server");
    if ((!persistent && argc != 5)) {
        fprintf(stderr, "usage: wintalker_cli [--server] DLL LEX [RATE PITCH]\n");
        return 2;
    }
    const char *dll_path = argv[persistent ? 2 : 1];
    const char *lex_path = argv[persistent ? 3 : 2];
    struct api api;
    if (!initialize(&api, dll_path, lex_path)) {
        fprintf(stderr, "cannot initialize WinTalker.dll (%u)\n", (unsigned)GetLastError());
        return 1;
    }
    int result = 0;
    if (persistent) {
        result = server(&api);
    } else {
        size_t capacity = 4096, length = 0, pcm_size = 0;
        char *text = malloc(capacity);
        int ch;
        while (text && (ch = fgetc(stdin)) != EOF) {
            if (length + 1 >= capacity) {
                capacity *= 2; char *grown = realloc(text, capacity);
                if (!grown) { free(text); text = NULL; break; } text = grown;
            }
            text[length++] = ch < 128 ? (char)ch : '?';
        }
        unsigned char *pcm = text ? render(&api, text, (uint32_t)length,
            strtol(argv[3], NULL, 10), strtol(argv[4], NULL, 10), &pcm_size) : NULL;
        if (!pcm) result = 1; else { write_wav(stdout, pcm, pcm_size); free(pcm); }
        free(text);
    }
    api.close(api.voice);
    FreeLibrary(api.dll);
    return result;
}
