# DSP Kelompok A — Spectral Subtraction (Q15 Fixed-Point, TMS320C6416T DSK)

Panduan ringkas arsitektur, format data Q15, simulasi PC, dan **step-by-step eksekusi lab CCS 6.2** untuk TMS320C6416T DSK (`Target: overrunCount == 0`, `Budget: 16.000.000 cycles/hop` @ 1 GHz, `Release -O3`).

---

## 1. Struktur Workspace `dsp_kelompok_a/`

```text
dsp_kelompok_a/
├── GEMINI.md                  # Panduan arsitektur, format Q, & step-by-step lab besok
├── python/
│   ├── prep_data.py           # Generate hann_win.h (Q15) + sinyal uji sintetis 8 kHz
│   ├── ss_ref.py              # Referensi float Python (F-PY) dengan STFT/OLA identik
│   └── evaluate_metrics.py    # Hitung SNR_in, SNR_out, Delta_SNR, SNR_q, & plot spektrogram
├── c_board/                   # File siap copy-paste ke project CCS 6.2 TMS320C6416
│   ├── hann_win.h             # Tabel const short HANN_WIN_Q15[256]
│   ├── ss.h / ss.c            # Spectral Subtraction Q15 + Fast SQRT LUT 257-entri + pragma C64x
│   ├── fft_r2.h / fft_r2.c    # Radix-2 DIT FFT/IFFT 256-titik int32 tanpa scaling
│   ├── stft_app.h / stft_app.c# Framework STFT + OLA + SAT16 + fungsi profiling per blok
│   ├── main_board.c           # Entry point CCS (pilih LAB_MODE_QDIG atau LAB_MODE_QLIVE)
│   ├── vectors_c6416.asm      # Tabel vektor interrupt (RESET -> _c_int00, INT11 -> _c_int11)
│   └── C6416_alg.cmd          # Linker cmd dengan section .algtext, .algdata, .extdata
└── c_sim_pc/
    └── sim_pc.c               # Testbench simulator PC (WAV 8 kHz 16-bit, identik dengan board)
```

---

## 2. Format Fixed-Point & Parameter Default

| Variabel / Tahap | Tipe C | Format | Default / Catatan |
|---|---|---|---|
| Audio & Window `HANN_WIN_Q15` | `short` | Q15 | `fs = 8 kHz`, `N = 256`, `H = 128`, input `<= -6 dBFS` |
| Spektrum `Xre[256]`, `Xim[256]` | `int` (`int32`) | Unscaled | `\|Y\| <= 2^23` untuk input Q15 |
| Daya `PY[129]` & Noise `PN[129]` | `unsigned int` | Scaled `2^(-2*8)` | `pow_shift = 8` (turunkan ke `0`/`2` jika ganti `DSP_fft16x16`) |
| Over-subtraction `alpha_q12` | `short` | Q12 | `2.0 -> 8192` |
| Spectral Floor `beta_q15` | `short` | Q15 | `0.02 -> 655` (Identity Test: set `32767`) |
| Smoothing `lam_shift` | `int` | Shift | `4 -> lambda = 1 - 2^-4 = 0.9375` |
| Ambang VAD `vad_thr_q8` | `int` | Q8 | `3 dB (~2.0) -> 512` |
| Inisialisasi Noise `n_init` | `int` | Frame | `6` frame awal (`96 ms`) |
| Gain `G[129]` | `short` | Q15 | `G in [sqrt(beta), 1)`, jaga `im[0]=im[128]=0`, konjugat `k=1..127` |

---

## 3. Step-by-Step Eksekusi Lab Besok (Target 120 Menit)

### Langkah 0 — Menyalakan Board & Setup Project CCS 6.2 (00–15 Menit)
1. **Urutan nyalakan hardware (wajib):** Colok adaptor `+5 V` ke DSK6416 -> tunggu LED menyala -> baru colok kabel USB ke laptop.
2. Buka **CCS 6.2** dengan workspace yang ada (`workspace_v6_2`), klik **Test Connection** pada `.ccxml` -> pastikan *scan chain* lulus tanpa error.
3. Buat project baru (atau duplikasi project `TalkThrough` yang sudah jalan):
   - Target: `TMS320C6416`, Compiler: `TI v7.4.x`, Output format: **Legacy COFF**.
   - **Copy-paste** seluruh file dari `dsp_kelompok_a/c_board/` ke folder project CCS:
     - `hann_win.h`, `ss.h`, `ss.c`
     - `fft_r2.h`, `fft_r2.c`
     - `stft_app.h`, `stft_app.c`
     - `vectors_c6416.asm`
     - `C6416_alg.cmd` *(hapus `C6416.cmd` bawaan agar tidak bentrok dua file `.cmd`)*
     - `main_board.c` *(hapus `main.c` lama jika ada)*
4. Cek **Project -> Properties -> Build**:
   - **C6000 Compiler -> Processor Options:** `-mv6400`
   - **Predefined Symbols:** `CHIP_6416`
   - **Include Options:** pastikan path `../board/include` dan folder project ikut tercantum.
   - **C6000 Linker -> File Search Path:** pastikan `dsk6416bsl.lib`, `csl6416.lib`, `libc.a` (`rts6400.lib`) ter-link, dengan `--stack_size=0x2000` dan `--heap_size=0x2000`.
5. Jalankan `TalkThrough` singkat dulu jika perlu cek kabel `LINE IN` -> `LINE OUT` (bukan `MIC IN`) dan pastikan puncak input `<= -6 dBFS` tanpa clipping.

---

### Langkah 1 — STFT Identity Test & Kalibrasi Timer (15–35 Menit)
1. Di `main_board.c`, pastikan setting awal:
   ```c
   #define LAB_MODE       LAB_MODE_QDIG
   #define IDENTITY_TEST  1
   #define QDIG_USE_CIO   0
   ```
2. Build **Debug**, lalu **Debug (Load Program)** dan klik **Resume (F8)**.
3. Lihat hasil di Console CCS (`printf`):
   - Pastikan seluruh proses berjalan mulus (`LED1` menyala saat selesai).
4. **Kalibrasi Timer vs Profile Clock CCS:**
   - Buka `Run -> Clock -> Enable` di CCS, pasang breakpoint sebelum dan sesudah 1 pemanggilan `stft_app_process_hop_prof(...)`, bandingkan angka **Profile Clock** dengan nilai `TOTAL hop` yang dicetak `TIMER_DEV1` (`dt * 8u`). Catat konsistensinya di logbook!
5. *(Jika ingin langsung cek Identity Test dengan file `.raw` via CIO)*:
   - Di PC (PowerShell), konversi WAV ke `.raw`:
     ```powershell
     python -c "import numpy as np; from scipy.io import wavfile; fs,x=wavfile.read('data/noisy/clip_01.wav'); x.astype('<i2').tofile('clip_in.raw')"
     ```
   - Taruh `clip_in.raw` di folder kerja Debug/Release project CCS (atau isi path lengkap di `QDIG_IN_FILE`), set `#define QDIG_USE_CIO 1`, run, lalu konversi `clip_out.raw` ke WAV dan cek `SNR_q >= 40 dB`.

---

### Langkah 2 — Aktifkan Spectral Subtraction Q15 di `Q-DIG` (35–65 Menit)
1. Di `main_board.c`, ubah:
   ```c
   #define IDENTITY_TEST  0   /* aktifkan beta_q15 = 655 (0.02) & alpha = 2.0 */
   ```
2. Jika memproses klip uji test set satu per satu (`QDIG_USE_CIO = 1`):
   - Konversi `noisy/clip_XX.wav` -> `clip_in.raw`.
   - Jalankan di board -> ambil `clip_out.raw`.
   - Konversi kembali ke `results/Q-DIG/clip_XX.wav`:
     ```powershell
     python -c "import numpy as np; from scipy.io import wavfile; y=np.fromfile('clip_out.raw', dtype='<i2'); wavfile.write('results/Q-DIG/clip_XX.wav', 8000, y)"
     ```
   - Ukur `SNR_in`, `SNR_out`, `Delta_SNR`, `SNR_q` (vs `F-PY`) menggunakan `python/evaluate_metrics.py`.

---

### Langkah 3 — Build `Release -O3`, Profiling Cycles, & Ambil File `.map` (65–95 Menit)
1. Klik kanan project -> **Build Configurations -> Set Active -> Release**.
2. Pastikan di **Properties -> C6000 Compiler -> Optimization** pada konfigurasi `Release` sudah terpasang **`-O3`** (Opt Level 3).
3. **Build** ulang dan Load `.out` versi `Release`.
4. Jalankan mode `Q-DIG` dan catat angka **mean** & **max** cycles dari Console CCS untuk mengisi **Tabel 6 Laporan**:
   - `Framing + Window`
   - `FFT 256`
   - `SS (Daya + Noise + VAD + Gain + Aplikasi)`
   - `IFFT 256`
   - `OLA + SAT16`
   - `Total process_hop` -> hitung `RTF = C_frame / 16.000.000` (wajib `<< 1.0`).
5. Buka folder `Release/` di project CCS, **salin file `<nama_project>.map`**:
   - Catat bagian `SECTION ALLOCATION MAP`: ukuran byte hex `.algtext` dan `.algdata` (dibagi `1024` = KB).
   - Catat total `used` pada `L2RAM` di bagian `MEMORY CONFIGURATION`.

---

### Langkah 4 — Live Real-Time Run `Q-LIVE` & Demo Bypass vs Enhanced (95–120 Menit)
1. Di `main_board.c`, ubah:
   ```c
   #define LAB_MODE       LAB_MODE_QLIVE
   #define IDENTITY_TEST  0
   ```
2. Pastikan konfigurasi build tetap **Release (`-O3`)**, lalu Build & Load ke board -> klik **Resume (F8)**.
3. Putar audio uji dari laptop ke **LINE IN** DSK6416, dengarkan output dari **LINE OUT / HEADPHONE** (kanal L dan R mengeluarkan audio mono yang sama).
4. **Demo Bypass vs Enhanced langsung di board:**
   - Gunakan **DIP Switch SW0** di board DSK6416:
     - `SW0` posisi **DOWN (0)** = **Bypass** (audio asli tanpa pemrosesan, `LED1` nyala).
     - `SW0` posisi **UP (1)** = **Enhanced Spectral Subtraction Q15** (`LED0` nyala).
5. **Verifikasi Real-Time (`overrunCount == 0`):**
   - Pastikan `LED3` **tidak menyala**, dan cek variabel `overrunCount` di jendela **Expressions** CCS bernilai **`0`** selama seluruh klip uji diputar.
   - Rekam audio/video perbandingan *Bypass vs Enhanced* untuk demo UAS.

---

### Troubleshooting Cepat di Lab
- **Suara patah-patah / klik di `Q-LIVE`:** Cek apakah masih build `Debug -O0` (wajib pindah ke `Release -O3`), atau `overrunCount > 0`.
- **Tidak ada suara di `Q-LIVE`:** Cek kabel harus di `LINE IN` (bukan `MIC IN`), cek `LED0`/`LED1` untuk melihat posisi `SW0`, dan cek `IRQ_EVT_RINT2` tidak terblokir.
- **Channel L/R terbalik atau suara aneh:** Cek `lr_phase` pada `c_int11` atau uji dengan tone 1 kHz di kanal kiri saja.
- **Error linker `program will not fit`:** Pastikan memakai `C6416_alg.cmd` (bukan dua file `.cmd` sekaligus) dan buffer besar `clipIn`/`clipOut` berada di `.extdata > EMIFACE0` (SDRAM).
- **Perintah Simulasi PC (untuk cross-check):**
  ```powershell
  gcc -O2 -Wall -Wextra -I./c_board ./c_sim_pc/sim_pc.c ./c_board/ss.c -lm -o ./c_sim_pc/sim_pc.exe
  python ./python/ss_ref.py
  ./c_sim_pc/sim_pc.exe ./data/noisy/clip_01.wav ./results/Q15_PC/clip_01.wav
  python ./python/evaluate_metrics.py
  ```
