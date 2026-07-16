#ifndef EMAX12_AUDIO_FX_API_V2_H
#define EMAX12_AUDIO_FX_API_V2_H

/* Transcribed verbatim from charlesvestal/schwung's docs/MODULES.md
 * ("Audio FX Plugin API" section). We don't have (and can't clone) the
 * real src/host/audio_fx_api_v2.h from the main schwung repo, so this is
 * a hand-matched copy of the documented ABI. As a plain C struct of
 * function pointers with fixed-width types, this is safe to redefine
 * locally as long as field order/types match exactly -- if a future
 * schwung host version changes this ABI, this file is the one to update
 * (compare against docs/MODULES.md's "Audio FX Plugin API" section).
 */

#include <stdint.h>

typedef struct host_api_v1 {
    uint32_t api_version;

    int sample_rate;         /* 44100 */
    int frames_per_block;    /* 128 */

    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;

    void (*log)(const char *msg);

    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);

    int (*get_clock_status)(void);
} host_api_v1_t;

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source); /* optional, may be NULL */
} audio_fx_api_v2_t;

/* Entry point the chain host dlsym()'s out of Emax_FX.so */
audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host);

#endif /* EMAX12_AUDIO_FX_API_V2_H */
