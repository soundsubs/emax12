#ifndef EMAX_DSP_H
#define EMAX_DSP_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * emax_dsp: signal chain modeling the E-mu Emax 12-bit sampling engine.
 *
 * Verified reference specs (E-mu Systems Emax / Emax HD / SE, 1986):
 *   - 12-bit linear ADC, discrete (non-oversampled) converter
 *   - 7-pole analog anti-aliasing filter ahead of the ADC
 *   - Variable sample rate, ~10 kHz to 42 kHz
 *   - 8x SSM2047 analog 4-pole (24 dB/oct) resonant VCF/VCA chips
 *   - Analog Devices AD6012 DACs feeding an analog reconstruction filter
 *
 * What is NOT publicly documented (no schematic-level source found):
 *   - The exact pole/Q alignment of the 7-pole AA filter (Butterworth vs.
 *     Chebyshev vs. a custom alignment)
 *   - The exact reconstruction filter order/alignment on the output side
 *   - SSM2047's transistor-level nonlinearity curve
 *
 * This module fills those gaps with well-established, principled choices
 * (maximally-flat Butterworth cascades, a saturating 4-pole ladder for the
 * VCF) called out explicitly below. If you have Emax service-manual
 * schematics, the filter design functions here are structured so you can
 * drop in exact pole locations instead.
 * ---------------------------------------------------------------------- */

typedef struct {
    double b0, b1, b2, a1, a2; /* direct form I, a0 normalized to 1 */
    double z1, z2;             /* state */
} EmaxBiquad;

typedef struct {
    double a;   /* one-pole coefficient */
    double z;   /* state */
} EmaxOnePole;

/* Cascade of up to 4 biquads + an optional trailing one-pole section,
 * enough to build any odd/even Butterworth order up to 9. */
#define EMAX_MAX_BIQUADS 4
typedef struct {
    EmaxBiquad biquad[EMAX_MAX_BIQUADS];
    int num_biquads;
    EmaxOnePole one_pole;
    int has_one_pole;
} EmaxButterworth;

/* Design an N-th order Butterworth lowpass at cutoff_hz for sample_rate.
 * order: 1-9. Uses the standard analog-prototype pole-angle formula,
 * combined into RBJ-cookbook resonant biquads per conjugate pole pair,
 * bilinear-transformed per stage; odd order gets one real pole as a
 * one-pole section. This is the "principled approximation" referenced
 * above — swap in exact coefficients here if you have the real filter
 * topology. */
void emax_butterworth_design(EmaxButterworth *f, int order, double cutoff_hz, double sample_rate);
double emax_butterworth_process(EmaxButterworth *f, double x);
void emax_butterworth_reset(EmaxButterworth *f);

/* 12-bit linear quantizer, no dither (matches the Emax: a real discrete
 * ADC/DAC, not an oversampled/dithered converter). bits is exposed as a
 * parameter in case you want to A/B against other bit depths. */
double emax_quantize(double x, int bits);

/* Sample-and-hold decimator: holds the (filtered, quantized) input value
 * for host_rate/target_rate host samples before re-sampling. This is what
 * produces authentic stairstep aliasing rather than a clean resample. */
typedef struct {
    double phase;
    double held_value;
} EmaxDecimator;

void emax_decimator_reset(EmaxDecimator *d);
/* Call once per host sample. new_value is the freshly-quantized sample to
 * potentially latch. Returns the held output for this host sample. */
double emax_decimator_process(EmaxDecimator *d, double new_value, double host_rate, double target_rate);

/* SSM2047-style 4-pole (24 dB/oct) resonant ladder filter with per-stage
 * tanh saturation, modeled after the classic transistor-ladder topology
 * (Stilson/Smith-style zero-delay-feedback structure). This is a
 * structural analog, not a transistor-level SSM2047 model -- E-mu never
 * published the chip's transconductance curve. cutoff_hz in [20, 18000],
 * resonance in [0, 1) (self-oscillation begins near 1.0). */
typedef struct {
    double stage[4];
    double cutoff_hz;
    double resonance;
    double sample_rate;
} EmaxLadder;

void emax_ladder_init(EmaxLadder *lf, double sample_rate);
void emax_ladder_set(EmaxLadder *lf, double cutoff_hz, double resonance);
double emax_ladder_process(EmaxLadder *lf, double x);
void emax_ladder_reset(EmaxLadder *lf);

/* ------------------------------------------------------------------- */
/* Top-level voice: full Emax signal chain for one channel.            */
/* ------------------------------------------------------------------- */

typedef struct {
    double host_sample_rate;
    double target_sample_rate; /* 10000, 20000, 28000, 42000, or custom */
    int    bit_depth;          /* 12 */
    double aa_cutoff_ratio;    /* AA filter cutoff = ratio * target_rate, default 0.45 */
    int    aa_order;           /* default 7 */
    double recon_cutoff_ratio; /* reconstruction filter cutoff ratio, default 0.5 */
    int    recon_order;        /* default 3 */
    int    ladder_enabled;
    double ladder_cutoff_hz;
    double ladder_resonance;

    EmaxButterworth aa_filter;
    EmaxButterworth recon_filter;
    EmaxDecimator   decimator;
    EmaxLadder      ladder;
} EmaxVoice;

/* Presets matching the four rates you asked about. */
typedef enum {
    EMAX_RATE_10K = 10000,
    EMAX_RATE_20K = 20000,
    EMAX_RATE_28K = 28000,
    EMAX_RATE_42K = 42000
} EmaxRatePreset;

void emax_voice_init(EmaxVoice *v, double host_sample_rate);
void emax_voice_set_rate(EmaxVoice *v, double target_sample_rate);
void emax_voice_reset(EmaxVoice *v);
double emax_voice_process(EmaxVoice *v, double x);

#endif /* EMAX_DSP_H */
