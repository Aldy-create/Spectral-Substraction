/* fft_r2.c -- Radix-2 DIT FFT/IFFT 256-titik in-place (int32 tanpa scaling, twiddle Q15)
 *
 * Kompatibel C89 (TI C6000 CGT 7.4.x) dan PC GCC/Clang.
 * Untuk input Q15, output FFT tumbuh maksimal x256 (|Y| <= 2^23), aman di int32.
 */
#include <math.h>
#include "fft_r2.h"

#ifdef __TI_COMPILER_VERSION__
#pragma CODE_SECTION(fft_r2, ".algtext")
#pragma DATA_SECTION(tw_re, ".algdata")
#pragma DATA_SECTION(tw_im, ".algdata")
#endif

static short tw_re[FFT_N / 2];
static short tw_im[FFT_N / 2];

void fft_init(void)
{
    int k;
    for (k = 0; k < FFT_N / 2; k++) {
        double a = 2.0 * 3.14159265358979323846 * (double)k / (double)FFT_N;
        tw_re[k] = (short)floor( cos(a) * 32767.0 + 0.5);
        tw_im[k] = (short)floor(-sin(a) * 32767.0 + 0.5);
    }
}

static void bitrev(int *re, int *im)
{
    int i, j = 0, k, t;
    for (i = 0; i < FFT_N - 1; i++) {
        if (i < j) {
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
        k = FFT_N >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

/* inverse = 0: FFT maju; inverse = 1: IFFT (dibagi N = 256 di akhir) */
void fft_r2(int *re, int *im, int inverse)
{
    int stage, half, step, k, i, j;
    bitrev(re, im);

    for (stage = 1, half = 1; stage <= FFT_LOG2N; stage++, half <<= 1) {
        step = FFT_N / (half << 1);
        for (k = 0; k < half; k++) {
            int wr = (int)tw_re[k * step];
            int wi = inverse ? -(int)tw_im[k * step] : (int)tw_im[k * step];
            for (i = k; i < FFT_N; i += (half << 1)) {
                int tr, ti;
                j = i + half;
                tr = (int)(((long long)re[j] * wr - (long long)im[j] * wi) >> 15);
                ti = (int)(((long long)re[j] * wi + (long long)im[j] * wr) >> 15);
                re[j] = re[i] - tr;
                im[j] = im[i] - ti;
                re[i] = re[i] + tr;
                im[i] = im[i] + ti;
            }
        }
    }

    if (inverse) {
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(256, 256)
#endif
        for (i = 0; i < FFT_N; i++) {
            re[i] >>= FFT_LOG2N;
            im[i] >>= FFT_LOG2N;
        }
    }
}
