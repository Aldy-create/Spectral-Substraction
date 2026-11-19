"""ss_ref.py -- Referensi floating-point (F-PY) Spectral Subtraction.

Framework STFT identik dengan implementasi C di board/PC:
  fs = 8000 Hz, N = 256, H = 128, Hann periodik (COLA), OLA tanpa window sintesis.
"""
import argparse
from pathlib import Path
import numpy as np
from scipy.io import wavfile

FS = 8000
N = 256
H = 128
WIN = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(N) / N)

ROOT_DIR = Path(__file__).resolve().parent.parent


def read_wav_float(path: Path) -> np.ndarray:
    """Baca WAV 8 kHz 16-bit PCM menjadi float64 dalam rentang [-1, 1)."""
    fs, data = wavfile.read(str(path))
    if fs != FS:
        raise ValueError(f"Sample rate {path} adalah {fs} Hz, wajib {FS} Hz.")
    if data.ndim > 1:
        data = data[:, 0]
    if np.issubdtype(data.dtype, np.integer):
        return data.astype(np.float64) / 32768.0
    return data.astype(np.float64)


def write_wav_int16(path: Path, x: np.ndarray, fs: int = FS) -> None:
    """Simpan sinyal float ke WAV PCM 16-bit."""
    path.parent.mkdir(parents=True, exist_ok=True)
    x_i16 = np.clip(np.round(x * 32768.0), -32768, 32767).astype(np.int16)
    wavfile.write(str(path), fs, x_i16)


def stft_process(x: np.ndarray, gain_fn) -> np.ndarray:
    """Framework STFT/OLA bersama: pad H di awal -> frame N/hop H -> Hann -> rfft -> gain -> irfft -> OLA."""
    orig_len = len(x)
    x_pad = np.concatenate([np.zeros(H), x, np.zeros(N)])
    y = np.zeros(len(x_pad) + N, dtype=np.float64)

    for m, i in enumerate(range(0, len(x_pad) - N + 1, H)):
        X = np.fft.rfft(WIN * x_pad[i : i + N])
        G = gain_fn(np.abs(X) ** 2, m)
        y[i : i + N] += np.fft.irfft(G * X, N)

    return y[H : H + orig_len]


class SpectralSubtraction:
    """Power Spectral Subtraction (Persamaan 2.4 - 2.6 Panduan Kelompok A)."""

    def __init__(
        self,
        alpha: float = 2.0,
        beta: float = 0.02,
        lam: float = 0.9375,
        vad_db: float = 3.0,
        n_init: int = 6,
    ) -> None:
        self.alpha = alpha
        self.beta = beta
        self.lam = lam
        self.T = 10.0 ** (vad_db / 10.0)
        self.n_init = n_init
        self.PN = None
        self.vad_history = []

    def gain(self, PY: np.ndarray, m: int) -> np.ndarray:
        if self.PN is None:
            self.PN = PY.copy()

        if m < self.n_init:
            self.PN = (m * self.PN + PY) / (m + 1)
            is_speech = False
        elif PY.sum() < self.T * self.PN.sum():
            self.PN = self.lam * self.PN + (1.0 - self.lam) * PY
            is_speech = False
        else:
            is_speech = True

        self.vad_history.append(1 if is_speech else 0)
        G2 = 1.0 - self.alpha * self.PN / np.maximum(PY, 1e-12)
        G2 = np.maximum(G2, self.beta)
        return np.sqrt(G2)


def run_batch(
    noisy_dir: Path,
    out_dir: Path,
    alpha: float = 2.0,
    beta: float = 0.02,
    lam: float = 0.9375,
    vad_db: float = 3.0,
    n_init: int = 6,
    identity: bool = False,
) -> None:
    wav_files = sorted(noisy_dir.glob("*.wav"))
    if not wav_files:
        raise FileNotFoundError(f"Tidak ada file .wav di {noisy_dir}")

    for wp in wav_files:
        y = read_wav_float(wp)
        if identity:
            s_hat = stft_process(y, lambda P, m: 1.0)
            speech_pct = 0.0
        else:
            ss = SpectralSubtraction(
                alpha=alpha, beta=beta, lam=lam, vad_db=vad_db, n_init=n_init
            )
            s_hat = stft_process(y, ss.gain)
            speech_pct = 100.0 * np.mean(ss.vad_history) if ss.vad_history else 0.0

        out_path = out_dir / wp.name
        write_wav_int16(out_path, s_hat)
        print(
            f"[F-PY] {wp.name} -> {out_path} | "
            f"samples={len(y)}, frames={int(np.ceil((len(y) + H) / H))}, "
            f"VAD speech={speech_pct:.1f}%"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description="Referensi Float Spectral Subtraction (F-PY)")
    parser.add_argument("--noisy-dir", type=Path, default=ROOT_DIR / "data" / "noisy")
    parser.add_argument("--out-dir", type=Path, default=ROOT_DIR / "results" / "F-PY")
    parser.add_argument("--alpha", type=float, default=2.0)
    parser.add_argument("--beta", type=float, default=0.02)
    parser.add_argument("--lam", type=float, default=0.9375)
    parser.add_argument("--vad-db", type=float, default=3.0)
    parser.add_argument("--n-init", type=int, default=6)
    parser.add_argument("--identity", action="store_true", help="Uji STFT identity (G = 1)")
    args = parser.parse_args()

    # Selalu cek identity STFT cepat terlebih dahulu
    t = np.arange(FS) / FS
    test_sig = 0.4 * np.sin(2.0 * np.pi * 440.0 * t)
    test_rec = stft_process(test_sig, lambda P, m: 1.0)
    print(f"[CHECK] Python STFT identity max err = {np.max(np.abs(test_sig - test_rec)):.2e}")

    run_batch(
        noisy_dir=args.noisy_dir,
        out_dir=args.out_dir,
        alpha=args.alpha,
        beta=args.beta,
        lam=args.lam,
        vad_db=args.vad_db,
        n_init=args.n_init,
        identity=args.identity,
    )


if __name__ == "__main__":
    main()
