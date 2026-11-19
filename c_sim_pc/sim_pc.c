/* sim_pc.c -- Testbench Simulator PC untuk Spectral Subtraction Q15 (Kelompok A)
 *
 * Fitur:
 *  - Baca & tulis file WAV 8 kHz 16-bit PCM mono
 *  - Kerangka STFT identik dengan board TMS320C6416:
 *      NFFT = 256, HOP = 128, Hann periodik Q15 (hann_win.h)
 *      Radix-2 DIT FFT/IFFT int32 tanpa scaling per stage (twiddle Q15)
 *      OLA 32-bit + saturasi 16-bit (SAT16)
 *  - Memanggil langsung ss_default_params(), ss_init(), dan ss_process() dari ../c_board/ss.c
 *
 * Kompilasi (MinGW/GCC):
 *   gcc -O2 -Wall -Wextra -I../c_board sim_pc.c ../c_board/ss.c -lm -o sim_pc.exe
 *
 * Penggunaan:
 *   sim_pc.exe <input.wav> <output.wav> [--identity]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "../c_board/ss.h"
#include "../c_board/hann_win.h"

#define HOP   128
#define LOG2N 8
#define FS    8000

/* Makro fixed-point Q15 (identik dengan q15.h Panduan Umum Listing 7) */
#define SAT16(x) ((x) > 32767 ? 32767 : ((x) < -32768 ? -32768 : (short)(x)))
#define Q15_MUL(a, b) SAT16((((int)(a) * (int)(b)) + (1 << 14)) >> 15)

/* Buffer STFT global (meniru struktur statis di board) */
static short xin[NFFT];
static int   ola[NFFT];
static int   Xre[NFFT];
static int   Xim[NFFT];

/* Tabel twiddle Q15 untuk Radix-2 FFT/IFFT */
static short tw_re[NFFT / 2];
static short tw_im[NFFT / 2];

static ss_params_t g_params;
static ss_state_t  g_state;

static void fft_init(void)
{
    int k;
    for (k = 0; k < NFFT / 2; k++) {
        double a = 2.0 * 3.14159265358979323846 * (double)k / (double)NFFT;
        tw_re[k] = (short)floor( cos(a) * 32767.0 + 0.5);
        tw_im[k] = (short)floor(-sin(a) * 32767.0 + 0.5);
    }
}

static void bitrev(int *re, int *im)
{
    int i, j = 0, k, t;
    for (i = 0; i < NFFT - 1; i++) {
        if (i < j) {
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
        k = NFFT >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

/* Radix-2 DIT FFT/IFFT int32 tanpa scaling per stage (twiddle Q15).
 * inverse = 0 -> FFT maju
 * inverse = 1 -> IFFT (hasil dibagi NFFT = 256 di akhir)
 */
static void fft_r2(int *re, int *im, int inverse)
{
    int stage, half, step, k, i, j;
    bitrev(re, im);

    for (stage = 1, half = 1; stage <= LOG2N; stage++, half <<= 1) {
        step = NFFT / (half << 1);
        for (k = 0; k < half; k++) {
            int wr = (int)tw_re[k * step];
            int wi = inverse ? -(int)tw_im[k * step] : (int)tw_im[k * step];
            for (i = k; i < NFFT; i += (half << 1)) {
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
        for (i = 0; i < NFFT; i++) {
            re[i] >>= LOG2N;
            im[i] >>= LOG2N;
        }
    }
}

static void stft_reset(void)
{
    memset(xin, 0, sizeof(xin));
    memset(ola, 0, sizeof(ola));
}

/* Proses 1 hop (128 sampel baru -> 128 sampel output ter-OLA + SAT16) */
static void process_hop(const short *in, short *out)
{
    int n;

    /* 1. Geser riwayat: buang 128 sampel lama, masukkan 128 sampel baru */
    for (n = 0; n < HOP; n++) {
        xin[n]       = xin[n + HOP];
        xin[n + HOP] = in[n];
    }

    /* 2. Window Hann Q15 + Radix-2 FFT int32 tanpa scaling */
    for (n = 0; n < NFFT; n++) {
        Xre[n] = (int)Q15_MUL(xin[n], HANN_WIN_Q15[n]);
        Xim[n] = 0;
    }
    fft_r2(Xre, Xim, 0);

    /* 3. Enhancement: Spectral Subtraction Q15 (in-place + simetri konjugat) */
    ss_process(&g_state, &g_params, Xre, Xim);

    /* 4. IFFT + Overlap-Add + Saturasi 16-bit */
    fft_r2(Xre, Xim, 1);
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
}

/* ---------------- Utilites WAV 16-bit PCM Little-Endian ---------------- */

static uint16_t read_u16_le(const unsigned char *b)
{
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t read_u32_le(const unsigned char *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void write_u16_le(FILE *f, uint16_t v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    fwrite(b, 1, 2, f);
}

static void write_u32_le(FILE *f, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    fwrite(b, 1, 4, f);
}

static int load_wav_mono16(const char *path, short **out_buf, size_t *out_len, uint32_t *out_fs)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[12];
    uint16_t audio_fmt = 0, num_ch = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0, data_bytes = 0;
    int found_fmt = 0, found_data = 0;

    if (!f) {
        fprintf(stderr, "[ERR] Gagal membuka input WAV: %s\n", path);
        return -1;
    }
    if (fread(hdr, 1, 12, f) != 12 ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "[ERR] File bukan WAV RIFF/WAVE valid: %s\n", path);
        fclose(f);
        return -1;
    }

    while (!found_data) {
        unsigned char chdr[8];
        uint32_t csize;
        if (fread(chdr, 1, 8, f) != 8) {
            break;
        }
        csize = read_u32_le(chdr + 4);

        if (memcmp(chdr, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (csize < 16 || fread(fmt, 1, 16, f) != 16) {
                fclose(f);
                return -1;
            }
            audio_fmt       = read_u16_le(fmt + 0);
            num_ch          = read_u16_le(fmt + 2);
            sample_rate     = read_u32_le(fmt + 4);
            bits_per_sample = read_u16_le(fmt + 14);
            if (csize > 16) {
                fseek(f, (long)(csize - 16), SEEK_CUR);
            }
            found_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            data_bytes = csize;
            found_data = 1;
        } else {
            long skip = (long)csize + (long)(csize & 1u);
            fseek(f, skip, SEEK_CUR);
        }
    }

    if (!found_fmt || !found_data || audio_fmt != 1 || bits_per_sample != 16) {
        fprintf(stderr, "[ERR] Format WAV harus PCM 16-bit (%s)\n", path);
        fclose(f);
        return -1;
    }

    {
        size_t total_frames = (size_t)data_bytes / (sizeof(short) * (size_t)num_ch);
        short *buf = (short *)malloc(total_frames * sizeof(short));
        size_t i;
        if (!buf) {
            fclose(f);
            return -1;
        }
        for (i = 0; i < total_frames; i++) {
            unsigned char sbytes[2];
            if (fread(sbytes, 1, 2, f) != 2) {
                total_frames = i;
                break;
            }
            buf[i] = (short)read_u16_le(sbytes);
            if (num_ch > 1) {
                fseek(f, (long)(2 * (num_ch - 1)), SEEK_CUR);
            }
        }
        *out_buf = buf;
        *out_len = total_frames;
        *out_fs  = sample_rate;
    }

    fclose(f);
    return 0;
}

static int save_wav_mono16(const char *path, const short *buf, size_t len, uint32_t fs)
{
    FILE *f = fopen(path, "wb");
    uint32_t data_bytes = (uint32_t)(len * sizeof(short));
    size_t i;

    if (!f) {
        fprintf(stderr, "[ERR] Gagal menulis output WAV: %s\n", path);
        return -1;
    }

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, 36u + data_bytes);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16u);
    write_u16_le(f, 1u);         /* PCM */
    write_u16_le(f, 1u);         /* Mono */
    write_u32_le(f, fs);         /* Sample rate */
    write_u32_le(f, fs * 2u);    /* Byte rate */
    write_u16_le(f, 2u);         /* Block align */
    write_u16_le(f, 16u);        /* Bits per sample */

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_bytes);
    for (i = 0; i < len; i++) {
        write_u16_le(f, (uint16_t)buf[i]);
    }

    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *in_path  = NULL;
    const char *out_path = NULL;
    int identity_mode = 0;
    int minstat_mode = 0;
    int argi;

    short *in_buf = NULL;
    short *padded_in = NULL;
    short *out_buf = NULL;
    size_t in_len = 0;
    size_t num_hops = 0;
    size_t padded_len = 0;
    uint32_t fs = FS;
    size_t k;
    int speech_frames = 0;
    int peak_abs = 0;

    if (argc < 3) {
        printf("Penggunaan: %s <input.wav> <output.wav> [--identity] [--minstat]\n", argv[0]);
        return 1;
    }

    in_path  = argv[1];
    out_path = argv[2];
    for (argi = 3; argi < argc; argi++) {
        if (strcmp(argv[argi], "--identity") == 0) {
            identity_mode = 1;
        } else if (strcmp(argv[argi], "--minstat") == 0) {
            minstat_mode = 1;
        }
    }

    if (load_wav_mono16(in_path, &in_buf, &in_len, &fs) != 0) {
        return 1;
    }
    if (fs != FS) {
        fprintf(stderr, "[WARN] Sample rate input %u Hz (ekspektasi %d Hz).\n", fs, FS);
    }

    /* Tambah 1 hop nol di akhir untuk flush delay OLA (128 sampel) agar ekor klip utuh */
    num_hops   = (in_len + HOP - 1) / HOP + 1;
    padded_len = num_hops * HOP;
    padded_in  = (short *)calloc(padded_len, sizeof(short));
    out_buf    = (short *)calloc(padded_len, sizeof(short));
    if (!padded_in || !out_buf) {
        free(in_buf);
        free(padded_in);
        free(out_buf);
        return 1;
    }
    memcpy(padded_in, in_buf, in_len * sizeof(short));

    fft_init();
    stft_reset();
    ss_default_params(&g_params);
    if (minstat_mode) {
        g_params.use_minstat = 1;
    }
    if (identity_mode) {
        g_params.beta_q15 = 32767; /* Trik Identity Test: paksa G[k] ~ 1.0 */
    }
    ss_init(&g_state, &g_params);

    for (k = 0; k < padded_len; k += HOP) {
        process_hop(&padded_in[k], &out_buf[k]);
        if (g_state.vad_speech) {
            speech_frames++;
        }
    }

    /* Kompensasi delay OLA 1 hop (128 sampel) agar panjang dan fase sejajar dengan input */
    for (k = 0; k < in_len; k++) {
        int a = (int)out_buf[k + HOP];
        if (a < 0) a = -a;
        if (a > peak_abs) peak_abs = a;
    }

    if (save_wav_mono16(out_path, &out_buf[HOP], in_len, fs) != 0) {
        free(in_buf);
        free(padded_in);
        free(out_buf);
        return 1;
    }

    printf("[SIM-PC %s] %s -> %s | samples=%zu, hops=%zu, speech_frames=%d, peak=%d/32767 (LUT=%d)\n",
           identity_mode ? "IDENTITY" : "Q15-SS",
           in_path, out_path, in_len, num_hops, speech_frames, peak_abs, USE_FAST_SQRT_LUT);

    free(in_buf);
    free(padded_in);
    free(out_buf);
    return 0;
}
