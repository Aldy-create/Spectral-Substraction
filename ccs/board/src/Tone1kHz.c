/*
 * Tone1kHz
 * Headphone di LINE OUT. Tidak perlu sumber suara.
 * Nada sinus 1 kHz terus-menerus. LED0 berkedip tiap setengah detik.
 */
#include "dsk_audio.h"

#define SINE_TABLE_SIZE 48

static const short sine[SINE_TABLE_SIZE] = {
    0x0000, 0x10b4, 0x2120, 0x30fb, 0x3fff, 0x4dea, 0x5a81, 0x658b,
    0x6ed8, 0x763f, 0x7ba1, 0x7ee5, 0x7ffd, 0x7ee5, 0x7ba1, 0x76ef,
    0x6ed8, 0x658b, 0x5a81, 0x4dea, 0x3fff, 0x30fb, 0x2120, 0x10b4,
    0x0000, 0xef4c, 0xdee0, 0xcf06, 0xc002, 0xb216, 0xa57f, 0x9a75,
    0x9128, 0x89c1, 0x845f, 0x811b, 0x8002, 0x811b, 0x845f, 0x89c1,
    0x9128, 0x9a76, 0xa57f, 0xb216, 0xc002, 0xcf06, 0xdee0, 0xef4c
};

void main(void)
{
    unsigned ms = 0;

    audio_init(48000);
    led_show(1);

    for (;;) {
        int i;
        for (i = 0; i < SINE_TABLE_SIZE; i++) {
            audio_write(sine[i]);
            audio_write(sine[i]);
        }
        ms++;
        if ((ms % 500) == 0) {
            led_show((ms / 500) & 1);
        }
    }
}
