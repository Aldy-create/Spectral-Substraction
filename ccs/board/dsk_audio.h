#ifndef DSK_AUDIO_H
#define DSK_AUDIO_H

/* Codec AIC23, LED, dan DIP switch pada DSK6416.
 * Sampel 16-bit. Satu panggilan audio_read / audio_write = satu kanal
 * (kiri lalu kanan, bergantian).
 */
void audio_init(int rate_hz);
int audio_read(void);
void audio_write(int sample);
void led_show(int mask);
int dip_down(int n);

#define AUDIO_SAT(s) ((s) > 32767 ? 32767 : ((s) < -32768 ? -32768 : (s)))

#endif
