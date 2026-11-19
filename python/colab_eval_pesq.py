"""colab_eval_pesq.py -- Hitung PESQ-NB + STOI + SNR di Google Colab & lengkapi Excel.

Cara pakai di Google Colab:
  1. Upload file `colab_pesq_kit.zip` ke Colab
  2. Jalankan di 1 cell:
       !unzip -q -o colab_pesq_kit.zip -d work
       !pip install -q pesq pystoi pandas scipy numpy matplotlib
       !python work/python/colab_eval_pesq.py
  3. Download hasil `work/results/Template_Pengukuran_Hasil_KelompokA.xlsx`
     dan `work/results/metrics_QDIG_all.csv`.
"""
import subprocess
import sys
from pathlib import Path
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    eval_py = ROOT / "python" / "evaluate_metrics.py"
    fill_py = ROOT / "python" / "fill_template.py"
    csv_all = ROOT / "results" / "metrics_QDIG_all.csv"
    csv_fpy = ROOT / "results" / "metrics_FPY.csv"
    csv_qdig = ROOT / "results" / "metrics_QDIG.csv"
    tpl_in = ROOT / "results" / "Template_Pengukuran_Hasil_KelompokA.xlsx"

    subprocess.run(
        [
            sys.executable,
            str(eval_py),
            "--q15-dir",
            str(ROOT / "results" / "Q-DIG"),
            "--csv-out",
            str(csv_all),
            "--skip-plots",
        ],
        check=True,
    )

    df = pd.read_csv(csv_all)
    fpy = df[df["cond"] == "F-PY"].copy()
    qdig = df[df["cond"] == "Q15"].copy()
    qdig["cond"] = "Q-DIG"
    fpy.to_csv(csv_fpy, index=False)
    qdig.to_csv(csv_qdig, index=False)

    snr_q_mean = float(qdig["SNR_q_vs_FPY"].mean())

    subprocess.run(
        [
            sys.executable,
            str(fill_py),
            "--template",
            str(tpl_in),
            "--out",
            str(tpl_in),
            "--anggota",
            "Aldy Alfaisal Pasaribu, Vincent Junio Ibrahim",
            "--tanggal",
            "2026-11-19",
            "--fpy-csv",
            str(csv_fpy),
            "--qdig-csv",
            str(csv_qdig),
            "--cycles-csv",
            str(ROOT / "results" / "cycles.csv"),
            "--cycles-max",
            "414424",
            "--memory-kb",
            "15.54",
            "--snr-q",
            f"{snr_q_mean:.3f}",
            "--overrun",
            "0",
        ],
        check=True,
    )

    for tag, sub in [("F-PY", fpy), ("Q-DIG", qdig)]:
        if "PESQ_out" in sub.columns:
            print(
                f"[{tag}] PESQ_in={sub['PESQ_in'].mean():.3f} -> "
                f"PESQ_out={sub['PESQ_out'].mean():.3f} +/- {sub['PESQ_out'].std():.3f} | "
                f"STOI_out={sub['STOI_out'].mean():.4f}"
            )
    print(f"[DONE] Excel lengkap siap di-download: {tpl_in}")


if __name__ == "__main__":
    main()
