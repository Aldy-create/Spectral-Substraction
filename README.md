# Proyek Riset DSP (AIR24) — Kelompok A: Spectral Subtraction Q15 (TMS320C6416T DSK)

**Anggota Kelompok A:** Aldy Alfaisal Pasaribu, Vincent Junio Ibrahim  
**Program:** Digital Signal Processing (AIR24) — Universitas Prasetiya Mulya (Semester Gasal 2026/2027)  
**Platform:** Spectrum Digital TMS320C6416T DSK (1 GHz), Code Composer Studio 6.2 (`cl6x v7.4.18`, Legacy COFF)

---

## Ringkasan Arsitektur & Spesifikasi
- **Audio & Framing:** Mono `8 kHz`, `N = 256` (32 ms), `H = 128` (overlap 50%), window Hann periodik (COLA), Radix-2 DIT FFT/IFFT `int32` tanpa scaling, Overlap-Add + saturasi 16-bit (`SAT16`).
- **Format Fixed-Point (`c_board/ss.h`, `c_board/ss.c`):**
  - Audio & `HANN_WIN_Q15[256]`: `short` Q15
  - Spektrum FFT `Xre`, `Xim`: `int32` (`|Y| <= 2^23`)
  - Daya `PY`, `PN`: `unsigned int` (setelah `pow_shift = 8`)
  - Over-subtraction `alpha`: `2.0` (`8192` Q12)
  - Spectral floor `beta` (pada `G^2`): `0.02` (`655` Q15)
  - Gain `G`: `short` Q15 (`Fast SQRT LUT` 257-entri + interpolasi linear), fase *noisy* dipertahankan + simetri konjugat manual (`k = 1..127`, `im[0] = im[128] = 0`).
  - Estimasi Noise:
    - **`Q-DIG` (default):** `n_init = 6` frame awal + VAD rasio energi (`vad_thr_q8 = 512` / 3 dB) + update shift-only `lambda = 1 - 2^-4 = 0.9375`.
    - **`Q-LIVE` (real-time):** Minimum Statistics (`U = 8`, `V = 8` ≈ 1,02 s, `B_min = 1.8`) agar tidak terkunci pada hening awal sebelum audio diputar.

---

## Struktur Direktori
```text
dsp_kelompok_a/
├── c_board/                   # Kode C/ASM/CMD siap pakai untuk TMS320C6416T DSK
├── c_sim_pc/                  # Testbench simulator PC (sim_pc.c)
├── ccs/KelompokA_SS/          # Project CCS 6.2 bersih (tanpa artifact Debug/Release)
├── python/                    # Skrip persiapan data, F-PY, evaluasi, Colab PESQ, & pengisi Excel
├── data/                      # Metadata & checksum test set (dataset WAV tidak di-commit)
├── results/
│   ├── F-PY/                  # 30 klip output referensi float Python
│   ├── Q-DIG/                 # 30 klip output board TMS320C6416T (Q15 jalur digital)
│   ├── map/                   # File .map build Release -O3 final
│   ├── plots/                 # Plot spektrogram perbandingan (Clean, Noisy, F-PY, Q-DIG)
│   ├── *.csv                  # Hasil metrik & per-clip cycles
│   └── Template_Pengukuran_Hasil_KelompokA.xlsx
├── GEMINI.md                  # Panduan cepat lab
└── Panduan_Step_by_Step_Lab_Kelompok_A.pdf
```

---

## Ringkasan Hasil Pengukuran (`n = 30` Klip VoiceBank-DEMAND 8 kHz, `5.194` Hops)
| Metrik | `F-PY` (Float Python) | `Q-DIG` (Board Q15) | Gap (`F − Q`) |
|---|---:|---:|---:|
| **`SNR_in` → `SNR_out` (dB)** | `8.237 → 11.316` | `8.237 → 11.302` | — |
| **`ΔSNR` (dB)** | **`+3.079 ± 1.626`** | **`+3.065 ± 1.633`** | `+0.014` |
| **`SegSNR_in` → `SegSNR_out` (dB)** | `1.458 → 4.639` | `1.458 → 4.622` | — |
| **`PESQ-NB`** | **`3.234 ± 0.641`** | **`3.193 ± 0.620`** | `+0.041` |
| **`STOI`** | **`0.9181 ± 0.0954`** | **`0.9209 ± 0.0941`** | `-0.0028` |
| **`SNR_q` (dB)** | — | **`42.574 ± 3.587`** | — |

- **Cycles/frame (`Release -O3`):** `mean = 360.980`, `max = 414.424` (`RTF = 0.0226` mean / `0.0259` max terhadap budget `16.000.000`).
- **Memory Algoritma (`KelompokA_SS.map`):** **`15.54 KB`** (`.algtext = 4.19 KB`, `.algdata = 10.85 KB`, `_HANN_WIN_Q15 = 0.50 KB`).
- **Real-Time (`Q-LIVE`):** **`overrunCount = 0`** dengan toggle langsung DIP switch `SW0` (*Bypass ↔ Enhanced*).

---

## Cara Reproduksi di PC
```powershell
# 1. Resample dataset VoiceBank-DEMAND 48 kHz -> 8 kHz (taruh WAV asli di data/clean_48k & data/noisy_48k)
python ./python/resample_voicebank.py --n-clips 30

# 2. Jalankan referensi Python (F-PY)
python ./python/ss_ref.py

# 3. Compile & jalankan simulator C PC
gcc -O2 -Wall -Wextra -I./c_board ./c_sim_pc/sim_pc.c ./c_board/ss.c -lm -o ./c_sim_pc/sim_pc.exe
./c_sim_pc/sim_pc.exe ./data/noisy/clip_01.wav ./results/Q15_PC/clip_01.wav

# 4. Evaluasi metrik (SNR, SegSNR, STOI, PESQ jika terpasang, SNR_q, & spektrogram)
python ./python/evaluate_metrics.py --q15-dir ./results/Q-DIG --csv-out ./results/metrics_QDIG_all.csv
```
