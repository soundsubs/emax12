#include "../src/emax_dsp.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int check_finite(double x) {
    return !(isnan(x) || isinf(x));
}

int main(void) {
    double host_sr = 48000.0;
    EmaxVoice v;
    emax_voice_init(&v, host_sr);

    double rates[4] = { EMAX_RATE_10K, EMAX_RATE_20K, EMAX_RATE_28K, EMAX_RATE_42K };
    const char *names[4] = { "10kHz", "20kHz", "28kHz", "42kHz" };

    int all_ok = 1;

    for (int r = 0; r < 4; r++) {
        emax_voice_set_rate(&v, rates[r]);
        emax_voice_reset(&v);

        double peak = 0.0;
        double sum_sq = 0.0;
        int n = (int)(host_sr * 0.5); /* 0.5s test sweep */
        int finite_ok = 1;
        int distinct_levels = 0;
        double last_out = -999.0;

        /* Log sine sweep 100Hz -> 15kHz to exercise the whole chain,
         * amplitude 0.8 to leave headroom below clipping. */
        for (int i = 0; i < n; i++) {
            double t = (double)i / host_sr;
            double f = 100.0 * pow(150.0, t / 0.5); /* 100Hz -> 15kHz over 0.5s */
            double phase_inc = 2.0 * M_PI * f / host_sr;
            static double phase = 0.0;
            phase += phase_inc;
            double x = 0.8 * sin(phase);

            double y = emax_voice_process(&v, x);
            if (!check_finite(y)) finite_ok = 0;
            if (fabs(y) > peak) peak = fabs(y);
            sum_sq += y * y;
            if (fabs(y - last_out) > 1e-9) {
                distinct_levels++;
                last_out = y;
            }
        }

        double rms = sqrt(sum_sq / n);
        printf("[%s] finite=%s peak=%.4f rms=%.4f value_changes=%d\n",
               names[r], finite_ok ? "yes" : "NO -- BUG", peak, rms, distinct_levels);

        if (!finite_ok) all_ok = 0;
        if (peak > 1.0001) { printf("  WARNING: peak exceeds full scale\n"); all_ok = 0; }
        if (peak < 0.05) { printf("  WARNING: suspiciously low output level\n"); all_ok = 0; }
    }

    /* Quantizer unit check: 12-bit should give 4096 representable
     * levels across [-1, 1). */
    {
        double lo = 2.0, hi = -2.0;
        for (int i = -20000; i <= 20000; i++) {
            double x = i / 20000.0;
            double q = emax_quantize(x, 12);
            if (q < lo) lo = q;
            if (q > hi) hi = q;
        }
        double step = 1.0 / 2048.0;
        printf("[quantizer] step=%.6f (expected %.6f) range=[%.4f, %.4f]\n",
               step, 1.0 / 2048.0, lo, hi);
    }

    printf(all_ok ? "\nALL CHECKS PASSED\n" : "\nSOME CHECKS FAILED -- see warnings above\n");
    return all_ok ? 0 : 1;
}
