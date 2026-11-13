#!/usr/bin/env python3
"""Batch-render OPLL/YM2413 VGM files to WAV through the IKAOPLL RTL simulator.

Usage:
    python3 export_vgm.py IN [IN ...] --outdir OUT [--sample-rate 44100|48000] [--force]

Each IN is a .vgm/.vgz file or a directory of them (non-recursive). Every
input becomes <stem>.wav in OUT, rendered by build/obj_dir/ikaopll_sim.
Prepare the environment first with ./setup_env.sh (tools, rtl/, build).
Existing WAVs are skipped unless --force is given. A results.csv manifest
is written into OUT. Exit status is 1 when any input failed.

Normal output is the WAV files plus results.csv only. The hidden --debug
option additionally keeps the simulator's diagnostic CSVs and bus log
under OUT/_diagnostics/<stem>/ for analysis runs; without it, a stale
_diagnostics tree in OUT is removed.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SIM = ROOT / "build" / "obj_dir" / "ikaopll_sim"
SUFFIXES = {".vgm", ".vgz"}


def collect(inputs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        if item.is_dir():
            files.extend(sorted(p for p in item.iterdir() if p.suffix.lower() in SUFFIXES))
        elif item.is_file() and item.suffix.lower() in SUFFIXES:
            files.append(item)
        else:
            print(f"warning: not a VGM file or directory, ignored: {item}", file=sys.stderr)
    return files


def ensure_built() -> None:
    if not (ROOT / "rtl" / "IKAOPLL.v").is_file():
        sys.exit("rtl/IKAOPLL.v is missing. Run ./setup_env.sh first.")
    if not SIM.is_file():
        print("Simulator not built; running build_and_run.sh --build-only ...")
        result = subprocess.run(["bash", "build_and_run.sh", "--build-only"], cwd=ROOT)
        if result.returncode != 0 or not SIM.is_file():
            sys.exit("Build failed. Run ./setup_env.sh and check its output.")


def wav_seconds(path: Path) -> str:
    try:
        with wave.open(str(path), "rb") as wav:
            return f"{wav.getnframes() / wav.getframerate():.3f}"
    except (wave.Error, OSError, ZeroDivisionError):
        return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch-render VGM to WAV via the IKAOPLL RTL simulator.")
    parser.add_argument("inputs", nargs="+", type=Path, help="VGM/VGZ file(s) or directories of them")
    parser.add_argument("--outdir", required=True, type=Path, help="Directory for the rendered WAV files")
    parser.add_argument("--sample-rate", type=int, choices=(44100, 48000), default=44100)
    parser.add_argument("--force", action="store_true", help="Overwrite WAVs that already exist in --outdir")
    parser.add_argument("--debug", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    files = collect(args.inputs)
    if not files:
        sys.exit("No VGM inputs found.")
    ensure_built()
    args.outdir.mkdir(parents=True, exist_ok=True)
    diagnostics_root = args.outdir / "_diagnostics"
    if not args.debug and diagnostics_root.is_dir():
        shutil.rmtree(diagnostics_root)

    rows: list[dict[str, str]] = []
    rendered = skipped = failed = 0
    for src in files:
        out = args.outdir / (src.stem + ".wav")
        if out.exists() and not args.force:
            print(f"skip (exists): {out.name}")
            rows.append({"input": str(src), "output": str(out), "status": "skipped", "seconds": wav_seconds(out)})
            skipped += 1
            continue
        command = [str(SIM), str(src), "--output", str(out), "--sample-rate", str(args.sample_rate)]
        diag_dir: Path | None = None
        if args.debug:
            diag_dir = diagnostics_root / src.stem
            diag_dir.mkdir(parents=True, exist_ok=True)
            command += ["--enable-csv", "--debug", str(diag_dir / (src.stem + ".bus.csv"))]
        result = subprocess.run(command, capture_output=True, text=True)
        if diag_dir is not None and result.returncode == 0:
            for suffix in (".audio.csv", ".acc.csv", ".events.csv"):
                produced = args.outdir / (src.stem + suffix)
                if produced.is_file():
                    produced.replace(diag_dir / produced.name)
        if result.returncode == 0 and out.is_file():
            seconds = wav_seconds(out)
            print(f"rendered: {src.name} -> {out.name} ({seconds}s)")
            rows.append({"input": str(src), "output": str(out), "status": "rendered", "seconds": seconds})
            rendered += 1
        else:
            print(f"FAILED: {src} (exit {result.returncode})", file=sys.stderr)
            tail = (result.stdout + result.stderr).strip().splitlines()
            for line in tail[-5:]:
                print(f"    {line}", file=sys.stderr)
            rows.append({"input": str(src), "output": str(out), "status": f"failed:{result.returncode}", "seconds": ""})
            failed += 1

    with open(args.outdir / "results.csv", "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=["input", "output", "status", "seconds"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"done: {rendered} rendered, {skipped} skipped, {failed} failed -> {args.outdir}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
