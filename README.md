# emax12

A Schwung `audio_fx` module for Ableton Move that runs incoming audio
through a signal chain modeling the E-mu Emax's 12-bit sampling engine,
switchable across the Emax's real variable-rate range: 10 kHz, 20 kHz,
28 kHz, and 42 kHz.

## What this models, and on what evidence

| Stage | Real Emax spec | Source |
|---|---|---|
| Bit depth | 12-bit linear, discrete (non-oversampled) ADC | vintagesynth.com, gearspace.com, nonaudio.wordpress.com |
| Sample rate | Variable, ~10 kHz–42 kHz | vintagesynth.com, deepsignalstudios.com |
| Anti-aliasing filter | 7-pole analog, ahead of the ADC | nonaudio.wordpress.com (Emax technical page) |
| Resonant filter | 8× SSM2047, 4-pole/24dB analog VCF/VCA | nonaudio.wordpress.com, deepsignalstudios.com |
| "Emaxed" lo-fi reference point | 10 kHz / 12-bit, named explicitly in Emax lore | deepsignalstudios.com |

What's **not** publicly documented anywhere I could find (no schematic-level
source): the exact pole alignment of the 7-pole AA filter, the exact
reconstruction-filter order on the output side, and SSM2047's
transistor-level transconductance curve. Where the spec is silent, this
project uses principled, clearly-labeled stand-ins (Butterworth cascades,
a saturating virtual-analog ladder) rather than guessing at "the real
thing." See the comments in `src/emax_dsp.h` for exactly where those
substitutions happen — if you get hold of Emax service-manual schematics,
that's where to plug in real coefficients.

## Signal chain

```
input -> 7-pole Butterworth AA filter (cutoff tracks target rate)
      -> sample-and-hold decimation to target rate (10/20/28/42 kHz)
      -> 12-bit linear quantization, no dither
      -> 3-pole Butterworth reconstruction filter
      -> SSM2047-style 4-pole saturating resonant ladder (optional)
      -> output
```

The sample-and-hold decimator is the important bit: it holds each
quantized sample for the full target-rate period at the host's native
sample rate, rather than doing a clean digital resample. That's what
produces the actual stairstep aliasing character instead of just "muffled
audio" — a common mistake in bitcrusher-style plugins.

## Status

- **DSP core**: implemented and tested (`make test` — runs a synthetic
  sweep through all four rate presets, checks for NaN/Inf, verifies the
  quantizer's step size against theoretical 12-bit).
- **JACK2 client**: implemented, MIDI CC control surface (see
  `src/main_jack.c` header comment for CC assignments).
- **Install path/user corrected** against real published modules
  (schwung-jv880, schwung-rex): SSH deploy goes to
  `/data/UserData/schwung/modules/audio_fx/emax12/` as `ableton@move.local`
  (not `root@`, and not the `move-anything` path used in an earlier draft
  of this script).
- **`release.json` added and confirmed against a real published module**
  (timncox/schwung-mark): Schwung Manager's "Install Custom Module" flow
  (`move.local:7700/modules`) reads `release.json` from the repo root and
  downloads/extracts the tarball named at `download_url`, which points to
  a GitHub Release asset. `scripts/package.sh` builds that tarball
  (`emax12-module.tar.gz`) from the ARM64 binary.

## Install

**Option A — Schwung Manager "Install Custom Module" (recommended):**
```sh
make test              # sanity check
./scripts/build.sh      # ARM64 cross-compile via Docker
./scripts/package.sh    # produces emax12-module.tar.gz
```
Then create a GitHub Release tagged to match `release.json`'s version
(e.g. `v0.1.0`), upload `emax12-module.tar.gz` as a release asset, and in
Schwung Manager (`move.local:7700/modules`) choose "Install Custom
Module" and give it this repo's URL.

**Option B — direct SSH deploy (bypasses the Module Store UI):**
```sh
./scripts/build.sh
./scripts/install.sh    # scp's straight to ableton@move.local
```

## Build

```sh
make test          # native sanity check, no JACK needed
./scripts/build.sh  # ARM64 cross-compile via Docker, for the actual Move
./scripts/install.sh  # deploy to move.local over SSH
```

## Controls (JACK MIDI CC on the module's `midi_in` port)

| CC | Function | Range |
|---|---|---|
| 20 | Sample rate | 0-31=10kHz, 32-63=20kHz, 64-95=28kHz, 96-127=42kHz |
| 21 | Ladder filter cutoff | 200 Hz – 12 kHz, log |
| 22 | Ladder filter resonance | 0 – 0.98 |
| 23 | Ladder filter bypass | <64=off, >=64=on |

Default on startup: 10 kHz / 12-bit (the "Emaxed" lo-fi reference point).
