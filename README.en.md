# IKAOPLL-verilator

## Setup (create the rtl/ directory)

This project expects the IKAOPLL RTL sources to live under the `rtl/` directory. If you don't have the RTL locally, the included script can fetch them from upstream. In fetch mode provide the GitHub `blob`-style URL (the script converts it to a raw.githubusercontent URL).

Example:
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main
```

---

## How to run

Build + run script: `./build_and_run.sh`  
(The script builds with Verilator and then runs the simulation.)

Basic usage
- For CSV input (CSV generation is described below):
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv
```

- For a VGM input file:
```bash
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm
```

Common options
- `--vcd <file>` : enable VCD output (default: `ikaopll_dump.vcd`). A full signal dump can be very large—use with care.
- `--fst <file>` : generate an FST trace (Verilator FST tracing).
- `--enable-csv` : enable CSV logs (MO/ACC etc.).
- `--no-csv` : disable CSV logs (the wrapper defaults to CSV disabled).

Defaults
- The build/run script is configured to *not* emit CSV logs by default. If you want CSV output, run with `--enable-csv`.

CSV / VCD notes
- VCD output can be extremely large (full dumps). Watch your disk usage.
- CSV logs (especially very high-rate logs tied to EMUCLK) produce many lines. Log only the intervals you need.

MO (DAC) logging
- With `--enable-csv` the simulator emits a value-change log for MO (DAC) changes named `mo_value_changes.csv`.
- `mo_value_changes.csv` columns:
  - t_ps : timestamp in picoseconds
  - mo_signed : DAC output (signed) after the change
  - (Implementations may include additional columns; check the header.)
- Because `mo_value_changes.csv` is a change-only log (value-change), it is not evenly sampled. To synthesize audio you should resample/decimate using techniques such as weighted averaging or ZOH.

---

## From logs to WAV (available tools)

We include several helper tools. Typical workflows and examples:

1) Create a WAV from `audio_samples.csv` (accumulator / periodic sampling produced by the simulator)
```bash
python3 ./tools/csv_to_wav.py -i audio_samples.csv -o out_acc.wav --col-name acc_signed --scale 1
```
- `--col-name` picks the CSV column (e.g. `acc_signed`).
- `--scale` scales integer values to audible ranges; tune to match expected levels.

2) Create a WAV from the DAC change log (`mo_value_changes.csv`) for a more faithful reproduction
- Because the change log only records value transitions, decimation by weighted averaging is suitable to preserve energy from short pulses.
- Example (weighted-average decimation):
```bash
python3 ./tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav
```
- Main options:
  - `--sr` : sample rate (default 44100)
  - `--scale` : scaling factor (e.g. 64)
  - `--start-ps` / `--end-ps` : time range in picoseconds

3) Simple ZOH resampling (hold previous value) example:
```bash
python3 ./tools/mo_changes_to_wav.py -i mo_value_changes.csv -o mo.wav --samplerate 44100 --scale 64
```

Notes
- `mo_changes_to_wav_weighted.py` computes a time-weighted average for each output audio sample interval so that short pulses contribute proportionally to the sample energy. This is effective when reconstructing pitch from change-only logs.
- If pitches still do not appear as expected, consider collecting denser logs (e.g., every EMUCLK) or enabling additional simulator-side logging; note this increases data volume significantly.

---

## Example full workflow

1. Prepare RTL:
```bash
./tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/tree/main
```

2. Build and simulate (no CSV, no VCD):
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv
```

3. If you want the MO change CSV (enable CSV):
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv --enable-csv
# This produces mo_value_changes.csv (in the working directory)
```

4. Convert `mo_value_changes.csv` to WAV (weighted average):
```bash
python3 ./tools/mo_changes_to_wav_weighted.py -i mo_value_changes.csv -o mo_avg.wav
```

---

## Output file naming and generated WAV (project-specific behavior)

This project uses the input filename provided at runtime to form output names. Specifically, it removes *only the final extension* from the input path and uses that string as the base name for produced files. The main generated files are:

- `<inputbase>.csv`  
  - Contents: simulator sampling results (`t_ps,mo_signed,acc_signed`, etc.)
  - Example: if you run with `tests/csv/ym2413_scale_chromatic.vgm.csv` the output CSV will be named `ym2413_scale_chromatic.vgm.csv`. The base is `ym2413_scale_chromatic.vgm` (only the final `.csv` extension was removed) and `.csv` is appended back for the output file.
  - Note: this behavior strips *only the last extension*. For `foo.vgm.csv` the base becomes `foo.vgm`. If you prefer special handling (e.g. remove `.vgm.csv` entirely) that can be added.

- `<inputbase>.wav`  
  - Contents: a "vanilla" WAV produced deterministically by the simulator harness. The harness collects the `mo_signed` (DAC-like) samples and scales them into signed 16-bit PCM.
  - Important: the generated WAV is a raw (vanilla) PCM of the collected samples — it does *not* pass through an analog reconstruction / lowpass filter. Therefore:
    - The waveform is essentially the discrete held values (ZOH/hold) and may sound bright or contain many harmonics.
    - Differences from real hardware or `vgm2wav` are typically due to analog reconstruction, DC removal, normalization, and envelope processing that are *not* applied to the vanilla WAV.
  - Recommended post-processing to improve listenability:
    - Oversample + lowpass (reconstruction) → downsample
    - DC removal (high-pass around 5–20 Hz)
    - Peak normalization (e.g., -1 dB)
    - These steps can be carried out by tools in `tools/` or external utilities (sox, etc.). A deterministic FIR lowpass could also be added to this repository in the future.
  - Determinism: the WAV is produced deterministically by the harness. With the same inputs and commands, the same binary WAV will be generated.

- Generation location:
  - `build_and_run.sh` runs the simulator from the repository root as the working directory. Output files (CSV/WAV) are therefore generated in the working directory (usually the repo root).
  - The run log prints the target names (`Audio outputs will be: <base>.csv and <base>.wav`) and the harness prints `WAV written: <path>` when the WAV is written.

---

## Notes & caveats

- Timestamps are in picoseconds (ps). WAV conversion scripts convert ps → seconds to compute sampling intervals.
- Full VCD traces are large. Disable unnecessary traces if disk space is a concern.
- The IKAOPLL RTL is not included in this repository. If needed, fetch it locally using the included script (see above).
