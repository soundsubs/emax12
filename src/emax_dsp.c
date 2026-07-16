#include "emax_dsp.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Biquad (RBJ cookbook resonant lowpass), bilinear per stage ---- */

static void biquad_design_lowpass(EmaxBiquad *bq, double cutoff_hz, double sample_rate, double Q) {
    double w0 = 2.0 * M_PI * cutoff_hz / sample_rate;
    double cosw0 = cos(w0);
    double sinw0 = sin(w0);
    double alpha = sinw0 / (2.0 * Q);

    double b0 = (1.0 - cosw0) / 2.0;
    double b1 = 1.0 - cosw0;
    double b2 = (1.0 - cosw0) / 2.0;
    double a0 = 1.0 + alpha;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
    bq->z1 = 0.0;
    bq->z2 = 0.0;
}

static double biquad_process(EmaxBiquad *bq, double x) {
    /* Transposed direct form II */
    double y = bq->b0 * x + bq->z1;
    bq->z1 = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2 = bq->b2 * x - bq->a2 * y;
    return y;
}

static void one_pole_design(EmaxOnePole *op, double cutoff_hz, double sample_rate) {
    /* Exponential-decay one-pole lowpass, matched at cutoff via standard
     * -3dB approximation: a = 1 - exp(-2*pi*fc/fs) */
    op->a = 1.0 - exp(-2.0 * M_PI * cutoff_hz / sample_rate);
    op->z = 0.0;
}

static double one_pole_process(EmaxOnePole *op, double x) {
    op->z += op->a * (x - op->z);
    return op->z;
}

/* ---- N-th order Butterworth cascade design ----
 * Analog prototype poles (normalized, cutoff = 1 rad/s):
 *   s_k = exp(j * pi * (2k + n - 1) / (2n)),  k = 1..n
 * Complex-conjugate pairs give 2nd-order sections with
 *   Q_k = 1 / (2 * |cos(theta_k)|),  theta_k = pi*(2k+n-1)/(2n)
 * For odd n, one real pole (theta = pi) becomes a one-pole section.
 * We only need k = 1 .. ceil(n/2) since poles pair up symmetrically. */

void emax_butterworth_design(EmaxButterworth *f, int order, double cutoff_hz, double sample_rate) {
    memset(f, 0, sizeof(*f));
    if (order < 1) order = 1;
    if (order > 9) order = 9;

    int pairs = order / 2;
    int odd = order % 2;

    for (int k = 1; k <= pairs; k++) {
        double theta = M_PI * (2.0 * k + order - 1.0) / (2.0 * order);
        double Q = 1.0 / (2.0 * fabs(cos(theta)));
        if (f->num_biquads < EMAX_MAX_BIQUADS) {
            biquad_design_lowpass(&f->biquad[f->num_biquads], cutoff_hz, sample_rate, Q);
            f->num_biquads++;
        }
    }
    if (odd) {
        one_pole_design(&f->one_pole, cutoff_hz, sample_rate);
        f->has_one_pole = 1;
    }
}

double emax_butterworth_process(EmaxButterworth *f, double x) {
    double y = x;
    for (int i = 0; i < f->num_biquads; i++) {
        y = biquad_process(&f->biquad[i], y);
    }
    if (f->has_one_pole) {
        y = one_pole_process(&f->one_pole, y);
    }
    return y;
}

void emax_butterworth_reset(EmaxButterworth *f) {
    for (int i = 0; i < f->num_biquads; i++) {
        f->biquad[i].z1 = 0.0;
        f->biquad[i].z2 = 0.0;
    }
    f->one_pole.z = 0.0;
}

/* ---- 12-bit linear quantizer, no dither ---- */

double emax_quantize(double x, int bits) {
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    double levels = (double)(1 << (bits - 1)); /* signed, e.g. 2048 for 12-bit */
    double step = 1.0 / levels;
    double q = floor(x / step + 0.5) * step;
    if (q > 1.0 - step) q = 1.0 - step;
    if (q < -1.0) q = -1.0;
    return q;
}

/* ---- Sample-and-hold decimator ---- */

void emax_decimator_reset(EmaxDecimator *d) {
    d->phase = 1.0; /* force a capture on the first call */
    d->held_value = 0.0;
}

double emax_decimator_process(EmaxDecimator *d, double new_value, double host_rate, double target_rate) {
    double step = target_rate / host_rate; /* fraction of one target-period per host sample */
    d->phase += step;
    if (d->phase >= 1.0) {
        d->phase -= floor(d->phase);
        d->held_value = new_value;
    }
    return d->held_value;
}

/* ---- SSM2047-style 4-pole saturating ladder (structural analog) ---- */

void emax_ladder_init(EmaxLadder *lf, double sample_rate) {
    memset(lf, 0, sizeof(*lf));
    lf->sample_rate = sample_rate;
    lf->cutoff_hz = 4000.0;
    lf->resonance = 0.0;
}

void emax_ladder_set(EmaxLadder *lf, double cutoff_hz, double resonance) {
    if (cutoff_hz < 20.0) cutoff_hz = 20.0;
    if (cutoff_hz > 0.45 * lf->sample_rate) cutoff_hz = 0.45 * lf->sample_rate;
    if (resonance < 0.0) resonance = 0.0;
    if (resonance > 0.98) resonance = 0.98; /* keep just under self-oscillation */
    lf->cutoff_hz = cutoff_hz;
    lf->resonance = resonance;
}

double emax_ladder_process(EmaxLadder *lf, double x) {
    /* Zero-delay-feedback-flavored ladder: each stage is a one-pole
     * lowpass with tanh saturation, four stages in series, resonance fed
     * back from stage 4 output to the input (classic Moog-style
     * structure). Not a literal SSM2047 model -- E-mu never published
     * the chip's transconductance curve -- but the same 24 dB/oct
     * saturating-ladder character. */
    double g = 1.0 - exp(-2.0 * M_PI * lf->cutoff_hz / lf->sample_rate);
    double fb = lf->resonance * 4.0;

    double input = x - fb * lf->stage[3];
    input = tanh(input);

    for (int i = 0; i < 4; i++) {
        double in = (i == 0) ? input : tanh(lf->stage[i - 1]);
        lf->stage[i] += g * (in - lf->stage[i]);
    }
    return lf->stage[3];
}

void emax_ladder_reset(EmaxLadder *lf) {
    lf->stage[0] = lf->stage[1] = lf->stage[2] = lf->stage[3] = 0.0;
}

/* ---- Top-level voice ---- */

void emax_voice_init(EmaxVoice *v, double host_sample_rate) {
    memset(v, 0, sizeof(*v));
    v->host_sample_rate = host_sample_rate;
    v->target_sample_rate = EMAX_RATE_20K;
    v->bit_depth = 12;
    v->aa_cutoff_ratio = 0.45;
    v->aa_order = 7;
    v->recon_cutoff_ratio = 0.50;
    v->recon_order = 3;
    v->ladder_enabled = 1;
    v->ladder_cutoff_hz = 6000.0;
    v->ladder_resonance = 0.15;

    emax_ladder_init(&v->ladder, host_sample_rate);
    emax_ladder_set(&v->ladder, v->ladder_cutoff_hz, v->ladder_resonance);
    emax_voice_set_rate(v, v->target_sample_rate);
}

void emax_voice_set_rate(EmaxVoice *v, double target_sample_rate) {
    v->target_sample_rate = target_sample_rate;
    double aa_cutoff = v->aa_cutoff_ratio * target_sample_rate;
    double recon_cutoff = v->recon_cutoff_ratio * target_sample_rate;
    /* Clamp cutoffs below Nyquist of the host rate to keep the bilinear
     * transform stable regardless of target rate vs host rate. */
    double nyq = 0.49 * v->host_sample_rate;
    if (aa_cutoff > nyq) aa_cutoff = nyq;
    if (recon_cutoff > nyq) recon_cutoff = nyq;

    emax_butterworth_design(&v->aa_filter, v->aa_order, aa_cutoff, v->host_sample_rate);
    emax_butterworth_design(&v->recon_filter, v->recon_order, recon_cutoff, v->host_sample_rate);
    emax_decimator_reset(&v->decimator);
}

void emax_voice_reset(EmaxVoice *v) {
    emax_butterworth_reset(&v->aa_filter);
    emax_butterworth_reset(&v->recon_filter);
    emax_decimator_reset(&v->decimator);
    emax_ladder_reset(&v->ladder);
}

double emax_voice_process(EmaxVoice *v, double x) {
    /* 1. Band-limit before decimation, like the Emax's 7-pole AA filter
     *    ahead of its ADC. */
    double aa = emax_butterworth_process(&v->aa_filter, x);

    /* 2. Sample-and-hold decimate to the target rate -- this is where
     *    the actual "sampling" happens and where stairstep aliasing is
     *    introduced. */
    double held = emax_decimator_process(&v->decimator, aa, v->host_sample_rate, v->target_sample_rate);

    /* 3. 12-bit linear quantize, no dither. */
    double q = emax_quantize(held, v->bit_depth);

    /* 4. Reconstruction filter, like the analog stage smoothing the
     *    DAC's stairsteps back out. */
    double recon = emax_butterworth_process(&v->recon_filter, q);

    /* 5. Optional SSM2047-style resonant ladder (the "Emax filter"
     *    character people usually mean when they say the machine
     *    sounds "Emaxed"). */
    if (v->ladder_enabled) {
        recon = emax_ladder_process(&v->ladder, recon);
    }
    return recon;
}
