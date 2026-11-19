"""fill_template.py -- Isi Template_Pengukuran_Hasil.xlsx secara otomatis & aman.

Mempertahankan seluruh sheet, style, dan rumus Excel asli (tanpa butuh openpyxl).
Hanya mengisi sel data nyata dari CSV hasil evaluasi dan log lab.

Contoh penggunaan besok setelah pengukuran di lab:
    python fill_template.py \
        --template "../../Template_Pengukuran_Hasil.xlsx" \
        --out "../results/Template_Pengukuran_Hasil_KelompokA.xlsx" \
        --anggota "Nama 1, Nama 2" \
        --tanggal "2026-11-18" \
        --fpy-csv "../results/metrics_FPY.csv" \
        --qdig-csv "../results/metrics_QDIG.csv" \
        --cycles-mean 125000 --cycles-max 132000 \
        --memory-kb 14.5 --snr-q 49.5 --latency-ms 32.0 --overrun 0
"""
import argparse
from pathlib import Path
import zipfile
import xml.etree.ElementTree as ET
import pandas as pd

NS_MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
ET.register_namespace("", NS_MAIN)
ET.register_namespace("r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships")

DEFAULT_PARAM_STR = (
    "alpha=2.0 (Q12=8192), beta=0.02 (Q15=655), lam=0.9375 (shift=4), "
    "vad=3dB (Q8=512), n_init=6, pow_shift=8, N=256, H=128, Hann, SQRT_LUT=257"
)

ROOT_DIR = Path(__file__).resolve().parent.parent


def _set_cell_text(root: ET.Element, cell_ref: str, text: str) -> None:
    for c in root.iter(f"{{{NS_MAIN}}}c"):
        if c.attrib.get("r") == cell_ref:
            c.attrib["t"] = "inlineStr"
            for child in list(c):
                if child.tag in (f"{{{NS_MAIN}}}v", f"{{{NS_MAIN}}}is", f"{{{NS_MAIN}}}f"):
                    c.remove(child)
            is_el = ET.SubElement(c, f"{{{NS_MAIN}}}is")
            t_el = ET.SubElement(is_el, f"{{{NS_MAIN}}}t")
            t_el.text = str(text)
            return


def _set_cell_num(root: ET.Element, cell_ref: str, val: float) -> None:
    if pd.isna(val):
        return
    for c in root.iter(f"{{{NS_MAIN}}}c"):
        if c.attrib.get("r") == cell_ref:
            c.attrib["t"] = "n"
            for child in list(c):
                if child.tag in (f"{{{NS_MAIN}}}is", f"{{{NS_MAIN}}}f"):
                    c.remove(child)
            v_el = c.find(f"{{{NS_MAIN}}}v")
            if v_el is None:
                v_el = ET.SubElement(c, f"{{{NS_MAIN}}}v")
            v_el.text = f"{float(val):.6g}" if not float(val).is_integer() else str(int(val))
            return


def _fill_block_from_csv(
    root: ET.Element,
    csv_path: Path,
    start_row: int,
    cycles_mean: float = None,
    memory_kb: float = None,
    cycles_map: dict = None,
) -> None:
    df = pd.read_csv(csv_path)
    col_map = {c.lower(): c for c in df.columns}

    def pick(row, *candidates):
        for cand in candidates:
            if cand.lower() in col_map:
                return row[col_map[cand.lower()]]
        return float("nan")

    for idx, (_, row) in enumerate(df.head(30).iterrows()):
        r = start_row + idx
        cid = pick(row, "clip_id")
        if isinstance(cid, str) and cid:
            _set_cell_text(root, f"A{r}", cid)
        _set_cell_num(root, f"B{r}", pick(row, "snr_in", "SNR_in"))
        _set_cell_num(root, f"C{r}", pick(row, "snr_out", "SNR_out"))
        _set_cell_num(root, f"E{r}", pick(row, "pesq", "PESQ_out"))
        _set_cell_num(root, f"F{r}", pick(row, "stoi", "STOI_out"))
        if cycles_map and isinstance(cid, str) and cid in cycles_map:
            c_mean, c_max = cycles_map[cid]
            _set_cell_num(root, f"G{r}", c_mean)
            _set_cell_text(root, f"J{r}", f"max_cyc={int(c_max)}")
        elif cycles_mean is not None:
            _set_cell_num(root, f"G{r}", cycles_mean)
        if memory_kb is not None:
            _set_cell_num(root, f"I{r}", memory_kb)


def main() -> None:
    parser = argparse.ArgumentParser(description="Isi Template_Pengukuran_Hasil.xlsx untuk Kelompok A")
    parser.add_argument(
        "--template",
        type=Path,
        default=ROOT_DIR.parent / "Template_Pengukuran_Hasil.xlsx",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=ROOT_DIR / "results" / "Template_Pengukuran_Hasil_KelompokA.xlsx",
    )
    parser.add_argument("--anggota", type=str, default=None)
    parser.add_argument("--params", type=str, default=DEFAULT_PARAM_STR)
    parser.add_argument("--commit", type=str, default=None)
    parser.add_argument("--tanggal", type=str, default=None)
    parser.add_argument("--qdig-csv", type=Path, default=None)
    parser.add_argument("--fpy-csv", type=Path, default=None)
    parser.add_argument("--qlive-csv", type=Path, default=None)
    parser.add_argument("--cycles-csv", type=Path, default=None)
    parser.add_argument("--cycles-mean", type=float, default=None)
    parser.add_argument("--cycles-max", type=float, default=None)
    parser.add_argument("--memory-kb", type=float, default=None)
    parser.add_argument("--snr-q", type=float, default=None)
    parser.add_argument("--latency-ms", type=float, default=None)
    parser.add_argument("--overrun", type=int, default=None)
    args = parser.parse_args()

    if not args.template.exists():
        raise FileNotFoundError(f"Template tidak ditemukan: {args.template}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.template, "r") as zin:
        sheet2_xml = zin.read("xl/worksheets/sheet2.xml")
        root = ET.fromstring(sheet2_xml)

        if args.anggota:
            _set_cell_text(root, "C3", args.anggota)
        if args.params:
            _set_cell_text(root, "C4", args.params)
        if args.commit:
            _set_cell_text(root, "C5", args.commit)
        if args.tanggal:
            _set_cell_text(root, "C8", args.tanggal)

        cycles_map = None
        if args.cycles_csv and args.cycles_csv.exists():
            cdf = pd.read_csv(args.cycles_csv)
            cycles_map = {
                str(r["clip_id"]): (float(r["mean_cyc"]), float(r["max_cyc"]))
                for _, r in cdf.iterrows()
            }

        if args.qdig_csv and args.qdig_csv.exists():
            _fill_block_from_csv(root, args.qdig_csv, start_row=12,
                                 cycles_mean=args.cycles_mean, memory_kb=args.memory_kb,
                                 cycles_map=cycles_map)
        if args.fpy_csv and args.fpy_csv.exists():
            _fill_block_from_csv(root, args.fpy_csv, start_row=48)
            _set_cell_text(root, "J78", "n/a")
        if args.qlive_csv and args.qlive_csv.exists():
            _fill_block_from_csv(root, args.qlive_csv, start_row=84,
                                 cycles_mean=args.cycles_mean, memory_kb=args.memory_kb)

        if args.cycles_max is not None:
            _set_cell_text(root, "J42", f"cycles max (worst case): {int(args.cycles_max)}")
        if args.snr_q is not None:
            _set_cell_num(root, "D123", args.snr_q)
        if args.latency_ms is not None:
            _set_cell_num(root, "D124", args.latency_ms)
        if args.overrun is not None:
            _set_cell_num(root, "D125", args.overrun)

        new_sheet2 = ET.tostring(root, encoding="utf-8", xml_declaration=True)
        tmp_out = args.out.with_suffix(".tmp.xlsx")
        with zipfile.ZipFile(tmp_out, "w", zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                if item.filename == "xl/worksheets/sheet2.xml":
                    zout.writestr(item, new_sheet2)
                else:
                    zout.writestr(item, zin.read(item.filename))
    tmp_out.replace(args.out)

    print(f"[OK] Template tersimpan di: {args.out}")


if __name__ == "__main__":
    main()
