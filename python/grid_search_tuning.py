"""grid_search_tuning.py -- Eksplorasi Grid Tuning & Heatmap PESQ(alpha, beta) Kelompok A.

Sesuai Panduan Kelompok A Bagian 5:
  - Grid minimal: alpha (4 nilai) x beta (4 nilai) x estimator (2) = 32 konfigurasi.
  - Dilakukan pada VALIDATION SET (disjoint dari 30 test clip).
  - Menghitung rata-rata PESQ-NB, Delta_SNR, dan STOI per konfigurasi.
  - Menghasilkan heatmap PESQ(alpha, beta) dan menyimpan ke results/plots/.
  - Memilih konfigurasi optimal berdasarkan PESQ tertinggi.
"""
import argparse
import os
import zipfile
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.io import wavfile
from scipy.signal import resample_poly
from pesq import pesq
from pystoi import stoi
import matplotlib.pyplot as plt

FS = 8000
N = 256
H = 128
NBIN = 129
WIN = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(N) / N)

ROOT_DIR = Path(r"D:\Spectral")
VAL_CLEAN_DIR = ROOT_DIR / "data" / "val_clean"
VAL_NOISY_DIR = ROOT_DIR / "data" / "val_noisy"
CLEAN_ZIP = Path(r"D:\Workshop VOICEBANK\clean_testset_wav.zip")
NOISY_ZIP = Path(r"D:\Workshop VOICEBANK\noisy_testset_wav.zip")


def prepare_validation_set(n_clips: int = 10) -> None:
    """Ekstrak klip validasi yang sama sekali berbeda dari 30 test klip."""
    VAL_CLEAN_DIR.mkdir(parents=True, exist_ok=True)
    VAL_NOISY_DIR.mkdir(parents=True, exist_ok=True)

    existing = list(VAL_CLEAN_DIR.glob("*.wav"))
    if len(existing) >= n_clips:
        print(f"[INFO] Validation set sudah ada ({len(existing)} klip) di {VAL_CLEAN_DIR}")
        return

    # Ambil daftar file test agar tidak bentrok
    test_meta_path = ROOT_DIR / "data" / "testset_meta.csv"
    test_files = set()
    if test_meta_path.exists():
        df_test = pd.read_csv(test_meta_path)
        test_files = set(df_test["orig_file"].tolist())

    with zipfile.ZipFile(CLEAN_ZIP) as zc, zipfile.ZipFile(NOISY_ZIP) as zn:
        all_clean = [Path(p).name for p in zc.namelist() if p.endswith(".wav")]
        all_noisy = set([Path(p).name for p in zn.namelist() if p.endswith(".wav")])
        valid_candidates = [f for f in all_clean if f in all_noisy and f not in test_files]
        val_files = valid_candidates[:n_clips]

        print(f"[INFO] Mengekstrak {len(val_files)} klip untuk validation set (dari {len(valid_candidates)} kandidat)...")
        for idx, vf in enumerate(val_files, 1):
            with zc.open(f"clean_testset_wav/{vf}") as fc, zn.open(f"noisy_testset_wav/{vf}") as fn:
                fs_c, s = wavfile.read(fc)
                fs_n, y = wavfile.read(fn)

            if s.ndim > 1: s = s[:, 0]
            if y.ndim > 1: y = y[:, 0]
            L = min(len(s), len(y))
            s = s[:L].astype(np.float64) / 32768.0
            y = y[:L].astype(np.float64) / 32768.0

            s8 = resample_poly(s, 1, 6)
            y8 = resample_poly(y, 1, 6)
            L8 = min(len(s8), len(y8))
            s8 = s8[:L8]
            y8 = y8[:L8]

            s_i16 = np.clip(np.round(s8 * 32768.0), -32768, 32767).astype(np.int16)
            y_i16 = np.clip(np.round(y8 * 32768.0), -32768, 32767).astype(np.int16)

            wavfile.write(str(VAL_CLEAN_DIR / f"val_{idx:02d}.wav"), FS, s_i16)
            wavfile.write(str(VAL_NOISY_DIR / f"val_{idx:02d}.wav"), FS, y_i16)

    print(f"[OK] Validation set ({len(val_files)} klip) siap di {VAL_CLEAN_DIR}")


class SpectralSubtractionPython:
    def __init__(self, alpha=2.0, beta=0.02, estimator="vad", lam=0.9375, vad_db=3.0, n_init=6):
        self.alpha = alpha
        self.beta = beta
        self.estimator = estimator
        self.lam = lam
        self.T = 10.0 ** (vad_db / 10.0)
        self.n_init = n_init
        self.PN = None
        
        # State Minimum Statistics (U=8, V=8 -> ~1.02s)
        self.U = 8
        self.V = 8
        self.alpha_s = 0.875
        self.B_min = 1.8
        self.P_smooth = None
        self.P_curmin = None
        self.P_submin = None
        self.ms_sub_cnt = 0
        self.ms_sub_idx = 0

    def gain(self, PY: np.ndarray, m: int) -> np.ndarray:
        if self.estimator == "vad":
            if self.PN is None:
                self.PN = PY.copy()
            if m < self.n_init:
                self.PN = (m * self.PN + PY) / (m + 1)
            elif PY.sum() < self.T * self.PN.sum():
                self.PN = self.lam * self.PN + (1.0 - self.lam) * PY
        else: # minstat
            if self.P_smooth is None:
                self.P_smooth = PY.copy()
                self.P_curmin = PY.copy()
                self.P_submin = np.tile(PY.copy(), (self.U, 1))
                self.PN = PY.copy() * self.B_min
                self.ms_sub_cnt = 1
                self.ms_sub_idx = 0
            else:
                self.P_smooth = self.alpha_s * self.P_smooth + (1.0 - self.alpha_s) * PY
                self.P_curmin = np.minimum(self.P_curmin, self.P_smooth)
                mval = np.minimum(self.P_curmin, np.min(self.P_submin, axis=0))
                self.PN = mval * self.B_min

                if self.ms_sub_cnt >= self.V:
                    self.P_submin[self.ms_sub_idx] = self.P_curmin.copy()
                    self.P_curmin = self.P_smooth.copy()
                    self.ms_sub_idx = (self.ms_sub_idx + 1) % self.U
                    self.ms_sub_cnt = 1
                else:
                    self.ms_sub_cnt += 1

        G2 = 1.0 - self.alpha * self.PN / np.maximum(PY, 1e-12)
        G2 = np.maximum(G2, self.beta)
        return np.sqrt(G2)


def stft_enhance(x: np.ndarray, ss_obj: SpectralSubtractionPython) -> np.ndarray:
    orig_len = len(x)
    x_pad = np.concatenate([np.zeros(H), x, np.zeros(N)])
    y = np.zeros(len(x_pad) + N, dtype=np.float64)

    for m, i in enumerate(range(0, len(x_pad) - N + 1, H)):
        X = np.fft.rfft(WIN * x_pad[i : i + N])
        G = ss_obj.gain(np.abs(X) ** 2, m)
        y[i : i + N] += np.fft.irfft(G * X, N)

    return y[H : H + orig_len]


def snr_db(s: np.ndarray, x: np.ndarray) -> float:
    return float(10.0 * np.log10((np.sum(s ** 2) + 1e-12) / (np.sum((s - x) ** 2) + 1e-12)))


def run_grid_search():
    prepare_validation_set(n_clips=10)

    clean_files = sorted(VAL_CLEAN_DIR.glob("*.wav"))
    noisy_files = sorted(VAL_NOISY_DIR.glob("*.wav"))
    assert len(clean_files) == len(noisy_files) and len(clean_files) > 0

    val_pairs = []
    for cf, nf in zip(clean_files, noisy_files):
        _, s = wavfile.read(str(cf))
        _, y = wavfile.read(str(nf))
        s = s.astype(np.float64) / 32768.0
        y = y.astype(np.float64) / 32768.0
        val_pairs.append((s, y))

    alphas = [1.0, 2.0, 3.0, 4.0]
    betas = [0.005, 0.02, 0.05, 0.1]
    estimators = ["vad", "minstat"]

    results = []
    total_cfg = len(alphas) * len(betas) * len(estimators)
    cfg_idx = 0

    print(f"\n[INFO] Memulai Grid Search: {total_cfg} Konfigurasi pada {len(val_pairs)} klip validasi...")
    print(f"{'No':<4} | {'Est':<7} | {'Alpha':<6} | {'Beta':<7} | {'PESQ Mean':<10} | {'DeltaSNR':<10} | {'STOI Mean':<10}")
    print("-" * 65)

    for est in estimators:
        for a in alphas:
            for b in betas:
                cfg_idx += 1
                pesq_list = []
                dsnr_list = []
                stoi_list = []

                for s, y in val_pairs:
                    ss = SpectralSubtractionPython(alpha=a, beta=b, estimator=est)
                    sh = stft_enhance(y, ss)
                    sh = np.clip(sh, -1.0, 1.0)

                    # PESQ-NB
                    score_p = pesq(FS, s, sh, "nb")
                    score_s = stoi(s, sh, FS, extended=False)
                    d_snr = snr_db(s, sh) - snr_db(s, y)

                    pesq_list.append(score_p)
                    dsnr_list.append(d_snr)
                    stoi_list.append(score_s)

                p_mean, p_std = np.mean(pesq_list), np.std(pesq_list)
                d_mean, d_std = np.mean(dsnr_list), np.std(dsnr_list)
                s_mean, s_std = np.mean(stoi_list), np.std(stoi_list)

                results.append({
                    "estimator": est,
                    "alpha": a,
                    "beta": b,
                    "pesq_mean": round(p_mean, 4),
                    "pesq_std": round(p_std, 4),
                    "dsnr_mean": round(d_mean, 3),
                    "dsnr_std": round(d_std, 3),
                    "stoi_mean": round(s_mean, 4),
                    "stoi_std": round(s_std, 4),
                })

                print(f"{cfg_idx:02d}/32 | {est:<7} | {a:<6.1f} | {b:<7.3f} | {p_mean:<10.4f} | {d_mean:<+10.3f} | {s_mean:<10.4f}")

    df_res = pd.DataFrame(results)
    out_csv = ROOT_DIR / "results" / "grid_search_val.csv"
    df_res.to_csv(out_csv, index=False)
    print(f"\n[OK] Hasil grid search disimpan ke {out_csv}")

    # Plot Heatmap PESQ(alpha, beta)
    plot_heatmaps(df_res, alphas, betas)


def plot_heatmaps(df: pd.DataFrame, alphas, betas):
    plots_dir = ROOT_DIR / "results" / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)
    out_plot = plots_dir / "heatmap_pesq_alpha_beta.png"

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5), sharey=True)

    estimators = [("vad", "VAD + Recursive Avg (Default)"), ("minstat", "Minimum Statistics (Martin)")]

    for ax, (est, title) in zip(axes, estimators):
        sub = df[df["estimator"] == est]
        mat = np.zeros((len(betas), len(alphas)))
        
        for i, b in enumerate(betas):
            for j, a in enumerate(alphas):
                val = sub[(sub["alpha"] == a) & (sub["beta"] == b)]["pesq_mean"].values[0]
                mat[i, j] = val

        im = ax.imshow(mat, cmap="viridis", origin="lower", aspect="auto")
        ax.set_xticks(range(len(alphas)))
        ax.set_xticklabels([f"{a:.1f}" for a in alphas], fontsize=11)
        ax.set_yticks(range(len(betas)))
        ax.set_yticklabels([f"{b:.3f}" for b in betas], fontsize=11)
        ax.set_xlabel(r"Over-subtraction Factor $\alpha$", fontsize=12, fontweight="bold")
        if ax == axes[0]:
            ax.set_ylabel(r"Spectral Floor $\beta$ (on $G^2$)", fontsize=12, fontweight="bold")
        ax.set_title(title, fontsize=13, fontweight="bold", pad=10)

        # Cari nilai max di matriks ini
        max_idx = np.unravel_index(np.argmax(mat), mat.shape)

        for i in range(len(betas)):
            for j in range(len(alphas)):
                val = mat[i, j]
                is_opt = (i == max_idx[0] and j == max_idx[1])
                color = "red" if is_opt else ("black" if val > np.mean(mat) else "white")
                weight = "bold" if is_opt else "normal"
                txt = f"{val:.3f}" + (" *" if is_opt else "")
                ax.text(j, i, txt, ha="center", va="center", color=color, fontsize=10, fontweight=weight)

        cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cbar.set_label("PESQ-NB Score", fontsize=11)

    fig.suptitle(r"Eksplorasi Grid Tuning: Heatmap PESQ($\alpha$, $\beta$) pada Validation Set",
                 fontsize=14, fontweight="bold", y=0.98)
    plt.tight_layout()
    plt.savefig(out_plot, dpi=300)
    plt.close()
    print(f"[OK] Heatmap PESQ tersimpan di: {out_plot}")


if __name__ == "__main__":
    run_grid_search()
