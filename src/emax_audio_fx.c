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
 *   ladder_cutoff    float, 0.0-1.0 normalized, log-mapped to 200-12000 Hz
 *                    internally (see cutoff_norm_to_hz/hz_to_norm below).
 *                    This is the filter's REST position.
 *   ladder_resonance float, 0-0.98
 *   env_amount       float, 0.0-1.0 (0-100%). How much the ADSR pushes
 *                    the cutoff open ABOVE ladder_cutoff, additive,
 *                    clamped so it can never exceed 1.0 (fully open).
 *                    At env_amount=0, the envelope has no audible effect
 *                    regardless of A/D/S/R. At ladder_cutoff=1.0 (fully
 *                    open already), env_amount has no room left to add,
 *                    so it becomes inaudible too -- both by design.
 *
 * LADDER_CUTOFF IS NORMALIZED, NOT RAW HZ: on-device testing showed the
 * Shadow UI applies a fixed absolute float step (0.01) to knob turns
 * regardless of a chain_params entry's declared "step" -- so a knob
 * spanning 200-12000 (declared step or not) always took ~1,180,000
 * ticks... no, ~1,180 knob-detents to sweep, since the effective step
 * was a flat 0.01 *in raw units*, not scaled to the declared range. The
 * only reliable lever turned out to be shrinking the declared range
 * itself. docs/MODULES.md describes float params as "0.0-1.0 typical",
 * which matches: a 0-1 range with a 0.01 step gives exactly ~100 ticks
 * full-sweep, which is what we actually want. So ladder_cutoff is now
 * normalized 0-1 and log-mapped to 200-12000 Hz here (log mapping also
 * happens to suit filter-cutoff perception better than linear Hz would
 * have). Known tradeoff: the Shadow UI most likely displays the raw
 * 0.00-1.00 value on-screen rather than the derived Hz number -- I don't
 * have confirmed info on whether it does unit-aware display formatting
 * for arbitrary units like "Hz".
 *
 * KNOWN SIMPLIFICATION: create_instance() does not parse config_json
 * (saved config.json / module.json defaults) on load -- it starts from
 * fixed defaults (10kHz, ladder 8000Hz/0.2/enabled) and relies on the
 * host calling set_param() to apply saved/UI values afterward, which
 * matches how Shadow UI chain_params typically re-sync on slot load.
 * Writing a minimal JSON parser for config_json was out of scope here;
 * flagging this explicitly rather than silently guessing at behavior
 * that wasn't verified on-device.
 *
 * NOTE-TRACKED ALIASING (experimental, on_midi):
 * On a real Emax, aliasing artifacts transpose with playback pitch,
 * because the target crush-rate was fixed at RECORD time and the
 * played-back note just speeds up or slows down the whole waveform
 * (varispeed) -- artifacts included. This module is downstream of
 * whatever's generating audio, so it doesn't know the note being played
 * unless the host routes MIDI to on_midi() via a capture rule (see
 * module.json's "capture" field). When it does, the "rate" parameter is
 * treated as the *base* rate at C4 (MIDI note 60), and each note-on
 * scales the effective crush rate by 2^((note-60)/12) -- so C5 crushes
 * at 2x the base rate (finer aliasing) and C3 at 0.5x (coarser),
 * matching the user's spec.
 *
 * CAVEAT: whether module-level "capture" of the pad/note group mirrors
 * notes to on_midi vs. actually stealing them from the sound generator
 * in the same chain slot is NOT something I could confirm from
 * available docs -- it's described generically as blocking captured
 * controls from their normal destination. If enabling capture in
 * module.json silences the synth, remove the "capture" block and this
 * feature falls back to untransposed aliasing (previous behavior)
 * without needing a rebuild.
 *
 * Also a real simplification: this tracks a single global "current
 * note" (last note-on wins), not per-voice/polyphonic pitch. Chords
 * will all alias at the last-played note's rate, not each note's own.
 *
 * FILTER ENVELOPE (knobs 5-8, also depends on on_midi/capture):
 * env_attack/env_decay/env_sustain/env_release drive a standard ADSR
 * (rest state 0, peaks at 1 during attack, settles at sustain level,
 * returns to 0 on release) whose output is scaled by env_amount (knob 4)
 * and added on top of ladder_cutoff (knob 2, now the filter's REST
 * position rather than a peak target) -- see effective cutoff formula
 * in process_block(). A/D/R are normalized 0-1, log-mapped to
 * 1ms-5000ms (same reasoning as ladder_cutoff's normalization). Sustain
 * is a plain 0-1 level, no mapping needed.
 *
 * Because env_amount defaults meaningfully and the envelope itself rests
 * at 0 when no note has played, there's no special fallback state needed
 * this time (unlike the earlier multiplicative design): with
 * env_amount=0 the envelope is simply inaudible regardless of whether
 * on_midi/capture ever fires at all.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "audio_fx_api_v2.h"
#include "emax_dsp.h"

/* Stored so on_midi/create_instance can emit diagnostic host->log() calls
 * (see /system/logs in Schwung Manager) -- added to definitively answer
 * whether on_midi/capture ever actually fires at all, rather than
 * continuing to guess blind about the "pads" capture group. */
static const host_api_v1_t *g_host = NULL;

static void dbg_log(const char *msg) {
    if (g_host && g_host->log) {
        g_host->log(msg);
    }
}

typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvStage;

typedef struct {
    EmaxVoice left;
    EmaxVoice right;
    double sample_rate;
    double base_rate_hz;   /* the user-selected preset, applies at C4 (note 60) */
    int current_note;      /* last note-on received via on_midi; -1 = none yet */

    double base_cutoff_norm; /* ladder_cutoff knob position; filter's REST cutoff */
    double env_amount;       /* 0.0-1.0; how much ADSR adds atop base_cutoff_norm */

    EnvStage env_stage;
    double env_level;          /* current 0.0-1.0 envelope output */
    double attack_sec;
    double decay_sec;
    double sustain_level;      /* 0.0-1.0, plain linear */
    double release_sec;
    double release_start_level;
} Emax12Instance;

static double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* env_attack/env_decay/env_release wire values are normalized 0.0-1.0;
 * log-mapped to 1ms-5000ms. */
#define ENV_TIME_MIN 0.001
#define ENV_TIME_MAX 5.0

static double time_norm_to_sec(double norm) {
    norm = clampd(norm, 0.0, 1.0);
    return ENV_TIME_MIN * pow(ENV_TIME_MAX / ENV_TIME_MIN, norm);
}

static double time_sec_to_norm(double sec) {
    sec = clampd(sec, ENV_TIME_MIN, ENV_TIME_MAX);
    return log(sec / ENV_TIME_MIN) / log(ENV_TIME_MAX / ENV_TIME_MIN);
}

/* ladder_cutoff wire value is normalized 0.0-1.0; log-mapped to the
 * musically useful 200-12000 Hz range. See header comment for why. */
#define CUTOFF_HZ_MIN 200.0
#define CUTOFF_HZ_MAX 12000.0

static double cutoff_norm_to_hz(double norm) {
    norm = clampd(norm, 0.0, 1.0);
    return CUTOFF_HZ_MIN * pow(CUTOFF_HZ_MAX / CUTOFF_HZ_MIN, norm);
}

static double cutoff_hz_to_norm(double hz) {
    hz = clampd(hz, CUTOFF_HZ_MIN, CUTOFF_HZ_MAX);
    return log(hz / CUTOFF_HZ_MIN) / log(CUTOFF_HZ_MAX / CUTOFF_HZ_MIN);
}

static double rate_string_to_hz(const char *val) {
    if (strcmp(val, "20kHz") == 0) return EMAX_RATE_20K;
    if (strcmp(val, "28kHz") == 0) return EMAX_RATE_28K;
    if (strcmp(val, "42kHz") == 0) return EMAX_RATE_42K;
    return EMAX_RATE_10K;
}

/* Recompute and apply the effective crush rate = base_rate_hz scaled by
 * the current note's distance from C4 (note 60), in semitones. */
static void update_effective_rate(Emax12Instance *inst) {
    double ratio = 1.0;
    if (inst->current_note >= 0) {
        ratio = pow(2.0, (inst->current_note - 60) / 12.0);
    }
    double effective_hz = clampd(inst->base_rate_hz * ratio, 4000.0, 48000.0);
    emax_voice_set_rate(&inst->left, effective_hz);
    emax_voice_set_rate(&inst->right, effective_hz);
}

static void apply_rate_string(Emax12Instance *inst, const char *val) {
    inst->base_rate_hz = rate_string_to_hz(val);
    update_effective_rate(inst);
}

/* Advance the ADSR by one audio frame's worth of time (dt seconds). */
static void env_process(Emax12Instance *inst, double dt) {
    switch (inst->env_stage) {
    case ENV_ATTACK:
        if (inst->attack_sec <= 0.0001) {
            inst->env_level = 1.0;
            inst->env_stage = ENV_DECAY;
        } else {
            inst->env_level += dt / inst->attack_sec;
            if (inst->env_level >= 1.0) {
                inst->env_level = 1.0;
                inst->env_stage = ENV_DECAY;
            }
        }
        break;
    case ENV_DECAY: {
        double target = inst->sustain_level;
        if (inst->decay_sec <= 0.0001) {
            inst->env_level = target;
            inst->env_stage = ENV_SUSTAIN;
        } else {
            /* Decay always ramps from 1.0 (attack's completion point)
             * down to sustain_level, over decay_sec. */
            double total_delta = 1.0 - target;
            inst->env_level -= (total_delta * dt / inst->decay_sec);
            if (inst->env_level <= target) {
                inst->env_level = target;
                inst->env_stage = ENV_SUSTAIN;
            }
        }
        break;
    }
    case ENV_SUSTAIN:
        inst->env_level = inst->sustain_level;
        break;
    case ENV_RELEASE:
        if (inst->release_sec <= 0.0001) {
            inst->env_level = 0.0;
            inst->env_stage = ENV_IDLE;
        } else {
            inst->env_level -= (inst->release_start_level * dt / inst->release_sec);
            if (inst->env_level <= 0.0) {
                inst->env_level = 0.0;
                inst->env_stage = ENV_IDLE;
            }
        }
        break;
    case ENV_IDLE:
    default:
        /* Hold current level (0 at rest, since env_level now starts at
         * 0.0 -- see create_instance). */
        break;
    }
}

static void env_note_on(Emax12Instance *inst) {
    inst->env_stage = ENV_ATTACK; /* ramps from wherever env_level currently is */
}

static void env_note_off(Emax12Instance *inst) {
    inst->release_start_level = inst->env_level;
    inst->env_stage = ENV_RELEASE;
}

static void* create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json; /* see KNOWN SIMPLIFICATION above */
    dbg_log("[Emax_FX] create_instance called");

    Emax12Instance *inst = (Emax12Instance*)calloc(1, sizeof(Emax12Instance));
    if (!inst) return NULL;

    inst->sample_rate = 44100.0; /* Move's fixed host rate, per docs/MODULES.md */
    inst->current_note = -1; /* no note seen yet -> untransposed base rate */
    emax_voice_init(&inst->left, inst->sample_rate);
    emax_voice_init(&inst->right, inst->sample_rate);
    apply_rate_string(inst, "10kHz");
    /* Ladder filter is always engaged now -- knob 4 was repurposed from
     * an on/off toggle to env_amount (see header comment). */
    inst->left.ladder_enabled = 1;
    inst->right.ladder_enabled = 1;
    inst->base_cutoff_norm = cutoff_hz_to_norm(8000.0);
    inst->env_amount = 0.3; /* 30% -- audible out of the box, not overwhelming */
    inst->left.ladder_cutoff_hz = 8000.0;
    inst->right.ladder_cutoff_hz = 8000.0;
    inst->left.ladder_resonance = 0.2;
    inst->right.ladder_resonance = 0.2;
    emax_ladder_set(&inst->left.ladder, 8000.0, 0.2);
    emax_ladder_set(&inst->right.ladder, 8000.0, 0.2);

    inst->env_stage = ENV_IDLE;
    inst->env_level = 0.0; /* standard ADSR rest state */
    inst->attack_sec = 0.01;   /* 10ms */
    inst->decay_sec = 0.2;     /* 200ms */
    inst->sustain_level = 0.7;
    inst->release_sec = 0.3;   /* 300ms */

    return inst;
}

static void destroy_instance(void *instance) {
    free(instance);
}

static void process_block(void *instance, int16_t *audio_inout, int frames) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst) return;

    double dt = 1.0 / inst->sample_rate;

    for (int i = 0; i < frames; i++) {
        env_process(inst, dt);
        /* Effective cutoff = rest position (ladder_cutoff) plus however
         * much the envelope is currently contributing, scaled by
         * env_amount, clamped so it can never exceed fully-open (1.0).
         * At env_amount=0 or ladder_cutoff=1.0, this reduces to a no-op
         * -- see header comment. */
        double eff_norm = clampd(inst->base_cutoff_norm + inst->env_level * inst->env_amount, 0.0, 1.0);
        double eff_hz = cutoff_norm_to_hz(eff_norm);
        emax_ladder_set(&inst->left.ladder, eff_hz, inst->left.ladder_resonance);
        emax_ladder_set(&inst->right.ladder, eff_hz, inst->right.ladder_resonance);

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
        inst->base_cutoff_norm = clampd(atof(val), 0.0, 1.0);
    } else if (strcmp(key, "ladder_resonance") == 0) {
        double q = clampd(atof(val), 0.0, 0.98);
        inst->left.ladder_resonance = q;
        inst->right.ladder_resonance = q;
    } else if (strcmp(key, "env_amount") == 0) {
        inst->env_amount = clampd(atof(val), 0.0, 1.0);
    } else if (strcmp(key, "env_attack") == 0) {
        inst->attack_sec = time_norm_to_sec(atof(val));
    } else if (strcmp(key, "env_decay") == 0) {
        inst->decay_sec = time_norm_to_sec(atof(val));
    } else if (strcmp(key, "env_sustain") == 0) {
        inst->sustain_level = clampd(atof(val), 0.0, 1.0);
    } else if (strcmp(key, "env_release") == 0) {
        inst->release_sec = time_norm_to_sec(atof(val));
    }
}

static int get_param(void *instance, const char *key, char *buf, int buf_len) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "rate") == 0) {
        const char *s = "10kHz";
        double r = inst->base_rate_hz;
        if (r == EMAX_RATE_20K) s = "20kHz";
        else if (r == EMAX_RATE_28K) s = "28kHz";
        else if (r == EMAX_RATE_42K) s = "42kHz";
        int n = snprintf(buf, (size_t)buf_len, "%s", s);
        return n;
    } else if (strcmp(key, "ladder_cutoff") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.4f", inst->base_cutoff_norm);
        return n;
    } else if (strcmp(key, "ladder_resonance") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.3f", inst->left.ladder_resonance);
        return n;
    } else if (strcmp(key, "env_amount") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.3f", inst->env_amount);
        return n;
    } else if (strcmp(key, "env_attack") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.4f", time_sec_to_norm(inst->attack_sec));
        return n;
    } else if (strcmp(key, "env_decay") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.4f", time_sec_to_norm(inst->decay_sec));
        return n;
    } else if (strcmp(key, "env_sustain") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.3f", inst->sustain_level);
        return n;
    } else if (strcmp(key, "env_release") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.4f", time_sec_to_norm(inst->release_sec));
        return n;
    }
    return -1;
}

static void on_midi(void *instance, const uint8_t *msg, int len, int source) {
    Emax12Instance *inst = (Emax12Instance*)instance;
    if (!inst || !msg || len < 1) return;

    char dbgbuf[96];
    if (len >= 3) {
        snprintf(dbgbuf, sizeof(dbgbuf), "[Emax_FX] on_midi: status=0x%02x note=%d vel=%d src=%d len=%d",
                 msg[0], msg[1], msg[2], source, len);
    } else {
        snprintf(dbgbuf, sizeof(dbgbuf), "[Emax_FX] on_midi: status=0x%02x src=%d len=%d",
                 msg[0], source, len);
    }
    dbg_log(dbgbuf);

    if (len < 3) return;
    uint8_t status = msg[0] & 0xF0;
    uint8_t note = msg[1];
    uint8_t velocity = msg[2];

    if (status == 0x90 && velocity > 0) {
        /* Note On */
        inst->current_note = note;
        update_effective_rate(inst);
        env_note_on(inst);
    } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
        /* Note Off -- monophonic simplification: only react if this was
         * the note we're currently tracking, so a released lower/higher
         * note in a chord doesn't yank the pitch or envelope back. */
        if (inst->current_note == note) {
            env_note_off(inst);
            /* Pitch tracking: leave the last rate in place rather than
             * snapping back to base -- avoids an audible jump at
             * release. Real polyphonic tracking would need a per-voice
             * note stack, out of scope. */
        }
    }
}

static audio_fx_api_v2_t api = {
    .api_version = 2,
    .create_instance = create_instance,
    .destroy_instance = destroy_instance,
    .process_block = process_block,
    .set_param = set_param,
    .get_param = get_param,
    .on_midi = on_midi,
};

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    dbg_log("[Emax_FX] move_audio_fx_init_v2 called -- plugin loaded");
    return &api;
}
