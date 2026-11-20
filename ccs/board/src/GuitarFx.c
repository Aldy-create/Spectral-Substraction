/*
 * GuitarFx
 * Gitar atau sumber line ke LINE IN, headphone di LINE OUT.
 * Saklar ke bawah mengaktifkan efek (boleh lebih dari satu):
 *   DIP 0 = tremolo
 *   DIP 1 = distorsi
 *   DIP 2 = wah
 * Semua ke atas = suara bersih (LED3 menyala).
 * LED0, LED1, LED2 menyala sesuai efek yang aktif.
 */
#include "dsk_audio.h"

static const short lfo[32] = {
    0, 6393, 12539, 18204, 23170, 27245, 30273, 32138,
    32767, 32138, 30273, 27245, 23170, 18204, 12539, 6393,
    0, -6393, -12539, -18204, -23170, -27245, -30273, -32138,
    -32767, -32138, -30273, -27245, -23170, -18204, -12539, -6393
};

static unsigned phase;
static int wah_y[2];
static int ch;

static int distort(int s)
{
    if (s > 7000) {
        s = 7000 + ((s - 7000) >> 2);
    } else if (s < -7000) {
        s = -7000 + ((s + 7000) >> 2);
    }
    return AUDIO_SAT(s * 2);
}

static int tremolo(int s)
{
    int g = (32767 + lfo[phase >> 27]) >> 1;
    return (s * g) >> 15;
}

static int wah(int s)
{
    int a = 6000 + (lfo[phase >> 27] >> 2);
    int y = wah_y[ch];

    if (a < 800) {
        a = 800;
    }
    if (a > 24000) {
        a = 24000;
    }
    y += (a * (s - y)) >> 15;
    wah_y[ch] = y;
    return y;
}

void main(void)
{
    audio_init(48000);

    for (;;) {
        int s = audio_read();
        int mask = 0;

        if (dip_down(1)) {
            s = distort(s);
            mask |= 2;
        }
        if (dip_down(2)) {
            s = wah(s);
            mask |= 4;
        }
        if (dip_down(0)) {
            s = tremolo(s);
            mask |= 1;
        }
        if (mask == 0) {
            mask = 8;
        }
        led_show(mask);
        audio_write(AUDIO_SAT(s));

        phase += 700000u;
        ch ^= 1;
    }
}
