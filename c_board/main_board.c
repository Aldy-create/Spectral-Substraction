/* main_board.c -- Entry Point CCS 6.2 untuk TMS320C6416T DSK (Kelompok A)
 *
 * Mode LAB_MODE_QDIG (1):
 *   - IDENTITY_TEST = 1 : paksa beta_q15 = 32767 (STFT Identity Test)
 *   - QDIG_USE_CIO  = 0 : uji buffer sintetis internal + profiling cycles
 *   - QDIG_USE_CIO  = 1 & QDIG_BATCH = 0 : 1 file (QDIG_IN_FILE -> QDIG_OUT_FILE)
 *   - QDIG_USE_CIO  = 1 & QDIG_BATCH = 1 : loop otomatis clip_01..clip_30 via CIO
 *
 * Mode LAB_MODE_QLIVE (2):
 *   - Real-time 8 kHz LINE IN -> LINE OUT (L & R) via McBSP2 Interrupt + Ping-Pong
 *   - SW0 DOWN = Bypass (LED1) | SW0 UP = Enhanced MinStat Q15 (LED0)
 *   - SW1 DOWN = Reset estimator noise | LED3 = indikator overrun
 */
#ifndef CHIP_6416
#define CHIP_6416 1
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <csl.h>
#include <csl_irq.h>
#include <csl_mcbsp.h>
#include <csl_timer.h>
#include "dsk6416.h"
#include "dsk6416_aic23.h"
#include "dsk6416_led.h"
#include "dsk6416_dip.h"
#include "stft_app.h"

/* ======================= PILIHAN KONFIGURASI LAB ======================= */
#define LAB_MODE_QDIG   1
#define LAB_MODE_QLIVE  2

#define LAB_MODE        LAB_MODE_QDIG   /* 1 = Q-DIG, 2 = Q-LIVE               */
#define IDENTITY_TEST   0               /* 1 = Identity Test (G~1), 0 = SS Q15 */
#define QDIG_USE_CIO    1               /* 1 = baca/tulis file .raw via CIO    */
#define QDIG_BATCH      1               /* 1 = proses otomatis 30 klip         */
#define QDIG_NCLIPS     30

#define QDIG_IN_DIR     "C:/Aldy Project/Spectral Substraction/dsp_kelompok_a/data/raw_in"
#define QDIG_OUT_DIR    "C:/Aldy Project/Spectral Substraction/dsp_kelompok_a/results/Q-DIG_raw"
#define QDIG_IN_FILE    "clip_in.raw"
#define QDIG_OUT_FILE   "clip_out.raw"
/* ======================================================================= */

#define QDIG_MAXLEN     (8000 * 12)     /* Maks 12 detik @ 8 kHz di SDRAM */

#if LAB_MODE == LAB_MODE_QDIG

/* Stub agar vectors_c6416.asm tetap ter-link bersih saat mode Q-DIG */
interrupt void c_int11(void)
{
}

#pragma DATA_SECTION(clipIn, ".extdata")
#pragma DATA_SECTION(clipOut, ".extdata")
static short clipIn[QDIG_MAXLEN + HOP];
static short clipOut[QDIG_MAXLEN + HOP];

static TIMER_Handle hTmr;

static unsigned int get_timer_tick(void)
{
    return (unsigned int)TIMER_getCount(hTmr);
}

static void prof_timer_init(void)
{
    hTmr = TIMER_open(TIMER_DEV1, TIMER_OPEN_RESET);
    TIMER_configArgs(hTmr, 0x000002C0u, 0xFFFFFFFFu, 0x00000000u); /* internal clk (CPU/8) */
    TIMER_start(hTmr);
}

static int fill_synth_buffer(short *buf, int len)
{
    int n;
    unsigned int lfsr = 0x12345678u;
    for (n = 0; n < len; n++) {
        int noise;
        double sig = 0.0;
        lfsr = lfsr * 1664525u + 1013904223u;
        noise = ((int)(lfsr >> 16) - 32768) >> 5;
        if (n >= 2000 && n < 10000) {
            sig = 8000.0 * sin(2.0 * 3.14159265358979 * 440.0 * n / 8000.0);
        }
        buf[n] = (short)((int)sig + noise);
    }
    return len;
}

static int process_one_buffer(
    int len,
    unsigned long long *sum_win, unsigned int *max_win,
    unsigned long long *sum_fft, unsigned int *max_fft,
    unsigned long long *sum_ss,  unsigned int *max_ss,
    unsigned long long *sum_ifft,unsigned int *max_ifft,
    unsigned long long *sum_ola, unsigned int *max_ola,
    unsigned long long *sum_tot, unsigned int *max_tot)
{
    int k;
    int hops = 0;
    int total_len;

    /* Pad 1 hop nol di akhir untuk flush delay OLA (128 sampel) */
    memset(&clipIn[len], 0, (size_t)HOP * sizeof(short));
    total_len = ((len + HOP - 1) / HOP + 1) * HOP;

    stft_app_reset();
    for (k = 0; k + HOP <= total_len; k += HOP) {
        unsigned int tw, tf, ts, ti, to, tt;
        stft_app_process_hop_prof(
            &clipIn[k], &clipOut[k], get_timer_tick,
            &tw, &tf, &ts, &ti, &to
        );
        tw *= 8u; tf *= 8u; ts *= 8u; ti *= 8u; to *= 8u;
        tt = tw + tf + ts + ti + to;

        *sum_win  += tw; if (tw > *max_win)  *max_win  = tw;
        *sum_fft  += tf; if (tf > *max_fft)  *max_fft  = tf;
        *sum_ss   += ts; if (ts > *max_ss)   *max_ss   = ts;
        *sum_ifft += ti; if (ti > *max_ifft) *max_ifft = ti;
        *sum_ola  += to; if (to > *max_ola)  *max_ola  = to;
        *sum_tot  += tt; if (tt > *max_tot)  *max_tot  = tt;
        hops++;
    }
    return hops;
}

void main(void)
{
    int total_hops = 0;
    unsigned long long sum_win = 0, sum_fft = 0, sum_ss = 0, sum_ifft = 0, sum_ola = 0, sum_tot = 0;
    unsigned int max_win = 0, max_fft = 0, max_ss = 0, max_ifft = 0, max_ola = 0, max_tot = 0;

    DSK6416_init();
    DSK6416_LED_init();
    prof_timer_init();
    stft_app_init(IDENTITY_TEST);

    DSK6416_LED_on(0);

#if QDIG_USE_CIO && QDIG_BATCH
    {
        int idx;
        char fin_path[256];
        char fout_path[256];

        for (idx = 1; idx <= QDIG_NCLIPS; idx++) {
            FILE *fi, *fo;
            int len, hops;
            unsigned long long cw = 0, cf = 0, cs = 0, ci = 0, co = 0, ct = 0;
            unsigned int mw = 0, mf = 0, ms = 0, mi = 0, mo = 0, mt = 0;

            sprintf(fin_path, "%s/clip_%02d.raw", QDIG_IN_DIR, idx);
            sprintf(fout_path, "%s/clip_%02d.raw", QDIG_OUT_DIR, idx);

            fi = fopen(fin_path, "rb");
            if (!fi) {
                printf("[ERR] Gagal buka %s\n", fin_path);
                DSK6416_LED_on(3);
                for (;;) { }
            }
            len = (int)fread(clipIn, sizeof(short), QDIG_MAXLEN, fi);
            fclose(fi);

            hops = process_one_buffer(
                len,
                &cw, &mw, &cf, &mf, &cs, &ms, &ci, &mi, &co, &mo, &ct, &mt
            );

            sum_win += cw;  if (mw > max_win)  max_win  = mw;
            sum_fft += cf;  if (mf > max_fft)  max_fft  = mf;
            sum_ss  += cs;  if (ms > max_ss)   max_ss   = ms;
            sum_ifft += ci; if (mi > max_ifft) max_ifft = mi;
            sum_ola += co;  if (mo > max_ola)  max_ola  = mo;
            sum_tot += ct;  if (mt > max_tot)  max_tot  = mt;
            total_hops += hops;

            fo = fopen(fout_path, "wb");
            if (!fo) {
                printf("[ERR] Gagal tulis %s\n", fout_path);
                DSK6416_LED_on(3);
                for (;;) { }
            }
            /* Skip 1 hop delay OLA awal agar sejajar dan panjang = len sampel */
            fwrite(&clipOut[HOP], sizeof(short), (size_t)len, fo);
            fclose(fo);

            printf("[Q-DIG %02d/%02d] samples=%d, hops=%d, mean_cyc=%lu, max_cyc=%u\n",
                   idx, QDIG_NCLIPS, len, hops,
                   (unsigned long)(hops > 0 ? ct / (unsigned long long)hops : 0ULL), mt);
            DSK6416_LED_toggle(2);
        }
    }
#elif QDIG_USE_CIO
    {
        FILE *fi = fopen(QDIG_IN_FILE, "rb");
        FILE *fo;
        int len;
        if (!fi) {
            printf("[ERR] Gagal buka %s\n", QDIG_IN_FILE);
            DSK6416_LED_on(3);
            for (;;) { }
        }
        len = (int)fread(clipIn, sizeof(short), QDIG_MAXLEN, fi);
        fclose(fi);

        total_hops = process_one_buffer(
            len,
            &sum_win, &max_win, &sum_fft, &max_fft, &sum_ss, &max_ss,
            &sum_ifft, &max_ifft, &sum_ola, &max_ola, &sum_tot, &max_tot
        );

        fo = fopen(QDIG_OUT_FILE, "wb");
        if (fo) {
            fwrite(&clipOut[HOP], sizeof(short), (size_t)len, fo);
            fclose(fo);
        }
    }
#else
    {
        int len = fill_synth_buffer(clipIn, 12000);
        total_hops = process_one_buffer(
            len,
            &sum_win, &max_win, &sum_fft, &max_fft, &sum_ss, &max_ss,
            &sum_ifft, &max_ifft, &sum_ola, &max_ola, &sum_tot, &max_tot
        );
    }
#endif

    DSK6416_LED_off(0);
    DSK6416_LED_on(1);

    if (total_hops > 0) {
        printf("\n=== Q-DIG Profiling (%s, IDENTITY=%d, LUT=%d, hops=%d) ===\n",
               QDIG_USE_CIO ? (QDIG_BATCH ? "CIO-BATCH" : "CIO") : "SYNTH",
               IDENTITY_TEST, USE_FAST_SQRT_LUT, total_hops);
        printf("Framing+Win  : mean=%lu, max=%u cycles\n", (unsigned long)(sum_win / total_hops), max_win);
        printf("FFT 256      : mean=%lu, max=%u cycles\n", (unsigned long)(sum_fft / total_hops), max_fft);
        printf("SS (PY+N+G)  : mean=%lu, max=%u cycles\n", (unsigned long)(sum_ss  / total_hops), max_ss);
        printf("IFFT 256     : mean=%lu, max=%u cycles\n", (unsigned long)(sum_ifft / total_hops), max_ifft);
        printf("OLA+SAT16    : mean=%lu, max=%u cycles\n", (unsigned long)(sum_ola / total_hops), max_ola);
        printf("TOTAL hop    : mean=%lu, max=%u cycles (Budget=16000000)\n",
               (unsigned long)(sum_tot / total_hops), max_tot);
    }

    for (;;) { }
}

#else /* LAB_MODE == LAB_MODE_QLIVE */

extern far void vectors(void); /* didefinisikan di vectors_c6416.asm (_vectors) */

static DSK6416_AIC23_Config codec_cfg = {
    0x0017, /* 0 Left line input volume : 0 dB      */
    0x0017, /* 1 Right line input volume: 0 dB      */
    0x01f9, /* 2 Left headphone volume  : 0 dB      */
    0x01f9, /* 3 Right headphone volume : 0 dB      */
    0x0011, /* 4 Analog audio path: DAC on, LINE IN */
    0x0000, /* 5 Digital audio path                 */
    0x0000, /* 6 Power down: semua aktif            */
    0x0043, /* 7 Digital IF: DSP mode, 16-bit       */
    0x0081, /* 8 Sample rate (di-override setFreq)  */
    0x0001  /* 9 Digital interface activation       */
};

#pragma DATA_ALIGN(inPP, 8)
#pragma DATA_ALIGN(outPP, 8)
static short inPP[2][HOP];
static short outPP[2][HOP];

static volatile int ppIdx = 0;
static volatile int sampIdx = 0;
static volatile int frameReady = 0;
static volatile unsigned int overrunCount = 0;
static volatile int lr_phase = 0;
static volatile short last_out = 0;

interrupt void c_int11(void)
{
    short in_s = (short)(MCBSP_read(DSK6416_AIC23_DATAHANDLE) & 0xFFFFu);

    if (lr_phase == 0) {
        inPP[ppIdx][sampIdx] = in_s;
        last_out = outPP[ppIdx][sampIdx];
        MCBSP_write(DSK6416_AIC23_DATAHANDLE, (Uint32)(Uint16)last_out);
        lr_phase = 1;
    } else {
        MCBSP_write(DSK6416_AIC23_DATAHANDLE, (Uint32)(Uint16)last_out);
        lr_phase = 0;

        if (++sampIdx >= HOP) {
            sampIdx = 0;
            ppIdx ^= 1;
            if (frameReady) {
                overrunCount++;
            }
            frameReady = 1;
        }
    }
}

void main(void)
{
    DSK6416_AIC23_CodecHandle hCodec;
    int b;
    int prev_bypass = -1;

    DSK6416_init();
    DSK6416_LED_init();
    DSK6416_DIP_init();
    memset(inPP, 0, sizeof(inPP));
    memset(outPP, 0, sizeof(outPP));
    stft_app_init(IDENTITY_TEST);
    /* Q-LIVE memakai Minimum Statistics agar tidak terkunci pada hening awal sebelum Play */
    stft_app_params()->use_minstat = 1;

    IRQ_globalDisable();
    IRQ_setVecs((void *)vectors);
    IRQ_nmiEnable();
    IRQ_map(IRQ_EVT_RINT2, 11);
    IRQ_reset(IRQ_EVT_RINT2);

    hCodec = DSK6416_AIC23_openCodec(0, &codec_cfg);
    DSK6416_AIC23_setFreq(hCodec, DSK6416_AIC23_FREQ_8KHZ);

    while (!MCBSP_rrdy(DSK6416_AIC23_DATAHANDLE)) { }
    (void)MCBSP_read(DSK6416_AIC23_DATAHANDLE);
    MCBSP_write(DSK6416_AIC23_DATAHANDLE, 0);
    while (!MCBSP_rrdy(DSK6416_AIC23_DATAHANDLE)) { }
    (void)MCBSP_read(DSK6416_AIC23_DATAHANDLE);
    MCBSP_write(DSK6416_AIC23_DATAHANDLE, 0);
    IRQ_clear(IRQ_EVT_RINT2);

    IRQ_enable(IRQ_EVT_RINT2);
    IRQ_globalEnable();

    for (;;) {
        int bypass;
        while (!frameReady) { }
        frameReady = 0;
        b = ppIdx ^ 1;

        bypass = (DSK6416_DIP_get(0) == 0) ? 1 : 0;
        if ((prev_bypass == 1 && bypass == 0) || (DSK6416_DIP_get(1) == 0)) {
            stft_app_reset();
        }
        prev_bypass = bypass;

        if (bypass) {
            DSK6416_LED_off(0);
            DSK6416_LED_on(1);
        } else {
            DSK6416_LED_on(0);
            DSK6416_LED_off(1);
        }

        stft_app_process_hop(inPP[b], outPP[b], bypass);

        if (overrunCount > 0u) {
            DSK6416_LED_on(3);
        }
    }
}

#endif /* LAB_MODE */
