/* ss.h -- Spectral Subtraction Q15 (Kelompok A - TMS320C6416) */
#ifndef SS_H
#define SS_H

#define NFFT 256
#define NBIN 129 /* Bin unik 0..NFFT/2 */

/* Set 1 untuk mengganti isqrt32 dengan LUT 257-entri + interpolasi linear */
#ifndef USE_FAST_SQRT_LUT
#define USE_FAST_SQRT_LUT 1
#endif

/* Kompatibilitas keyword restrict untuk TI C6000 & C99/C11 vs C89 murni */
#if !defined(__TI_COMPILER_VERSION__) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
#ifndef restrict
#define restrict
#endif
#endif

typedef struct {
    short alpha_q12;  /* over-subtraction alpha, Q12 (default 2.0 -> 8192)    */
    short beta_q15;   /* spectral floor beta (pada G^2), Q15 (default -> 655) */
    int   lam_shift;  /* lambda = 1 - 2^-lam_shift (4 -> 0.9375)              */
    int   vad_thr_q8; /* ambang VAD Q8 (3 dB ~ 2.0 -> 512)                    */
    int   n_init;     /* jumlah frame awal untuk inisialisasi noise (6)       */
    int   pow_shift;  /* geser kanan re/im sebelum kuadrat (default: 8)       */
    int   use_minstat;/* 0 = VAD + recursive avg (default Q-DIG), 1 = MinStat */
} ss_params_t;

#define MS_U 8        /* jumlah sub-window Minimum Statistics                 */
#define MS_V 8        /* frame per sub-window (8*8 = 64 frame ~ 1.02 detik)   */

typedef struct {
    unsigned int PN[NBIN];          /* estimasi daya noise (skala pow_shift) */
    unsigned int PY[NBIN];          /* daya spektrum noisy frame saat ini    */
    short        G[NBIN];           /* gain real Q15 in [sqrt(beta), 1)      */
    int          frame_count;
    int          vad_speech;        /* 1 = speech, 0 = noise                 */
    /* State untuk opsi Minimum Statistics (Bagian 2.3.2) */
    unsigned int P_smooth[NBIN];
    unsigned int P_curmin[NBIN];
    unsigned int P_submin[MS_U][NBIN];
    int          ms_sub_cnt;
    int          ms_sub_idx;
} ss_state_t;

void ss_default_params(ss_params_t *p);
void ss_init(ss_state_t *st, const ss_params_t *p);
void ss_process(ss_state_t * restrict st, const ss_params_t * restrict p,
                int * restrict re, int * restrict im);

#endif /* SS_H */
