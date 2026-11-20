/*
 * Dtmf
 * Masukkan nada DTMF (telepon) ke LINE IN. Headphone mengulang suara itu.
 * Codec di 8 kHz. Empat LED menampilkan digit sebagai nibble biner
 * (LED0 = bit paling rendah):
 *   0-9 = angka, A=10, B=11, C=12, D=13, *=14, #=15.
 * Semua LED mati jika tidak ada nada.
 */
#include "dsk_audio.h"

#define NBUF 160

static const int coef[8] = {
    27980, 26956, 25701, 24219, 19073, 16325, 13085, 9315
};

static const int hexdigit[4][4] = {
    { 0x1, 0x2, 0x3, 0xA },
    { 0x4, 0x5, 0x6, 0xB },
    { 0x7, 0x8, 0x9, 0xC },
    { 0xE, 0x0, 0xF, 0xD }
};

static int q1[8];
static int q2[8];
static int count;

static void decode(void)
{
    long long rowp[4];
    long long colp[4];
    int k, r, c, r2, c2;

    for (k = 0; k < 8; k++) {
        long long cross = ((long long)coef[k] * q1[k] * q2[k]) >> 14;
        long long p = (long long)q1[k] * q1[k] + (long long)q2[k] * q2[k] - cross;
        if (p < 0) {
            p = 0;
        }
        if (k < 4) {
            rowp[k] = p;
        } else {
            colp[k - 4] = p;
        }
        q1[k] = 0;
        q2[k] = 0;
    }

    r = 0;
    r2 = 1;
    for (k = 1; k < 4; k++) {
        if (rowp[k] > rowp[r]) {
            r2 = r;
            r = k;
        } else if (rowp[k] > rowp[r2]) {
            r2 = k;
        }
    }
    c = 0;
    c2 = 1;
    for (k = 1; k < 4; k++) {
        if (colp[k] > colp[c]) {
            c2 = c;
            c = k;
        } else if (colp[k] > colp[c2]) {
            c2 = k;
        }
    }

    if (rowp[r] > 1000000 && colp[c] > 1000000 &&
        rowp[r] > rowp[r2] * 4 && colp[c] > colp[c2] * 4 &&
        rowp[r] < colp[c] * 8 && colp[c] < rowp[r] * 8) {
        led_show(hexdigit[r][c]);
    } else {
        led_show(0);
    }
}

static void goertzel(int sample)
{
    int k;
    int s = sample >> 4;

    for (k = 0; k < 8; k++) {
        int q0 = s + (int)(((long long)coef[k] * q1[k]) >> 14) - q2[k];
        q2[k] = q1[k];
        q1[k] = q0;
    }
    count++;
    if (count >= NBUF) {
        count = 0;
        decode();
    }
}

void main(void)
{
    audio_init(8000);

    for (;;) {
        int left = audio_read();
        int right = audio_read();
        audio_write(left);
        audio_write(right);
        /* Jumlah kiri+kanan: nada tetap terdeteksi di kanal mana pun. */
        goertzel((left + right) >> 1);
    }
}
