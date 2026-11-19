/* ss.c -- Implementasi Q15 Spectral Subtraction untuk TMS320C6416T DSK
 *
 * Fitur optimasi TI C6000:
 *  - #pragma CODE_SECTION(ss_process, ".algtext")
 *  - #pragma DATA_SECTION(SQRT_Q15_LUT, ".algdata")
 *  - #pragma MUST_ITERATE untuk software pipelining pada loop 129-bin
 *  - Pointer restrict untuk menghindari alias memori
 *  - Opsi Fast SQRT LUT 257-entri + interpolasi linear (USE_FAST_SQRT_LUT = 1)
 */
#include <string.h>
#include "ss.h"

#ifdef __TI_COMPILER_VERSION__
#pragma CODE_SECTION(ss_process, ".algtext")
#endif

#if USE_FAST_SQRT_LUT
#ifdef __TI_COMPILER_VERSION__
#pragma DATA_SECTION(SQRT_Q15_LUT, ".algdata")
#endif
static short SQRT_Q15_LUT[257];
static int   lut_initialized = 0;
#endif

/* Akar kuadrat integer 32-bit bit-by-bit (dipakai saat init LUT atau fallback) */
static unsigned int isqrt32(unsigned int x)
{
    unsigned int r = 0;
    unsigned int b = 1u << 30;

    while (b > x) {
        b >>= 2;
    }
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

#if USE_FAST_SQRT_LUT
/* Fast SQRT Q15: input g2_q15 in [0, 32767], output sqrt(g2) in Q15 [0, 32767].
 * LUT berisi 257 entri pada titik i * 128 (i = 0..256) dengan interpolasi linear.
 */
static short fast_sqrt_q15(int g2_q15)
{
    int idx;
    int frac;
    int y0;
    int y1;

    if (g2_q15 <= 0) {
        return 0;
    }
    if (g2_q15 >= 32767) {
        return 32767;
    }

    idx  = g2_q15 >> 7;   /* 0..255 */
    frac = g2_q15 & 127;  /* 0..127 */
    y0   = (int)SQRT_Q15_LUT[idx];
    y1   = (int)SQRT_Q15_LUT[idx + 1];
    return (short)(y0 + (((y1 - y0) * frac) >> 7));
}
#endif

void ss_default_params(ss_params_t *p)
{
    p->alpha_q12   = 8192; /* alpha = 2.0 (Q12)                   */
    p->beta_q15    = 655;  /* beta  = 0.02 (Q15)                  */
    p->lam_shift   = 4;    /* lambda = 1 - 2^-4 = 0.9375          */
    p->vad_thr_q8  = 512;  /* ambang VAD 3 dB (rasio 2.0, Q8)     */
    p->n_init      = 6;    /* rata-rata 6 frame awal sebagai noise */
    p->pow_shift   = 8;    /* geser kanan re/im cegah overflow    */
    p->use_minstat = 0;    /* default 0 = VAD (identik F-PY)      */
}

void ss_init(ss_state_t *st, const ss_params_t *p)
{
    (void)p;
    memset(st, 0, sizeof(ss_state_t));

#if USE_FAST_SQRT_LUT
    if (!lut_initialized) {
        int i;
        for (i = 0; i <= 256; i++) {
            unsigned int val = (unsigned int)(i * 128) << 15;
            unsigned int s   = isqrt32(val);
            SQRT_Q15_LUT[i]  = (s > 32767u) ? 32767 : (short)s;
        }
        lut_initialized = 1;
    }
#endif
}

void ss_process(ss_state_t * restrict st, const ss_params_t * restrict p,
                int * restrict re, int * restrict im)
{
    int k;
    unsigned long long sumY = 0ULL;
    unsigned long long sumN = 0ULL;
    const int pow_shift = p->pow_shift;
    const int lam_shift = p->lam_shift;
    const short alpha_q12 = p->alpha_q12;
    const short beta_q15  = p->beta_q15;

    /* 1. Hitung spektrum daya |Y|^2 (digeser pow_shift agar tidak overflow 32-bit) */
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
    for (k = 0; k < NBIN; k++) {
        int r = re[k] >> pow_shift;
        int i = im[k] >> pow_shift;
        unsigned int ur = (unsigned int)(r < 0 ? -r : r);
        unsigned int ui = (unsigned int)(i < 0 ? -i : i);
        unsigned int py = ur * ur + ui * ui;

        st->PY[k] = py;
        sumY += (unsigned long long)py;
        sumN += (unsigned long long)st->PN[k];
    }

    /* 2. Estimasi noise: Minimum Statistics (Q-LIVE) atau VAD + avg (Q-DIG) */
    if (p->use_minstat) {
        int u;
        if (st->frame_count == 0) {
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
            for (k = 0; k < NBIN; k++) {
                unsigned int py = st->PY[k];
                st->P_smooth[k] = py;
                st->P_curmin[k] = py;
                st->PN[k]       = py;
                for (u = 0; u < MS_U; u++) {
                    st->P_submin[u][k] = py;
                }
            }
            st->ms_sub_cnt = 1;
            st->ms_sub_idx = 0;
        } else {
            int advance_sub = (st->ms_sub_cnt >= MS_V) ? 1 : 0;
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
            for (k = 0; k < NBIN; k++) {
                long long diff = (long long)st->PY[k] - (long long)st->P_smooth[k];
                unsigned int ps = (unsigned int)((long long)st->P_smooth[k] + (diff >> 3)); /* alpha_s=0.875 */
                unsigned int mval;

                st->P_smooth[k] = ps;
                if (ps < st->P_curmin[k]) {
                    st->P_curmin[k] = ps;
                }
                mval = st->P_curmin[k];
                for (u = 0; u < MS_U; u++) {
                    if (st->P_submin[u][k] < mval) {
                        mval = st->P_submin[u][k];
                    }
                }
                /* Koreksi bias B_min ~ 1.8 (7373 dalam Q12) */
                st->PN[k] = (unsigned int)(((unsigned long long)mval * 7373ULL) >> 12);

                if (advance_sub) {
                    st->P_submin[st->ms_sub_idx][k] = st->P_curmin[k];
                    st->P_curmin[k] = ps;
                }
            }
            if (advance_sub) {
                st->ms_sub_cnt = 1;
                st->ms_sub_idx = (st->ms_sub_idx + 1) % MS_U;
            } else {
                st->ms_sub_cnt++;
            }
        }
        st->vad_speech = ((sumY << 8) > (sumN * (unsigned long long)p->vad_thr_q8)) ? 1 : 0;
    } else if (st->frame_count < p->n_init) {
        int denom = st->frame_count + 1;
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
        for (k = 0; k < NBIN; k++) {
            if (st->frame_count == 0) {
                st->PN[k] = st->PY[k];
            } else {
                long long diff = (long long)st->PY[k] - (long long)st->PN[k];
                st->PN[k] = (unsigned int)((long long)st->PN[k] + (diff / denom));
            }
        }
        st->vad_speech = 0;
    } else {
        st->vad_speech = ((sumY << 8) > (sumN * (unsigned long long)p->vad_thr_q8)) ? 1 : 0;
        if (!st->vad_speech) {
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
            for (k = 0; k < NBIN; k++) {
                /* Update rekursif shift-only: PN += (PY - PN) * 2^-lam_shift */
                long long diff = (long long)st->PY[k] - (long long)st->PN[k];
                st->PN[k] = (unsigned int)((long long)st->PN[k] + (diff >> lam_shift));
            }
        }
    }
    st->frame_count++;

    /* 3. Hitung Gain: G^2 = max(1 - alpha * PN / PY, beta), lalu G = sqrt(G^2) */
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
    for (k = 0; k < NBIN; k++) {
        unsigned int py = st->PY[k];
        unsigned long long aPN = ((unsigned long long)st->PN[k] * (unsigned int)alpha_q12) >> 12;
        int g2;

        if (py == 0u || aPN >= (unsigned long long)py) {
            g2 = (int)beta_q15;
        } else {
            g2 = 32767 - (int)((aPN << 15) / (unsigned long long)py);
            if (g2 < (int)beta_q15) {
                g2 = (int)beta_q15;
            }
        }

#if USE_FAST_SQRT_LUT
        st->G[k] = fast_sqrt_q15(g2);
#else
        {
            unsigned int s = isqrt32((unsigned int)g2 << 15);
            st->G[k] = (s > 32767u) ? 32767 : (short)s;
        }
#endif
    }

    /* 4. Terapkan gain real Q15 (fase noisy dipertahankan) */
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(129, 129)
#endif
    for (k = 0; k < NBIN; k++) {
        short g = st->G[k];
        re[k] = (int)(((long long)re[k] * g) >> 15);
        im[k] = (int)(((long long)im[k] * g) >> 15);
    }

    /* 5. Jaga simetri konjugat manual untuk IFFT kompleks 256-titik */
    im[0]        = 0;
    im[NFFT / 2] = 0;
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(127, 127)
#endif
    for (k = 1; k < NFFT / 2; k++) {
        re[NFFT - k] =  re[k];
        im[NFFT - k] = -im[k];
    }
}
