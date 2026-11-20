/*
 * Equalizer
 * LINE IN ke headphone. Empat pita: bass, low-mid, high-mid, treble.
 * Bass dan treble sedikit dinaikkan.
 * LED yang menyala adalah pita dengan energi paling besar:
 *   LED0 bass, LED1 low-mid, LED2 high-mid, LED3 treble.
 */
#include "dsk_audio.h"

typedef struct {
    int s0;
    int s1;
    int s2;
} EqState;

static EqState eq[2];
static int ch;
static unsigned energy[4];
static int block;

static int pole(int *state, int x, int a)
{
    *state += (a * (x - *state)) >> 15;
    return *state;
}

static int eq_sample(int x)
{
    EqState *e = &eq[ch];
    int bass, midlo, midhi, treble, y;
    int l0, l1, l2;

    l0 = pole(&e->s0, x, 600);
    l1 = pole(&e->s1, x, 2500);
    l2 = pole(&e->s2, x, 8000);

    bass = l0;
    midlo = l1 - l0;
    midhi = l2 - l1;
    treble = x - l2;

    y = ((bass * 3) >> 1) + midlo + midhi + ((treble * 3) >> 1);
    y >>= 1;

    energy[0] += (unsigned)(bass < 0 ? -bass : bass);
    energy[1] += (unsigned)(midlo < 0 ? -midlo : midlo);
    energy[2] += (unsigned)(midhi < 0 ? -midhi : midhi);
    energy[3] += (unsigned)(treble < 0 ? -treble : treble);
    block++;
    if (block >= 4096) {
        int m = 0;
        int b;
        for (b = 1; b < 4; b++) {
            if (energy[b] > energy[m]) {
                m = b;
            }
        }
        led_show(1 << m);
        energy[0] = energy[1] = energy[2] = energy[3] = 0;
        block = 0;
    }

    return AUDIO_SAT(y);
}

void main(void)
{
    audio_init(48000);

    for (;;) {
        audio_write(eq_sample(audio_read()));
        ch ^= 1;
    }
}
