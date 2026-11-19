"""resample_voicebank.py -- Pilih 30 klip representatif VoiceBank-DEMAND & resample 48 kHz -> 8 kHz.

Sesuai Panduan Umum Bagian 5.1 & 5.3:
  - Resample 48 kHz -> 8 kHz memakai scipy.signal.resample_poly(x, 1, 6)
  - Menyimpan data/clean/clip_01.wav .. clip_30.wav (8 kHz 16-bit mono)
  - Menyimpan data/noisy/clip_01.wav .. clip_30.wav (8 kHz 16-bit mono)
  - Menyimpan data/raw_in/clip_01.raw .. clip_30.raw (int16 little-endian untuk mode Q-DIG CCS)
  - Mencatat metadata asal file, SNR_in, durasi, & durasi jeda awal di data/testset_meta.csv
  - Membuat data/SHA256SUMS.txt untuk verifikasi integritas test set
"""
import argparse
import hashlib
from pathlib import Path
import numpy as np
import pandas as pd
from scipy.io import wavfile
from scipy.signal import resample_poly

ROOT_DIR = Path(__file__).resolve().parent.parent
CLEAN_48K = ROOT_DIR / "data" / "clean_48k"
NOISY_48K = ROOT_DIR / "data" / "noisy_48k"
CLEAN_8K = ROOT_DIR / "data" / "clean"
NOISY_8K = ROOT_DIR / "data" / "noisy"
RAW_IN_DIR = ROOT_DIR / "data" / "raw_in"


def read_wav_f64(path: Path):
    fs, data = wavfile.read(str(path))
    if data.ndim > 1:
        data = data[:, 0]
    if np.issubdtype(data.dtype, np.integer):
        return fs, data.astype(np.float64) / 32768.0
    return fs, data.astype(np.float64)


def compute_snr(s: np.ndarray, y: np.ndarray) -> float:
    return float(10.0 * np.log10((np.sum(s ** 2) + 1e-12) / (np.sum((s - y) ** 2) + 1e-12)))


def estimate_lead_in_ms(clean_8k: np.ndarray, fs: int = 8000) -> float:
    """Estimasi durasi jeda awal (ms) sebelum energi ucapan aktif muncul."""
    peak = np.max(np.abs(clean_8k))
    if peak < 1e-9:
        return 0.0
    thresh = 0.02 * peak
    active = np.where(np.abs(clean_8k) > thresh)[0]
    if len(active) == 0:
        return len(clean_8k) * 1000.0 / fs
    return float(active[0] * 1000.0 / fs)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def select_balanced_clips(pairs, n_total: int = 30):
    """Pilih subset seimbang dari speaker p232 dan p257 lintas rentang SNR."""
    records = []
    for cp, np_path in pairs:
        fs_c, s = read_wav_f64(cp)
        fs_n, y = read_wav_f64(np_path)
        if fs_c != 48000 or fs_n != 48000:
            continue
        L = min(len(s), len(y))
        snr_val = compute_snr(s[:L], y[:L])
        speaker = cp.stem.split("_")[0]
        records.append({
            "name": cp.name,
            "clean_path": cp,
            "noisy_path": np_path,
            "speaker": speaker,
            "snr_48k": snr_val,
            "dur_s": L / 48000.0,
        })

    df = pd.DataFrame(records)
    # Filter durasi 1.5 - 6.0 detik agar nyaman untuk Q-DIG & Q-LIVE
    df = df[(df["dur_s"] >= 1.5) & (df["dur_s"] <= 6.0)].copy()

    selected = []
    speakers = sorted(df["speaker"].unique())
    per_spk = max(1, n_total // max(1, len(speakers)))

    for spk in speakers:
        sub = df[df["speaker"] == spk].sort_values("snr_48k").reset_index(drop=True)
        if len(sub) <= per_spk:
            selected.append(sub)
        else:
            idx = np.linspace(0, len(sub) - 1, per_spk, dtype=int)
            selected.append(sub.iloc[idx])

    sel_df = pd.concat(selected, ignore_index=True)
    if len(sel_df) > n_total:
        sel_df = sel_df.iloc[:n_total]
    return sel_df.sort_values(["speaker", "snr_48k"]).reset_index(drop=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Resample VoiceBank-DEMAND 48 kHz -> 8 kHz (subset test)")
    parser.add_argument("--n-clips", type=int, default=30, help="Jumlah klip subset (default: 30)")
    args = parser.parse_args()

    clean_files = sorted(CLEAN_48K.glob("*.wav"))
    pairs = []
    for cp in clean_files:
        np_path = NOISY_48K / cp.name
        if np_path.exists():
            pairs.append((cp, np_path))

    if not pairs:
        raise FileNotFoundError(f"Tidak ada pasangan WAV di {CLEAN_48K} & {NOISY_48K}")

    print(f"[INFO] Ditemukan {len(pairs)} pasang WAV 48 kHz. Memilih {args.n_clips} klip representatif...")
    sel_df = select_balanced_clips(pairs, n_total=args.n_clips)

    CLEAN_8K.mkdir(parents=True, exist_ok=True)
    NOISY_8K.mkdir(parents=True, exist_ok=True)
    RAW_IN_DIR.mkdir(parents=True, exist_ok=True)

    meta_rows = []
    sha_lines = []

    for i, row in sel_df.iterrows():
        cid = f"clip_{i + 1:02d}"
        _, s48 = read_wav_f64(row["clean_path"])
        _, y48 = read_wav_f64(row["noisy_path"])
        L = min(len(s48), len(y48))
        s48, y48 = s48[:L], y48[:L]

        # Resample berkualitas 48 kHz -> 8 kHz (faktor 1/6)
        s8 = resample_poly(s48, 1, 6)
        y8 = resample_poly(y48, 1, 6)
        L8 = min(len(s8), len(y8))
        s8, y8 = s8[:L8], y8[:L8]

        s_i16 = np.clip(np.round(s8 * 32768.0), -32768, 32767).astype(np.int16)
        y_i16 = np.clip(np.round(y8 * 32768.0), -32768, 32767).astype(np.int16)

        c_out = CLEAN_8K / f"{cid}.wav"
        n_out = NOISY_8K / f"{cid}.wav"
        r_out = RAW_IN_DIR / f"{cid}.raw"

        wavfile.write(str(c_out), 8000, s_i16)
        wavfile.write(str(n_out), 8000, y_i16)
        y_i16.astype("<i2").tofile(str(r_out))

        lead_ms = estimate_lead_in_ms(s8, 8000)
        snr_8k = compute_snr(s8, y8)

        meta_rows.append({
            "clip_id": cid,
            "orig_file": row["name"],
            "speaker": row["speaker"],
            "snr_in_db": round(snr_8k, 3),
            "duration_s": round(L8 / 8000.0, 3),
            "lead_in_ms": round(lead_ms, 1),
            "peak_noisy_i16": int(np.max(np.abs(y_i16))),
        })

        sha_lines.append(f"{sha256_file(c_out)}  clean/{cid}.wav")
        sha_lines.append(f"{sha256_file(n_out)}  noisy/{cid}.wav")

    meta_df = pd.DataFrame(meta_rows)
    meta_path = ROOT_DIR / "data" / "testset_meta.csv"
    sha_path = ROOT_DIR / "data" / "SHA256SUMS.txt"
    meta_df.to_csv(meta_path, index=False)
    sha_path.write_text("\n".join(sha_lines) + "\n", encoding="utf-8")

    print(f"[OK] {len(meta_df)} klip berhasil di-resample ke 8 kHz!")
    print(f"     SNR_in min/mean/max : {meta_df['snr_in_db'].min():.2f} / {meta_df['snr_in_db'].mean():.2f} / {meta_df['snr_in_db'].max():.2f} dB")
    print(f"     Jeda awal (lead-in) : min={meta_df['lead_in_ms'].min():.1f} ms, median={meta_df['lead_in_ms'].median():.1f} ms (<96ms={int((meta_df['lead_in_ms'] < 96.0).sum())}/{len(meta_df)})")
    print(f"     Metadata            : {meta_path}")
    print(f"     Checksum            : {sha_path}")


if __name__ == "__main__":
    main()
