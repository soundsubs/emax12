/* emax12 -- Schwung audio_fx module
 *
 * Real-time stereo processor that runs incoming audio through an E-mu
 * Emax-style 12-bit sampling chain (see emax_dsp.c for the signal chain
 * and the assumptions/citations behind each stage).
 *
 * This connects to Move's JACK2 server the same way other Schwung C
 * modules do (confirmed via schwung-rex, which ships a C JACK client at
 * /data/UserData/move-anything/modules/). Install path for this module
 * type follows the same pattern:
 *   /data/UserData/move-anything/modules/audio_fx/emax12/
 *
 * NOTE ON PACKAGING: I could not verify the exact manifest/metadata
 * schema Schwung's module loader expects (module store listing fields,
 * icon, etc.) -- GitHub blocked directory browsing for me here. Before
 * shipping, copy the manifest file structure from an installed module
 * (e.g. open an existing audio_fx module's folder on your Move over
 * SFTP/Cyberduck per the Schwung manual) and mirror its fields for this
 * module rather than trusting a guessed schema.
 *
 * Control surface: since I don't have confirmed details of Schwung's
 * Shadow UI parameter-binding API either, control is exposed the
 * simplest robust way -- JACK MIDI CC on the module's MIDI input port,
 * which Shadow UI (or any MIDI controller/Move's own pads/knobs routed
 * to it) can drive without needing an undocumented UI API:
 *   CC 20: sample rate select (0-31=10kHz, 32-63=20kHz, 64-95=28kHz, 96-127=42kHz)
 *   CC 21: ladder filter cutoff (0-127 -> 200Hz-12kHz, log)
 *   CC 22: ladder filter resonance (0-127 -> 0-0.98)
 *   CC 23: ladder filter bypass (0-63=off, 64-127=on)
 */

#include <jack/jack.h>
#include <jack/midiport.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "emax_dsp.h"

static jack_port_t *in_l, *in_r, *out_l, *out_r, *midi_in;
static jack_client_t *client;

static EmaxVoice voice_l, voice_r;

static double cc_to_rate(int cc_val) {
    if (cc_val < 32) return EMAX_RATE_10K;
    if (cc_val < 64) return EMAX_RATE_20K;
    if (cc_val < 96) return EMAX_RATE_28K;
    return EMAX_RATE_42K;
}

static double cc_to_log_hz(int cc_val, double lo, double hi) {
    double t = cc_val / 127.0;
    return lo * pow(hi / lo, t);
}

static void handle_midi(jack_nframes_t nframes) {
    void *buf = jack_port_get_buffer(midi_in, nframes);
    jack_nframes_t count = jack_midi_get_event_count(buf);
    for (jack_nframes_t i = 0; i < count; i++) {
        jack_midi_event_t ev;
        jack_midi_event_get(&ev, buf, i);
        if (ev.size == 3 && (ev.buffer[0] & 0xF0) == 0xB0) {
            int cc = ev.buffer[1];
            int val = ev.buffer[2];
            switch (cc) {
                case 20: {
                    double rate = cc_to_rate(val);
                    emax_voice_set_rate(&voice_l, rate);
                    emax_voice_set_rate(&voice_r, rate);
                    break;
                }
                case 21: {
                    double hz = cc_to_log_hz(val, 200.0, 12000.0);
                    voice_l.ladder_cutoff_hz = hz;
                    voice_r.ladder_cutoff_hz = hz;
                    emax_ladder_set(&voice_l.ladder, hz, voice_l.ladder_resonance);
                    emax_ladder_set(&voice_r.ladder, hz, voice_r.ladder_resonance);
                    break;
                }
                case 22: {
                    double res = (val / 127.0) * 0.98;
                    voice_l.ladder_resonance = res;
                    voice_r.ladder_resonance = res;
                    emax_ladder_set(&voice_l.ladder, voice_l.ladder_cutoff_hz, res);
                    emax_ladder_set(&voice_r.ladder, voice_r.ladder_cutoff_hz, res);
                    break;
                }
                case 23: {
                    int on = val >= 64;
                    voice_l.ladder_enabled = on;
                    voice_r.ladder_enabled = on;
                    break;
                }
                default:
                    break;
            }
        }
    }
}

static int process(jack_nframes_t nframes, void *arg) {
    (void)arg;
    handle_midi(nframes);

    jack_default_audio_sample_t *inL = jack_port_get_buffer(in_l, nframes);
    jack_default_audio_sample_t *inR = jack_port_get_buffer(in_r, nframes);
    jack_default_audio_sample_t *outL = jack_port_get_buffer(out_l, nframes);
    jack_default_audio_sample_t *outR = jack_port_get_buffer(out_r, nframes);

    for (jack_nframes_t i = 0; i < nframes; i++) {
        outL[i] = (jack_default_audio_sample_t)emax_voice_process(&voice_l, (double)inL[i]);
        outR[i] = (jack_default_audio_sample_t)emax_voice_process(&voice_r, (double)inR[i]);
    }
    return 0;
}

static int on_sample_rate_change(jack_nframes_t nframes, void *arg) {
    (void)arg;
    double sr = (double)nframes;
    emax_voice_init(&voice_l, sr);
    emax_voice_init(&voice_r, sr);
    return 0;
}

static void jack_shutdown_cb(void *arg) {
    (void)arg;
    fprintf(stderr, "emax12: JACK server shut down, exiting\n");
    exit(1);
}

int main(void) {
    client = jack_client_open("emax12", JackNullOption, NULL);
    if (!client) {
        fprintf(stderr, "emax12: could not connect to JACK server\n");
        return 1;
    }

    double sr = (double)jack_get_sample_rate(client);
    emax_voice_init(&voice_l, sr);
    emax_voice_init(&voice_r, sr);
    /* Default to the "Emaxed" lo-fi setting people usually mean --
     * 10 kHz / 12-bit is called out by name in Emax literature. */
    emax_voice_set_rate(&voice_l, EMAX_RATE_10K);
    emax_voice_set_rate(&voice_r, EMAX_RATE_10K);

    in_l  = jack_port_register(client, "in_l",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    in_r  = jack_port_register(client, "in_r",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    out_l = jack_port_register(client, "out_l", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    out_r = jack_port_register(client, "out_r", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    midi_in = jack_port_register(client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (!in_l || !in_r || !out_l || !out_r || !midi_in) {
        fprintf(stderr, "emax12: failed to register ports\n");
        return 1;
    }

    jack_set_process_callback(client, process, NULL);
    jack_set_sample_rate_callback(client, on_sample_rate_change, NULL);
    jack_on_shutdown(client, jack_shutdown_cb, NULL);

    if (jack_activate(client)) {
        fprintf(stderr, "emax12: could not activate client\n");
        return 1;
    }

    fprintf(stderr, "emax12: running (default 10kHz/12-bit \"Emaxed\" mode)\n");
    /* Block forever; JACK calls process() on its own thread. */
    while (1) {
        sleep(3600);
    }
    return 0;
}
