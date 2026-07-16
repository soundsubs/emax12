/* emax12 -- Schwung audio_fx module
 *
 * Real-time stereo processor implementing Schwung's audio_fx_api_v2_t
 * (see audio_fx_api_v2.h and docs/MODULES.md in charlesvestal/schwung).
 * This is the CORRECTED architecture: earlier drafts of this project
 * built emax12 as a standalone JACK2 client, which is not how Schwung
 * audio_fx modules actually load -- they're dlopen()'d shared libraries
 * exporting move_audio_fx_init_v2(), hosted inside chain_host.c. The DSP
 * core (emax_dsp.c/h) is unchanged and already verified (make test);
 * only this wrapper is new.
 *
 * Per docs/MODULES.md: "When loaded inside Signal Chain, the chain host
 * expects the shared library at modules/audio_fx/<id>/<id>.so -- it does
 * not read module.json's dsp field." So this must be built and shipped
 * as Emax_FX.so specifically.
 *
 * Parameters (see module.json capabilities.chain_params):
 *   rate             enum: "10kHz" | "20kHz" | "28kHz" | "42kHz"
 *   ladder_cutoff    float, 200-12000 Hz
 *   ladder_resonance float, 0-0.98
 *   ladder_bypass    enum: "Off" | "On"
 *
 * KNOWN SIMPLIFICATION: create_instance() does not parse config_json
 * (saved config.json / module.json defaults) on load -- it starts from
 * fixed defaults (10kHz, ladder 8000Hz/0.2/enabled) and relies on the
 * host calling set_param() to apply saved/UI values afterward, which
 * matches how Shadow UI chain_params typically re-sync on slot load.
 * Writing a minimal JSON parser for config_json was out of scope here;
 * flagging this explicitly rather than silently guessing at behavior
 * that wasn't verified on-device.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "audio_fx_api_v2.h"
#include "emax_dsp.h"

typedef struct {
    EmaxVoice left;
    EmaxVoice right;
    double sample_rate;
} Emax12Instance;

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void apply_rate_string(Emax12Instance *inst, const char *val) {
    double rate = 10000.0;
    if (strcmp(val, "10kHz") == 0) rate = EMAX_RATE_10K;
    else if (strcmp(val, "20kHz") == 0) rate = EMAX_RATE_20K;
    else if (strcmp(val, "28kHz") == 0) rate = EMAX_RATE_28K;
    else if (strcmp(val, "42kHz") == 0) rate = EMAX_RATE_42K;
    emax_voice_set_rate(&inst->left, rate);
    emax_voice_set_rate(&inst->right, rate);
}

static void* create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json; /* see KNOWN SIMPLIFICATION above */

    Emax12Instance *inst = (Emax12Instance*)calloc(1, sizeof(Emax12Instance));
    if (!inst) return NULL;

    inst->sample_rate = 44100.0; /* Move's fixed host rate, per docs/MODULES.md */
    emax_voice_init(&inst->left, inst->sample_rate);
    emax_voice_init(&inst->right, inst->sample_rate);
    apply_rate_string(inst, "10kHz");
    inst->left.ladder_enabled = 1;
    inst->right.ladder_enabled = 1;
    inst->left.ladder_cutoff_hz = 8000.0;
    inst->right.ladder_cutoff_hz = 8000.0;
    inst->left.ladder_resonance = 0.2;
    inst->right.ladder_resonance = 0.2;
    emax_ladder_set(&inst->left.ladder, 8000.0, 0.2);
    emax_ladder_set(&inst->right.ladder, 8000.0, 0.2);

    return inst;
}

static void destroy_instance(void *instance) {
    free(instance);
}

static void process_block(void *instance, int16_t *audio_inout, int frames) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst) return;

    for (int i = 0; i < frames; i++) {
        double l = audio_inout[2 * i]     / 32768.0;
        double r = audio_inout[2 * i + 1] / 32768.0;

        l = emax_voice_process(&inst->left, l);
        r = emax_voice_process(&inst->right, r);

        double lo = clampd(l * 32767.0, -32768.0, 32767.0);
        double ro = clampd(r * 32767.0, -32768.0, 32767.0);

        audio_inout[2 * i]     = (int16_t)lround(lo);
        audio_inout[2 * i + 1] = (int16_t)lround(ro);
    }
}

static void set_param(void *instance, const char *key, const char *val) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst || !key || !val) return;

    if (strcmp(key, "rate") == 0) {
        apply_rate_string(inst, val);
    } else if (strcmp(key, "ladder_cutoff") == 0) {
        double hz = clampd(atof(val), 20.0, 18000.0);
        inst->left.ladder_cutoff_hz = hz;
        inst->right.ladder_cutoff_hz = hz;
        emax_ladder_set(&inst->left.ladder, hz, inst->left.ladder_resonance);
        emax_ladder_set(&inst->right.ladder, hz, inst->right.ladder_resonance);
    } else if (strcmp(key, "ladder_resonance") == 0) {
        double q = clampd(atof(val), 0.0, 0.98);
        inst->left.ladder_resonance = q;
        inst->right.ladder_resonance = q;
        emax_ladder_set(&inst->left.ladder, inst->left.ladder_cutoff_hz, q);
        emax_ladder_set(&inst->right.ladder, inst->right.ladder_cutoff_hz, q);
    } else if (strcmp(key, "ladder_bypass") == 0) {
        int on = (strcmp(val, "On") == 0);
        inst->left.ladder_enabled = !on;
        inst->right.ladder_enabled = !on;
    }
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "rate") == 0) {
        const char *s = "10kHz";
        double r = inst->left.target_sample_rate;
        if (r == EMAX_RATE_20K) s = "20kHz";
        else if (r == EMAX_RATE_28K) s = "28kHz";
        else if (r == EMAX_RATE_42K) s = "42kHz";
        int n = snprintf(buf, (size_t)buf_len, "%s", s);
        return n;
    } else if (strcmp(key, "ladder_cutoff") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.3f", inst->left.ladder_cutoff_hz);
        return n;
    } else if (strcmp(key, "ladder_resonance") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.3f", inst->left.ladder_resonance);
        return n;
    } else if (strcmp(key, "ladder_bypass") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%s", inst->left.ladder_enabled ? "Off" : "On");
        return n;
    }
    return -1;
}

static audio_fx_api_v2_t api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .process_block = process_block,
    .set_param = set_param,
    .get_param = get_param,
    .on_midi = NULL,
};

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    (void)host;
    return &api;
}
