/*
 * Echo
 * LINE IN ke headphone.
 * Gema sekitar 0,25 detik (12000 sampel pada 48 kHz), dicampur dengan suara asli.
 */
#include "dsk_audio.h"

#define ECHO_LEN 12000

far static short buf[2][ECHO_LEN];
static int ip[2];
static int ch;

void main(void)
{
    audio_init(48000);
    led_show(1);

    for (;;) {
        int s = audio_read();
        int wet = buf[ch][ip[ch]];
        int y = (s * 5 + wet * 3) >> 3;

        buf[ch][ip[ch]] = (short)AUDIO_SAT(s);
        ip[ch]++;
        if (ip[ch] >= ECHO_LEN) {
            ip[ch] = 0;
        }
        audio_write(AUDIO_SAT(y));
        ch ^= 1;
    }
}
