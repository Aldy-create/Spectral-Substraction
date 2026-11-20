/*
 * FirFilter
 * LINE IN ke headphone.
 * DIP 0 ke bawah = low-pass (suara lebih gelap).
 * DIP 0 ke atas   = high-pass (suara lebih tipis).
 * LED0 = low-pass, LED1 = high-pass.
 */
#include "dsk_audio.h"

#define TAPS 31

static const short h_lp[TAPS] = {
    0, 39, 91, 139, 129, 0, -271, -609, -832, -696,
    0, 1297, 3011, 4754, 6059, 6545, 6059, 4754, 3011, 1297,
    0, -696, -832, -609, -271, 0, 129, 139, 91, 39, 0
};

static const short h_hp[TAPS] = {
    0, -39, -91, -139, -129, 0, 271, 609, 832, 696,
    0, -1297, -3011, -4754, -6059, 26222, -6059, -4754, -3011, -1297,
    0, 696, 832, 609, 271, 0, -129, -139, -91, -39, 0
};

static short delay[2][TAPS];
static int pos[2];
static int ch;

static int fir(int x, const short *h)
{
    long long acc = 0;
    int p;
    int i;

    delay[ch][pos[ch]] = (short)AUDIO_SAT(x);
    p = pos[ch];
    for (i = 0; i < TAPS; i++) {
        acc += (long long)h[i] * delay[ch][p];
        if (--p < 0) {
            p = TAPS - 1;
        }
    }
    pos[ch]++;
    if (pos[ch] >= TAPS) {
        pos[ch] = 0;
    }
    return (int)(acc >> 15);
}

void main(void)
{
    audio_init(48000);

    for (;;) {
        int low;
        int y;

        low = dip_down(0);
        led_show(low ? 1 : 2);
        y = fir(audio_read(), low ? h_lp : h_hp);
        audio_write(y);
        ch ^= 1;
    }
}
