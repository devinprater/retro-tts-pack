#ifndef RETRO_TTS_TRUEVOICE_SHIM_H
#define RETRO_TTS_TRUEVOICE_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tv_engine tv_engine;
typedef void (*tv_sample_cb)(const int16_t *, size_t, void *);
tv_engine *tv_create(const char *staging_dir);
void tv_destroy(tv_engine *engine);
int cgrm_init(tv_engine *engine);
int cgrm_speak(tv_engine *engine, const char *text, int voice, int rate,
               int pitch, int volume, tv_sample_cb callback, void *context);

#ifdef __cplusplus
}
#endif
#endif
