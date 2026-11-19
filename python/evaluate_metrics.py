"""evaluate_metrics.py -- Evaluasi SNR_in, SNR_out, Delta_SNR, SNR_q (F-PY vs Q15) & Plot Spektrogram.

Penggunaan:
    python evaluate_metrics.py
"""
import argparse
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.io import wavfile
from scipy.signal import correlate, stft
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

try:
    from pesq import pesq as _pesq_fn
except ImportError:
    _pesq_fn = None

try:
    from pystoi import stoi as _stoi_fn
except ImportError:
    _stoi_fn = None

FS = 8000
NFFT = 256
HOP = 128

ROOT_DIR = Path(__file__).resolve().parent.parent


def load_wav(path: Path) -> np.ndarray:
    """Baca file WAV 8 kHz menjadi float64 dalam rentang [-1, 1)."""
    fs, data = wavfile.read(str(path))
    if fs != FS:
        raise ValueError(f"{path} memiliki sample rate {fs} Hz (wajib {FS} Hz)")
    if data.ndim > 1:
        data = data[:, 0]
    if np.issubdtype(data.dtype, np.integer):
        return data.astype(np.float64) / 32768.0
    return data.astype(np.float64)


def align_signals(ref: np.ndarray, sig: np.ndarray, max_lag: int = 2048):
    """Sejajarkan sig terhadap ref menggunakan puncak cross-correlation."""
    c = correlate(sig, ref, mode="full", method="fft")
    mid = len(ref) - 1
    lo = max(0, mid - max_lag)
    hi = min(len(c), mid + max_lag + 1)
    lag = int(np.argmax(c[lo:hi])) + lo - mid
    if lag > 0:
        aligned = sig[lag:]
    elif lag < 0:
        aligned = np.concatenate([np.zeros(-lag), sig])
    else:
        aligned = sig
    return aligned, lag


def compute_snr(ref: np.ndarray, test: np.ndarray) -> float:
    """SNR global (dB) sesuai Persamaan (6.1) Panduan Umum."""
    num = np.sum(ref ** 2)
    den = np.sum((ref - test) ** 2) + 1e-12
    return float(10.0 * np.log10((num + 1e-12) / den))


def compute_segsnr(
    ref: np.ndarray, test: np.ndarray, frame_len: int = NFFT, hop: int = HOP, lo: float = -10.0, hi: float = 35.0
) -> float:
    """Segmental SNR (dB) sesuai Persamaan (6.3) Panduan Umum."""
    vals = []
    for i in range(0, len(ref) - frame_len + 1, hop):
        s_seg = ref[i : i + frame_len]
        e_seg = s_seg - test[i : i + frame_len]
        seg_val = 10.0 * np.log10((np.sum(s_seg ** 2) + 1e-12) / (np.sum(e_seg ** 2) + 1e-12))
        vals.append(np.clip(seg_val, lo, hi))
    return float(np.mean(vals)) if vals else 0.0


def save_spectrogram_plot(
    clip_id: str,
    clean: np.ndarray,
    noisy: np.ndarray,
    fpy: np.ndarray,
    q15: np.ndarray,
    out_path: Path,
) -> None:
    """Simpan plot perbandingan spektrogram Clean, Noisy, F-PY, dan Q15."""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    signals = [
        ("Clean (Referensi)", clean),
        ("Noisy (Input 8 kHz)", noisy),
        ("F-PY (Float Python)", fpy),
        ("Q15 (C Fixed-Point)", q15),
    ]

    specs = []
    times = freqs = None
    for _, sig in signals:
        freqs, times, Zxx = stft(sig, fs=FS, window="hann", nperseg=NFFT, noverlap=NFFT - HOP)
        mag_db = 20.0 * np.log10(np.maximum(np.abs(Zxx), 1e-6))
        specs.append(mag_db)

    vmax = max(np.max(sp) for sp in specs)
    vmin = vmax - 70.0

    fig, axes = plt.subplots(2, 2, figsize=(12, 7), sharex=True, sharey=True, constrained_layout=True)
    for ax, (title, _), sp in zip(axes.ravel(), signals, specs):
        im = ax.pcolormesh(times, freqs, sp, shading="gouraud", cmap="magma", vmin=vmin, vmax=vmax)
        ax.set_title(title, fontsize=11, fontweight="bold")
        ax.set_ylabel("Frekuensi (Hz)")
        ax.set_xlabel("Waktu (s)")

    fig.colorbar(im, ax=axes, label="Magnitudo (dB)", shrink=0.92)
    fig.suptitle(f"Perbandingan Spektrogram Spectral Subtraction -- {clip_id}", fontsize=13, fontweight="bold")
    fig.savefig(str(out_path), dpi=160)
    plt.close(fig)


def evaluate_all(
    clean_dir: Path,
    noisy_dir: Path,
    fpy_dir: Path,
    q15_dir: Path,
    id_dir: Path,
    plots_dir: Path,
    csv_out: Path,
    skip_plots: bool = False,
) -> pd.DataFrame:
    rows = []
    clean_files = sorted(clean_dir.glob("*.wav"))
    if not clean_files:
        raise FileNotFoundError(f"Tidak ada file WAV clean di {clean_dir}")

    for cp in clean_files:
        cid = cp.stem
        s = load_wav(cp)
        y = load_wav(noisy_dir / cp.name)
        s_fpy = load_wav(fpy_dir / cp.name)
        s_q15_raw = load_wav(q15_dir / cp.name)

        s_q15, lag_q15 = align_signals(s_fpy, s_q15_raw)
        L = min(len(s), len(y), len(s_fpy), len(s_q15))
        s_c, y_c, fpy_c, q15_c = s[:L], y[:L], s_fpy[:L], s_q15[:L]

        snr_in = compute_snr(s_c, y_c)
        segsnr_in = compute_segsnr(s_c, y_c)
        snr_q = compute_snr(fpy_c, q15_c)

        # Cek hasil Identity Test Q15 bila tersedia
        id_snr = np.nan
        id_path = id_dir / cp.name
        if id_path.exists():
            id_raw = load_wav(id_path)
            id_aligned, _ = align_signals(y, id_raw)
            Lid = min(len(y), len(id_aligned))
            id_snr = compute_snr(y[:Lid], id_aligned[:Lid])

        for cond_name, est, lag_val in [("F-PY", fpy_c, 0), ("Q15", q15_c, lag_q15)]:
            snr_out = compute_snr(s_c, est)
            segsnr_out = compute_segsnr(s_c, est)
            row = {
                "clip_id": cid,
                "cond": cond_name,
                "lag": lag_val,
                "SNR_in": snr_in,
                "SNR_out": snr_out,
                "Delta_SNR": snr_out - snr_in,
                "SegSNR_in": segsnr_in,
                "SegSNR_out": segsnr_out,
                "SNR_q_vs_FPY": np.inf if cond_name == "F-PY" else snr_q,
                "Identity_SNR_dB": id_snr if cond_name == "Q15" else np.nan,
            }
            if _pesq_fn is not None:
                row["PESQ_in"] = float(_pesq_fn(FS, s_c, y_c, "nb"))
                row["PESQ_out"] = float(_pesq_fn(FS, s_c, est, "nb"))
            if _stoi_fn is not None:
                row["STOI_in"] = float(_stoi_fn(s_c, y_c, FS, extended=False))
                row["STOI_out"] = float(_stoi_fn(s_c, est, FS, extended=False))
            rows.append(row)

        if not skip_plots:
            plot_file = plots_dir / f"spectrogram_{cid}.png"
            save_spectrogram_plot(cid, s_c, y_c, fpy_c, q15_c, plot_file)
            print(f"[PLOT] Spektrogram disimpan: {plot_file}")

    df = pd.DataFrame(rows)
    csv_out.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(csv_out, index=False)

    print("\n=== Ringkasan Metrik Evaluasi ===")
    print(df.to_string(index=False, float_format=lambda v: f"{v:.3f}"))
    print(f"\n[OK] CSV disimpan ke {csv_out}")
    return df


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluasi Metrik & Spektrogram Kelompok A")
    parser.add_argument("--clean-dir", type=Path, default=ROOT_DIR / "data" / "clean")
    parser.add_argument("--noisy-dir", type=Path, default=ROOT_DIR / "data" / "noisy")
    parser.add_argument("--fpy-dir", type=Path, default=ROOT_DIR / "results" / "F-PY")
    parser.add_argument("--q15-dir", type=Path, default=ROOT_DIR / "results" / "Q15_PC")
    parser.add_argument("--id-dir", type=Path, default=ROOT_DIR / "results" / "Q15_ID")
    parser.add_argument("--plots-dir", type=Path, default=ROOT_DIR / "results" / "plots")
    parser.add_argument("--csv-out", type=Path, default=ROOT_DIR / "results" / "metrics_summary.csv")
    parser.add_argument("--skip-plots", action="store_true", help="Lewati pembuatan ulang plot PNG")
    args = parser.parse_args()

    evaluate_all(
        clean_dir=args.clean_dir,
        noisy_dir=args.noisy_dir,
        fpy_dir=args.fpy_dir,
        q15_dir=args.q15_dir,
        id_dir=args.id_dir,
        plots_dir=args.plots_dir,
        csv_out=args.csv_out,
        skip_plots=args.skip_plots,
    )


if __name__ == "__main__":
    main()
