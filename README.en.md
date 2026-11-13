# IKAOPLL-verilator

Render an OPLL/YM2413 VGM to WAV by simulating IKAOPLL RTL under Verilator. The RTL generates the sound; the C/C++ harness decodes VGM, drives clocks/register writes, captures PCM, and writes WAV. It does not substitute a software sound-chip emulator.

## Run

Use WSL with Bash, Verilator, make, a C/C++ compiler, and zlib development libraries. Python 3 is needed only for regression checks, with no additional packages. The renderer was tested with Verilator 4.038.

`./setup_env.sh` prepares the whole environment in one step: it checks the required tools (installing missing ones via apt), fetches the RTL, builds the simulator, and smoke-renders a bundled fixture. Re-running it leaves existing files as-is.

To prepare the RTL manually instead, place the upstream IKAOPLL sources and license in `rtl/` (do not overwrite an existing local copy unnecessarily). For a new checkout:

```bash
bash tools/create_rtl_dir.sh --fetch-raw https://github.com/ika-musume/IKAOPLL/blob/main
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm
```

This produces `ym2413_scale_rom1.wav`. To select output location and rate:

```bash
mkdir -p outputs
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --output outputs/scale.wav --sample-rate 48000
```

After building, run `build/obj_dir/ikaopll_sim INPUT.vgm --output OUTPUT.wav` directly. Output parent directories must exist. Existing output files are replaced; destinations that alias the input or another output are rejected.

### Batch conversion

`export_vgm.py` renders a file or a whole directory at once. Inputs are `.vgm`/`.vgz` (directories are scanned non-recursively); each becomes `<stem>.wav` in `--outdir`, with a `results.csv` manifest. Existing WAVs are skipped unless `--force` is given. The simulator is built automatically when missing (run `./setup_env.sh` first if `rtl/` is not prepared).

Normal output is the WAV files and `results.csv` only. Diagnostic CSVs and bus logs are produced only when the analysis option is given.

```bash
python3 export_vgm.py input.vgm --outdir outputs
python3 export_vgm.py /path/to/vgm_dir --outdir outputs --sample-rate 48000
```

## Timing and audio

VGM waits use 44,100 units per second. The master clock comes from the VGM YM2413 header (normally 3,579,545 Hz). `i_phiM_PCEN_n=0` enables every master edge. Capture signed 16-bit accumulated output once per ACC strobe rising edge, every 72 clocks (normally about 49,715.903 Hz), then linearly interpolate on integer clock timestamps to 44,100 Hz (default) or 48,000 Hz. Merely relabeling a WAV header does not resample audio.

Capture continues during bus writes and internal waits. Gains are 2 for melody and 3 for percussion. There is no heuristic impulse-MO fallback, normalization, or tone correction. The final partial interval holds the most recent native sample. Linear interpolation is a PCM conversion choice, not an analog reconstruction or hardware/SPDIF equivalence claim.

Explicit build parameters match the working sample: `FULLY_SYNCHRONOUS=1`, `FAST_RESET=1`, `USE_PIPELINED_MULTIPLIER=1`. The supplied nonpipelined phase-generator branch has an inverted reset condition. Vendor RTL is preserved. See [project knowledge](docs/project_knowledge.md).

Register transactions enforce 12 master clocks after an address and 84 after data. They consume the same timeline as VGM waits; subsequent absolute deadlines absorb service time. Oversubscribed bursts serialize in source order and their maximum lateness is reported. Reset time is excluded; trailing waits are retained. Deadlines round up to a master-clock boundary, so duration may exceed flooring the ideal VGM duration by one output frame. Writes extending past the final deadline retain their actual service time.

## Diagnostics

```bash
bash build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --output outputs/scale.wav --enable-csv --debug outputs/scale.bus.csv
```

- `scale.acc.csv`: native ACC values, capture clocks and ps timestamps.
- `scale.audio.csv`: interpolated PCM matching WAV; `mo_signed` is a sign-extended impulse-MO snapshot for reference.
- `scale.events.csv`: source address/data operations and relative VGM waits, including final waits.
- `scale.bus.csv`: requested VGM times, actual write-completion clocks, lateness and values.

`--enable-csv` creates the first three; `--debug [FILE]` adds the bus log. `--vcd [FILE]` enables a potentially large RTL waveform trace. CSVs/traces are disabled by default. The build script also saves simulator output in `build/sim_last_output.txt`.

## Supported input and tests

Input is VGM only; the former CSV event input has been removed. VGM 1.00–1.71, one standard YM2413, one pass. Plain and gzip-compressed VGM/VGZ are detected by their contents, including gzip data named `.vgm`. No loop expansion, dual YM2413 or VRC7 flags. Known foreign-chip commands can be skipped, but their audio is not rendered. Unknown/unsupported or malformed commands fail rather than reinterpret payload bytes. This is not an implementation of every VGM command.

The supplied comparison program generates a fixed test tone, not VGM playback. This renderer has no external DRAM model dependency.

```bash
bash build_and_run.sh --build-only
cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_vgm_parser.c src/vgm_parser.c -lz -o build/test_vgm_parser
build/test_vgm_parser build/parser_clock_fixture.vgm
python3 tests/test_audio_pipeline.py build/obj_dir/ikaopll_sim
```

Synthetic tests cover pitch, sign, strobe period, resampling, header clock, final waits, silence, rhythm, dense writes, repeatability, input protection and errors. See [AGENTS.md](AGENTS.md) for operating rules and external private field-note location.

## License

Copyright (c) 2025 emef2247.

Licensed under the [BSD 2-Clause License](LICENSE).

For licenses and copyright notices of third-party components used or referenced by this project, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The IKAOPLL RTL itself is not included in this repository; it is fetched locally (the upstream license text is saved to `rtl/IKAOPLL_LICENSE` at fetch time).

## Acknowledgments

The following tools and projects have been used or referenced during development, validation, and test generation. Special thanks to their developers, maintainers, and contributors.

Above all, thanks to ika-musume for publishing IKAOPLL, the YM2413 (OPLL) RTL reimplementation. This project renders sound with the IKAOPLL RTL itself, and would not exist without its publication.

Thanks also to [Hiroyuki](https://d4.princess.ne.jp/) for providing the working IKAOPLL sample and for his advice along the way; the parameter configuration the sample made explicit became the foundation for driving the RTL correctly.

| Project | Purpose |
| --- | --- |
| [IKAOPLL](https://github.com/ika-musume/IKAOPLL) | The YM2413 RTL itself, the sound source (by ika-musume) |
| [Verilator](https://www.veripool.org/verilator/) | RTL simulation |
| [EMU2413 / libvgm](https://github.com/ValleyBell/libvgm) | Reference WAV comparison (optional; not part of the renderer) |

Versions, upstream references, copyright notices, license terms, and redistribution requirements for third-party components are documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Independent WAV comparison

The requested public fixture copy is in `tests/fixtures/public/opll/` (19 VGM files,83files total), imported from msx_vgm2mml with hashes in `tests/fixtures/public/opll_import.json`. Its MML/MIDI expected files belong to the source project and are not RTL acoustic golden files.

Given independently rendered reference PCM and RTL PCM with the same sample rate, run `python3 tools/compare_opll_wavs.py reference.wav simulation.wav --output comparison.json --label fixture`. This optional tool requires NumPy. It retains raw length/level/DC/clipping, then reports bounded global delay, signed gain, waveform error/correlation,10ms envelope and spectral shape. It does not time-stretch the signals or treat emulator output as hardware truth. Periodic phase differences can also affect the fitted lag. Reference emulation is used only for comparison, never as the production renderer.
