/*
 * NoiseCancel
 * Headphone di LINE OUT.
 * Kanal kiri LINE IN = suara + noise (sinyal yang ingin dibersihkan).
 * Kanal kanan LINE IN = noise saja (referensi).
 * Keluaran headphone adalah sinyal kiri setelah noise dikurangi (LMS, 32 tap).
 * LED0 berkedip pelan selama berjalan.
 *
 * Jack stereo: ujung (tip) = kiri, ring = kanan.
 */
#include "dsk_audio.h"

#define TAPS 32

static short hist[TAPS];
static short w[TAPS];
static int idx;
static unsigned tick;

static int lms(int desired, int reference)
{
    int y = 0;
    int e;
    int i;
    int p;

    hist[idx] = (short)AUDIO_SAT(reference);
    p = idx;
    for (i = 0; i < TAPS; i++) {
        y += (int)w[i] * hist[p];
        if (--p < 0) {
            p = TAPS - 1;
        }
    }
    y >>= 15;
    e = AUDIO_SAT(desired - y);

    p = idx;
    for (i = 0; i < TAPS; i++) {
        int nw = w[i] + ((e * (int)hist[p]) >> 10);
        if (nw > 32767) {
            nw = 32767;
        }
        if (nw < -32767) {
            nw = -32767;
        }
        w[i] = (short)nw;
        if (--p < 0) {
            p = TAPS - 1;
        }
    }

    idx++;
    if (idx >= TAPS) {
        idx = 0;
    }
    return e;
}

void main(void)
{
    audio_init(48000);

    for (;;) {
        int left = audio_read();
        int right = audio_read();
        int e = lms(left, right);

        audio_write(e);
        audio_write(e);

        tick++;
        if ((tick & 0x7FFF) == 0) {
            led_show((tick >> 15) & 1);
        }
    }
}
