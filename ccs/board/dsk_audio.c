#ifndef CHIP_6416
#define CHIP_6416 1
#endif

#include "dsk6416.h"
#include "dsk6416_aic23.h"
#include "dsk6416_led.h"
#include "dsk6416_dip.h"
#include "dsk_audio.h"

/* Sama dengan contoh tone Spectrum Digital: 48 kHz, 16-bit, codec master. */
static DSK6416_AIC23_Config config = {
    0x0017, 0x0017, 0x00d8, 0x00d8, 0x0011,
    0x0000, 0x0000, 0x0043, 0x0001, 0x0001
};

static DSK6416_AIC23_CodecHandle codec;

static void fail_blink(void)
{
    unsigned n = 0;
    for (;;) {
        led_show(1 << (n & 3));
        n++;
        {
            volatile unsigned d;
            for (d = 0; d < 2000000u; d++) { }
        }
    }
}

void audio_init(int rate_hz)
{
    DSK6416_init();
    DSK6416_LED_init();
    DSK6416_DIP_init();
    codec = DSK6416_AIC23_openCodec(0, &config);
    if (codec < 0) {
        fail_blink();
    }
    if (rate_hz == 8000) {
        DSK6416_AIC23_setFreq(codec, DSK6416_AIC23_FREQ_8KHZ);
    }
}

int audio_read(void)
{
    Uint32 v = 0;
    while (!DSK6416_AIC23_read(codec, &v)) { }
    return (int)(short)(v & 0xFFFF);
}

void audio_write(int sample)
{
    while (!DSK6416_AIC23_write(codec, (Uint32)(sample & 0xFFFF))) { }
}

void led_show(int mask)
{
    unsigned i;
    for (i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            DSK6416_LED_on(i);
        } else {
            DSK6416_LED_off(i);
        }
    }
}

int dip_down(int n)
{
    return DSK6416_DIP_get((Uint32)n) == 0;
}
