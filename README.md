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
  quantizer's step size against theoretical 12-bit). Unchanged and still
  passing through every architecture revision below.
- **Architecture corrected**: earlier drafts of this project built
  `emax12` as a standalone JACK2 client. That is **not** how Schwung
  `audio_fx` modules actually load. Per `docs/MODULES.md` in
  charlesvestal/schwung, an audio_fx module is a **shared library**
  (`Emax_FX.so`) that the chain host `dlopen()`s and calls through
  `audio_fx_api_v2_t` (`create_instance` / `process_block` / `set_param`
  / `get_param`), located via a **required** `module.json` manifest at
  the module root. This mismatch — not a missing file — is why the
  Module Store kept rejecting the tarball with "No module.json found in
  tarball." The DSP core itself needed no changes; only the wrapper
  (`src/emax_audio_fx.c`, replacing the old `src/main_jack.c`) and the
  build/package/install scripts were rewritten.
- **`module.json`** added at the repo root, matching the documented
  schema (`id`, `name`, `version`, `api_version`, `capabilities.chainable`,
  `capabilities.component_type: "audio_fx"`, `capabilities.chain_params`
  for the four exposed parameters).
- **Tarball structure corrected**: `Emax_FX-module.tar.gz` now extracts to
  `Emax_FX/module.json` + `emax12/Emax_FX.so`, matching the schema
  documented in `docs/MODULES.md`'s "Publishing to Module Store" section.
- **Install path/user** confirmed against real published modules
  (schwung-jv880, schwung-rex): `/data/UserData/schwung/modules/audio_fx/emax12/`
  as `ableton@move.local`.
- **`release.json`** confirmed against `docs/MODULES.md`'s documented
  schema and a real published module (timncox/schwung-mark). Schwung
  Manager's "Install Custom Module" flow (`move.local:7700/modules`)
  reads `release.json` from the repo root and downloads/extracts the
  tarball named at `download_url`.
- **Not yet verified on-device**: the plugin loads and passes `make test`
  in isolation, but has not yet been confirmed to load correctly inside
  Schwung's chain host on real Move hardware (dlopen success, parameter
  round-trip through Shadow UI, actual audio through the chain). That's
  the next real test.

## Install

**Option A — Schwung Manager "Install Custom Module" (recommended):**
```sh
make test              # sanity check
./scripts/build.sh      # ARM64 cross-compile via Docker -> Emax_FX.so
./scripts/package.sh    # produces Emax_FX-module.tar.gz (module.json + Emax_FX.so)
```
Then create/update a GitHub Release tagged to match `release.json`'s
version (e.g. `v0.1.0`), upload `Emax_FX-module.tar.gz` as a release
asset, and in Schwung Manager (`move.local:7700/modules`) choose
"Install Custom Module" and give it this repo's URL.

**Option B — direct SSH deploy (bypasses the Module Store UI):**
```sh
./scripts/build.sh
./scripts/install.sh    # scp's Emax_FX.so + module.json to ableton@move.local
```

## Build

```sh
make test          # native sanity check of the DSP core only
./scripts/build.sh  # ARM64 cross-compile via Docker -> build-arm64/Emax_FX.so
./scripts/package.sh # -> Emax_FX-module.tar.gz
./scripts/install.sh # deploy to move.local over SSH
```

## Controls (Shadow UI chain_params, see `module.json`)

| Key | Function | Range |
|---|---|---|
| `rate` | Sample rate | enum: `10kHz`, `20kHz`, `28kHz`, `42kHz` |
| `ladder_cutoff` | Ladder filter cutoff | 200–12000 Hz |
| `ladder_resonance` | Ladder filter resonance | 0–0.98 |
| `ladder_bypass` | Ladder filter bypass | enum: `Off`, `On` |

Default on startup: 10 kHz / 12-bit (the "Emaxed" lo-fi reference point),
ladder at 8000 Hz / 0.2 resonance, enabled.

## Known simplification

`create_instance()` does not currently parse the `config_json` the host
passes on load (saved `config.json` / `module.json` defaults) — it starts
from fixed defaults and relies on the host calling `set_param()` to apply
saved/UI values afterward. This is called out in `src/emax_audio_fx.c` and
hasn't been verified against real on-device Shadow UI behavior yet.
