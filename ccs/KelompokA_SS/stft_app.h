/* stft_app.h -- Kerangka STFT + OLA Q15 Kelompok A untuk TMS320C6416T DSK */
#ifndef STFT_APP_H
#define STFT_APP_H

#include "ss.h"

#define HOP 128

void stft_app_init(int identity_mode);
void stft_app_reset(void);

/* Pemrosesan real-time / offline standar per hop (128 sampel).
 * bypass = 1 -> lewati enhancement (untuk demo perbandingan bypass vs enhanced)
 */
void stft_app_process_hop(const short * restrict in, short * restrict out, int bypass);

/* Versi profiling per blok (untuk mengisi Tabel 6 laporan di mode Q-DIG).
 * get_tick() harus mengembalikan counter monotonik (mis. TIMER_getCount(hTmr)).
 */
typedef unsigned int (*stft_tick_fn)(void);
void stft_app_process_hop_prof(
    const short * restrict in,
    short * restrict out,
    stft_tick_fn get_tick,
    unsigned int *cyc_win,
    unsigned int *cyc_fft,
    unsigned int *cyc_ss,
    unsigned int *cyc_ifft,
    unsigned int *cyc_ola
);

const ss_state_t *stft_app_state(void);
ss_params_t *stft_app_params(void);

#endif /* STFT_APP_H */
