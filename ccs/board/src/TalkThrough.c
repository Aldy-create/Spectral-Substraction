/*
 * TalkThrough
 * LINE IN ke headphone (LINE OUT). Suara diteruskan apa adanya, kiri dan kanan.
 * LED0 menyala selama program berjalan.
 */
#include "dsk_audio.h"

void main(void)
{
    audio_init(48000);
    led_show(1);

    for (;;) {
        audio_write(audio_read());
    }
}
