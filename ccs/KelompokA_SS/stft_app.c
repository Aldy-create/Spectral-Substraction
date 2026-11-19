/* stft_app.c -- Kerangka STFT (N=256, H=128, Hann periodik Q15) + OLA + SAT16
 *
 * Seluruh kode dan buffer algoritma ditempatkan ke .algtext / .algdata
 * agar memory footprint di file .map terukur terpisah dari library BSL/CSL/RTS.
 */
#include <string.h>
#include "stft_app.h"
#include "fft_r2.h"
#include "hann_win.h"

#define SAT16(x) ((x) > 32767 ? 32767 : ((x) < -32768 ? -32768 : (short)(x)))
#define Q15_MUL(a, b) SAT16((((int)(a) * (int)(b)) + (1 << 14)) >> 15)

#ifdef __TI_COMPILER_VERSION__
#pragma CODE_SECTION(stft_app_process_hop, ".algtext")
#pragma DATA_SECTION(xin, ".algdata")
#pragma DATA_SECTION(ola, ".algdata")
#pragma DATA_SECTION(Xre, ".algdata")
#pragma DATA_SECTION(Xim, ".algdata")
#pragma DATA_SECTION(g_params, ".algdata")
#pragma DATA_SECTION(g_state, ".algdata")
#endif

static short xin[NFFT];
static int   ola[NFFT];
static int   Xre[NFFT];
static int   Xim[NFFT];

static ss_params_t g_params;
static ss_state_t  g_state;

void stft_app_init(int identity_mode)
{
    fft_init();
    ss_default_params(&g_params);
    if (identity_mode) {
        /* Trik Identity Test: paksa G[k] ~ 1.0 tanpa mengubah alur program */
        g_params.beta_q15 = 32767;
    }
    stft_app_reset();
}

void stft_app_reset(void)
{
    memset(xin, 0, sizeof(xin));
    memset(ola, 0, sizeof(ola));
    memset(Xre, 0, sizeof(Xre));
    memset(Xim, 0, sizeof(Xim));
    ss_init(&g_state, &g_params);
}

void stft_app_process_hop(const short * restrict in, short * restrict out, int bypass)
{
    int n;

    if (bypass) {
        for (n = 0; n < HOP; n++) {
            out[n] = in[n];
        }
        return;
    }

    /* 1. Geser riwayat: buang 128 lama, masukkan 128 baru */
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(128, 128)
#endif
    for (n = 0; n < HOP; n++) {
        xin[n]       = xin[n + HOP];
        xin[n + HOP] = in[n];
    }

    /* 2. Window Hann Q15 + Radix-2 FFT int32 */
#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(256, 256)
#endif
    for (n = 0; n < NFFT; n++) {
        Xre[n] = (int)Q15_MUL(xin[n], HANN_WIN_Q15[n]);
        Xim[n] = 0;
    }
    fft_r2(Xre, Xim, 0);

    /* 3. Spectral Subtraction Q15 (in-place + simetri konjugat) */
    ss_process(&g_state, &g_params, Xre, Xim);

    /* 4. IFFT + Overlap-Add + Saturasi 16-bit */
    fft_r2(Xre, Xim, 1);

#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(256, 256)
#endif
    for (n = 0; n < NFFT; n++) {
        ola[n] += Xre[n];
    }

#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(128, 128)
#endif
    for (n = 0; n < HOP; n++) {
        out[n] = SAT16(ola[n]);
    }

#ifdef __TI_COMPILER_VERSION__
#pragma MUST_ITERATE(128, 128)
#endif
    for (n = 0; n < HOP; n++) {
        ola[n]       = ola[n + HOP];
        ola[n + HOP] = 0;
    }
}

void stft_app_process_hop_prof(
    const short * restrict in,
    short * restrict out,
    stft_tick_fn get_tick,
    unsigned int *cyc_win,
    unsigned int *cyc_fft,
    unsigned int *cyc_ss,
    unsigned int *cyc_ifft,
    unsigned int *cyc_ola)
{
    int n;
    unsigned int t0, t1, t2, t3, t4, t5;

    t0 = get_tick();
    for (n = 0; n < HOP; n++) {
        xin[n]       = xin[n + HOP];
        xin[n + HOP] = in[n];
    }
    for (n = 0; n < NFFT; n++) {
        Xre[n] = (int)Q15_MUL(xin[n], HANN_WIN_Q15[n]);
        Xim[n] = 0;
    }
    t1 = get_tick();

    fft_r2(Xre, Xim, 0);
    t2 = get_tick();

    ss_process(&g_state, &g_params, Xre, Xim);
    t3 = get_tick();

    fft_r2(Xre, Xim, 1);
    t4 = get_tick();

    for (n = 0; n < NFFT; n++) {
        ola[n] += Xre[n];
    }
    for (n = 0; n < HOP; n++) {
        out[n] = SAT16(ola[n]);
    }
    for (n = 0; n < HOP; n++) {
        ola[n]       = ola[n + HOP];
        ola[n + HOP] = 0;
    }
    t5 = get_tick();

    *cyc_win  = (t1 - t0);
    *cyc_fft  = (t2 - t1);
    *cyc_ss   = (t3 - t2);
    *cyc_ifft = (t4 - t3);
    *cyc_ola  = (t5 - t4);
}

const ss_state_t *stft_app_state(void)
{
    return &g_state;
}

ss_params_t *stft_app_params(void)
{
    return &g_params;
}
