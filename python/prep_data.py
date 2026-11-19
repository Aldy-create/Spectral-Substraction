"""prep_data.py -- Generate hann_win.h (Q15) & sinyal uji sintetis 8 kHz (clean & noisy).

Penggunaan:
    python prep_data.py
"""
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

FS = 8000
N = 256
H = 128

ROOT_DIR = Path(__file__).resolve().parent.parent
HANN_HEADER_PATH = ROOT_DIR / "c_board" / "hann_win.h"
DATA_CLEAN_DIR = ROOT_DIR / "data" / "clean"
DATA_NOISY_DIR = ROOT_DIR / "data" / "noisy"

# Window Hann periodik (memenuhi syarat COLA untuk overlap 50%, H = N/2)
WIN = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(N) / N)


def write_wav_int16(path: Path, x: np.ndarray, fs: int = FS) -> None:
    """Simpan array float [-1, 1) ke WAV PCM 16-bit mono."""
    path.parent.mkdir(parents=True, exist_ok=True)
    x_i16 = np.clip(np.round(x * 32768.0), -32768, 32767).astype(np.int16)
    wavfile.write(str(path), fs, x_i16)


def export_hann_header(path: Path = HANN_HEADER_PATH) -> None:
    """Bangkitkan file header C const short HANN_WIN_Q15[256]."""
    path.parent.mkdir(parents=True, exist_ok=True)
    win_q15 = np.clip(np.round(WIN * 32768.0), -32768, 32767).astype(np.int16)

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* hann_win.h -- Window Hann periodik 256-titik Q15 (auto-generated) */\n")
        f.write("#ifndef HANN_WIN_H\n")
        f.write("#define HANN_WIN_H\n\n")
        f.write(f"const short HANN_WIN_Q15[{N}] = {{\n")
        for i in range(0, N, 8):
            row = ", ".join(f"{int(val):6d}" for val in win_q15[i : i + 8])
            comma = "," if (i + 8) < N else ""
            f.write(f"    {row}{comma}\n")
        f.write("};\n\n")
        f.write("#endif /* HANN_WIN_H */\n")

    cola_float_err = np.max(np.abs((WIN[:H] + WIN[H:]) - 1.0))
    cola_q15_vals = win_q15[:H].astype(np.int32) + win_q15[H:].astype(np.int32)
    print(f"[OK] {path} dibuat | COLA float err={cola_float_err:.2e}, "
          f"Q15 sum min/max={cola_q15_vals.min()}/{cola_q15_vals.max()}")


def synthesize_speech_like(duration_s: float = 3.0, seed: int = 42) -> np.ndarray:
    """Buat sinyal uji mirip ucapan (speech-like) 8 kHz dengan jeda awal >= 0.3 s.

    Struktur:
      - 0.00 - 0.35 s : hening (untuk inisialisasi noise VAD n_init = 6 frame = 96 ms)
      - 0.35 - 1.25 s : segmen voiced harmonik + formant
      - 1.25 - 1.55 s : jeda antar kata
      - 1.55 - 2.65 s : segmen voiced harmonik berbeda pitch + transien konsonan
      - 2.65 - 3.00 s : hening akhir
    """
    rng = np.random.default_rng(seed)
    total_samples = int(duration_s * FS)
    t = np.arange(total_samples) / FS
    clean = np.zeros(total_samples, dtype=np.float64)

    def voiced_burst(t_seg: np.ndarray, f0_start: float, f0_end: float) -> np.ndarray:
        f0 = np.linspace(f0_start, f0_end, len(t_seg))
        phase = 2.0 * np.pi * np.cumsum(f0) / FS
        sig = np.zeros_like(t_seg)
        for h in range(1, 14):
            # Penurunan spektral alami harmonik + penekanan formant sederhana
            freq = h * np.mean(f0)
            if freq >= 0.45 * FS:
                break
            weight = (1.0 / (h ** 1.15)) * (1.0 + 0.8 * np.exp(-((freq - 650.0) / 260.0) ** 2))
            sig += weight * np.sin(h * phase)
        # Envelope halus (attack/decay) agar bebas klik
        env = np.sin(np.pi * np.arange(len(t_seg)) / len(t_seg)) ** 0.45
        mod = 0.65 + 0.35 * np.sin(2.0 * np.pi * 4.5 * t_seg)
        return sig * env * mod

    # Segmen 1: 0.35 s .. 1.25 s
    i1_s, i1_e = int(0.35 * FS), int(1.25 * FS)
    clean[i1_s:i1_e] = voiced_burst(t[i1_s:i1_e] - t[i1_s], 135.0, 165.0)

    # Segmen 2: 1.55 s .. 2.65 s (ditambah sedikit komponen frikatif band-pass)
    i2_s, i2_e = int(1.55 * FS), int(2.65 * FS)
    v2 = voiced_burst(t[i2_s:i2_e] - t[i2_s], 170.0, 125.0)
    fric_raw = rng.standard_normal(int(0.12 * FS))
    fric = lfilter([0.35, -0.35], [1.0, 0.45], fric_raw)
    fric *= np.hanning(len(fric)) * 0.25
    clean[i2_s:i2_e] = v2
    clean[i2_s : i2_s + len(fric)] += fric

    # Normalisasi ke puncak <= -7 dBFS (~0.44) agar ada headroom Q15 untuk penambahan noise
    peak = np.max(np.abs(clean))
    if peak > 0:
        clean = clean * (0.42 / peak)
    return clean


def add_noise_at_snr(clean: np.ndarray, snr_db: float, noise_type: str, seed: int) -> np.ndarray:
    """Campur sinyal bersih dengan noise stasioner/berwarna pada SNR global tertentu."""
    rng = np.random.default_rng(seed)
    raw = rng.standard_normal(len(clean))
    if noise_type == "colored":
        # Low-pass IIR sederhana menyerupai dengung/kipas ruangan
        noise = lfilter([1.0], [1.0, -0.78], raw)
    else:
        noise = raw

    # Hitung daya speech hanya pada segmen aktif agar SNR realistis & konsisten
    speech_power = np.mean(clean ** 2)
    noise_power = np.mean(noise ** 2)
    target_noise_power = speech_power / (10.0 ** (snr_db / 10.0))
    scaled_noise = noise * np.sqrt(target_noise_power / max(noise_power, 1e-15))
    noisy = clean + scaled_noise

    # Pastikan tidak clipping di Q15
    max_abs = np.max(np.abs(noisy))
    if max_abs > 0.85:
        scale = 0.85 / max_abs
        return noisy * scale, clean * scale
    return noisy, clean


def verify_python_stft_identity() -> None:
    """Verifikasi cepat COLA STFT identity test di Python."""
    t = np.arange(FS) / FS
    sig = 0.4 * np.sin(2.0 * np.pi * 440.0 * t)
    x_pad = np.concatenate([np.zeros(H), sig, np.zeros(N)])
    y = np.zeros(len(x_pad) + N)
    for i in range(0, len(x_pad) - N + 1, H):
        X = np.fft.rfft(WIN * x_pad[i : i + N])
        y[i : i + N] += np.fft.irfft(X, N)
    rec = y[H : H + len(sig)]
    err = np.max(np.abs(sig - rec))
    print(f"[OK] Python STFT Identity Test max error: {err:.2e}")


def main() -> None:
    export_hann_header(HANN_HEADER_PATH)
    verify_python_stft_identity()

    # Clip 01: SNR ~5 dB, noise berwarna (dengung/ruangan)
    clean_01 = synthesize_speech_like(duration_s=3.0, seed=101)
    noisy_01, clean_01 = add_noise_at_snr(clean_01, snr_db=5.0, noise_type="colored", seed=201)
    write_wav_int16(DATA_CLEAN_DIR / "clip_01.wav", clean_01)
    write_wav_int16(DATA_NOISY_DIR / "clip_01.wav", noisy_01)

    # Clip 02: SNR ~10 dB, white noise
    clean_02 = synthesize_speech_like(duration_s=3.0, seed=102)
    noisy_02, clean_02 = add_noise_at_snr(clean_02, snr_db=10.0, noise_type="white", seed=202)
    write_wav_int16(DATA_CLEAN_DIR / "clip_02.wav", clean_02)
    write_wav_int16(DATA_NOISY_DIR / "clip_02.wav", noisy_02)

    print(f"[OK] Sinyal uji 8 kHz dibuat di {DATA_CLEAN_DIR} & {DATA_NOISY_DIR}")


if __name__ == "__main__":
    main()
