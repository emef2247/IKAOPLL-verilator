# AGENTS.md — IKAOPLL-verilator

## Purpose and working environment

- Read an OPLL/YM2413 VGM, run IKAOPLL RTL with Verilator, and obtain its rendered WAV.
- The RTL generates the sound. The C/C++ harness decodes VGM, drives the bus and clocks, captures digital output, and converts it to WAV; it must not replace IKAOPLL with a software sound-chip implementation.
- Main code is C/C++ and Verilog; Python tools support inspection and regression checks. Build and run primarily in WSL.
- Preserve source timing, register values, chip clock, and intermediate evidence. A valid WAV file alone does not establish correct sound or hardware equivalence.
- This is a rendering/simulation project. MML conversion, musical quantization, and cross-chip projection belong to other projects.
- Keep the normal flow simple: input VGM → RTL simulation → WAV. Optional intermediate/debug outputs support validation and are not additional product goals.

## Model and delegation policy

- Use GPT-6.1 Sol with xhigh reasoning for the main session, subject to available models and higher-priority instructions. Keep routine work in the main session.
- When useful, delegate independent exploration to explorer and documentation research to researcher (GPT-6 Luna, medium), and scoped implementation/tests to worker (GPT-6.1 Sol, medium).
- Use architect (GPT-6 Astra, high) before committing to a major plan, when the same error recurs after an attempted fix, and before declaring a long or substantial task complete. Provide evidence and a focused question. The architect reviews; Sol implements and verifies.
- Handle straightforward file/tool selection and retries directly. No separate JEV service is configured.

## Read before changing behavior

- Read `docs/project_knowledge.md` before changing parsing, bus timing, clocking, reset, signed values, gain, sample capture, or resampling.
- Read `docs/collaboration_principles.md` before substantial architectural decisions or changes to scope.
- Search relevant external field notes for prior measurements and rejected approaches. Do not invent missing decisions or apply another project's chip-specific assumptions without checking their scope.

## Build, run, and check

From the repository root in WSL:

```bash
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm
./build_and_run.sh tests/vgm/ym2413_scale_rom1.vgm --enable-csv --debug
```

- The script builds `build/obj_dir/ikaopll_sim`. For isolated runs, use that executable from a separate output directory with an absolute input path. Avoid overwriting source fixtures or pre-existing outputs.
- Regression commands added with the audio repair are `python3 tests/test_audio_pipeline.py build/obj_dir/ikaopll_sim` and `cc -std=c99 -Isrc tests/test_vgm_parser.c src/vgm_parser.c -lz -o build/test_vgm_parser`, followed by `./build/test_vgm_parser build/parser_clock_fixture.vgm`.
- Input is VGM only (CSV event input was removed on 2026-11-13). Inspect intermediate evidence in addition to test exit status.
- VCD traces can be very large. Keep tracing off by default; use short, targeted intervals when needed.
- Do not invent undocumented command-line options. Inspect `README.md`, the script, and the executable's help.

## Layout and transformation boundaries

- `src/vgm_parser.*`: bounded VGM decoding and source/header validation.
- `src/vgm_player.*`: source event ordering and playback schedule.
- `src/ym2413_bus.*`: physical bus transactions and clock advancement.
- `src/ikaopll_wrapper.*`: Verilator lifecycle, clocks/reset, raw output capture, and PCM collection.
- `src/wav_writer.*`: WAV serialization.
- `rtl/`: locally supplied upstream IKAOPLL sources; preserve their provenance and license.
- `fromHiroyuki-san/`: locally supplied comparison material. Preserve originals; do not assume permission to commit it.
- `tests/`: synthetic VGM/MML fixtures and automated checks; `tools/`: inspection/conversion helpers.
- `docs/`: public, stable knowledge and focused technical explanations.
- `handoffs/current.md`: restart state, completed checks, remaining work, and next allowed action.

Keep these responsibilities distinguishable without refactoring merely to match a diagram:

```text
Raw VGM / source timestamps
  -> ordered register events
  -> scheduled bus writes (requested and actual clock times)
  -> IKAOPLL RTL
  -> raw signed ACC samples and capture times
  -> explicitly resampled PCM
  -> WAV
```

## Evidence and implementation rules

- Preserve human-readable dumps of relevant source events, actual bus timing, raw ACC samples, and resampled output when investigating a transformation. Keep observed values separate from interpolated or otherwise derived values.
- Distinguish the VGM 44,100-unit timebase, YM2413 master clock, ACC update rate, and WAV sample rate. Changing a WAV header is not resampling.
- Use one master-clock timeline for bus work and playback waits. Account for transaction time; report unavoidable lateness rather than silently stretching every VGM interval.
- Capture stable ACC output once per selected strobe edge; a strobe high level can span multiple clocks. Continue audio acquisition during bus writes and internal waits.
- Respect Verilator port widths: ACC is signed 16-bit; impulse MO/RO ports are signed 10-bit and require explicit sign extension when read through unsigned generated C++ storage.
- Keep vendor RTL unchanged when a harness/build correction suffices. Record explicit parameter choices. An upstream parameter default is not proof that every branch works identically.
- Avoid heuristic output-path fallback, normalization, or filtering that conceals a timing or synthesis defect. If an approximation is intentional, document it at the PCM conversion boundary.
- Do not claim FPGA, cartridge, analog, or SPDIF equivalence from simulation-only measurements. Hardware observations supplied by the owner are reference evidence with their own clock/rate path.
- Keep changes attributable to a stage and preserve unrelated user changes. Do not rewrite the project merely to introduce documentation or a workflow.

## Fixtures and external private field notes

- Private VGM, recordings, ROM-derived data, and supplied archives stay outside git unless explicitly safe to commit. Use synthetic public fixtures for committed regressions.
- In the current checkout layout, resolve the external notes repository relative to this project root as `../test/my-external-intelligence/`; this is the existing repository, not a new local notes directory.
- This project's notes belong in `../test/my-external-intelligence/IKAOPLL-verilator/field_notes/`. References to `field_notes/...` mean that directory. Do not create a local `field_notes/` copy or symlink.
- If the checkout is relocated, locate the existing private repository and update this path deliberately. Do not silently create another repository at the conventional sibling `../my-external-intelligence/` path.
- Search related `msx_vgm2mml`, `x68k_vgm2mml`, and `mucom88_vgm2mml` note directories in that same private repository when a relevant shared decision is missing. Respect each note's source/target scope.
- Record experiments, ablations, metrics, rejected hypotheses, and unresolved questions as `field_notes/YYYY-MM-DD_topic.md`. Put only durable public principles in `docs/project_knowledge.md`; never copy private fixture contents into public documentation.
- If external notes cannot be read or written, report the limitation and intended destination, and continue independent authorized work without inventing history.
- Inspect the public and private repositories' status/diff separately. Commit or push only when requested; stage only task-related changes. A public commit does not include external notes, and its authorization does not authorize a private push.

## Done means

- The authorized change is implemented, with relevant tests and output generation checked or a concrete blocker reported.
- Relevant intermediate evidence was generated and inspected. Compare duration, pitch, signed amplitude, sample/capture counts, event ordering, and rhythm behavior as appropriate.
- Keep causal evidence separate from cleanup: preserve baseline and isolated parameter/audio/scheduling comparisons when diagnosing sound.
- Update stable knowledge, external field notes, and `handoffs/current.md` with verified results, remaining limits, next allowed action, and known traps.
- Report what was verified and what remains unverified; do not substitute successful file generation for correctness.
