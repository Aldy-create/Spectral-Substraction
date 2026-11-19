/* fft_r2.h -- Radix-2 DIT FFT/IFFT 256-titik (int32, twiddle Q15) */
#ifndef FFT_R2_H
#define FFT_R2_H

#define FFT_N     256
#define FFT_LOG2N 8

void fft_init(void);
void fft_r2(int *re, int *im, int inverse);

#endif /* FFT_R2_H */
