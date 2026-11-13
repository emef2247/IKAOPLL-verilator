# IKAOPLL-verilator project knowledge

This document records durable principles and established implementation facts. Detailed measurements and provisional hypotheses belong in the external private field notes defined by `AGENTS.md`.

## Goal and boundaries

The purpose is straightforward: read an OPLL/YM2413 VGM, run IKAOPLL RTL with Verilator, and obtain the rendered WAV. The RTL generates the sound. The C/C++ harness handles file decoding, bus/clock timing, digital output capture, and format conversion. Do not replace it with a software sound-chip implementation or extend it into note interpretation, MML conversion, or other rendering targets.

Keep source command decoding, requested event timing, actual bus execution, RTL synthesis, raw digital sample capture, resampling, and WAV encoding distinguishable. A correction in one stage must not silently rewrite the evidence from an earlier stage.

Optional debug/intermediate outputs help verify those stages. They should not complicate the ordinary VGM-to-WAV flow or expand the product's purpose.

## Four distinct rates

- VGM waits use units of 1/44,100 second. This does not mean that the chip itself updates at 44.1 kHz.
- The YM2413 master clock is normally 3,579,545 Hz. A supported VGM header can specify another rate; clock flags must be validated separately from the numeric rate.
- With the supplied IKAOPLL implementation and a continuously enabled master clock, the accumulated digital output updates once per 72 master clocks, approximately 49.716 kHz at the normal clock.
- The output WAV sample rate is an independent choice; the repair uses 44,100 Hz by default. Actual conversion of sample positions is required. Relabeling a header changes duration and pitch.

`i_phiM_PCEN_n` is an active-low clock enable. A constant zero enables every EMUCLK rising edge. A software reference divider does not divide the RTL clock unless it drives that input. Check both sides of an apparent clock factor before changing it.

## Raw outputs and capture

`o_ACC_SIGNED` is a signed 16-bit accumulated mix. `o_ACC_SIGNED_STRB` indicates a stable output; detect a selected edge once per native sample rather than treating every high clock as a new sample. It is also possible to read the held ACC value every master clock and resample that stream, as in the supplied FPGA reference discussion, provided the actual timebase is preserved.

`o_IMP_FLUC_SIGNED_MO` and `o_IMP_FLUC_SIGNED_RO` are signed 10-bit impulse outputs. Their generated Verilator C++ storage can be unsigned. Casting that storage directly to `int16_t` does not sign-extend bit 9. These outputs are distinct from the accumulated full-channel PCM mix and must not be substituted silently.

The upstream/sample gain inputs are +2 for melodic output and +3 for percussion. Gain changes affect level and clipping; do not mistake them for evidence that a synthesis or timing defect is fixed.

Acquire audio on every clock advancement path, including register transactions and minimum bus waits. A sample loop restricted to VGM wait processing loses information during writes; emitting repeated catch-up values cannot recover those missing samples.

Resampled PCM is derived data. Preserve the raw sample values and capture times when inspecting interpolation. Linear interpolation is a chosen reconstruction method, not a claim of analog reconstruction or an ideal antialias filter.

## RTL provenance and parameter selection

The nine RTL Verilog files in the local checkout and the supplied `IKAOPLL_ACC_SIGNED_STRB.zip` were byte-identical at the start of the 2026-11-12 investigation. Preserve vendor files and licenses; record harness parameter choices separately.

The supplied sample explicitly selects `FULLY_SYNCHRONOUS=1`, `FAST_RESET=1`, and `USE_PIPELINED_MULTIPLIER=1`. The top-level RTL defaults differ. In the supplied source, the nonpipelined phase-generator branch contains `~(~i_PG_PHASE_RST | i_TEST[2])`, while the pipelined branch contains `~(i_PG_PHASE_RST | i_TEST[2])`. That difference reverses phase retention/reset behavior. Explicitly selecting the sample's pipelined branch avoids that code path without editing vendor RTL. Keep isolated build comparisons as evidence rather than attributing every audible difference to resampling.

Reset duration must match the selected implementation and flush its pipelines before playback begins. The upstream documentation requires at least 72 master-clock cycles for the normal reset path; a 64-cycle pulse does not satisfy that stated minimum. Exclude reset time from the playback timeline and generated audio duration.

## Playback and parser rules

Use a single master-clock timeline. Source timestamps describe requested event times; physical bus writes occupy clock cycles and may serialize a burst. Retain requested and actual timing so lateness is inspectable, and let subsequent absolute deadlines absorb transaction time rather than adding full relative VGM waits after every transaction.

Round final deadlines up to the next physical master-clock boundary. At 48 kHz this can add one output sample compared with flooring the ideal VGM duration (for example, a 113-unit silent VGM produces 123 frames rather than 122). Writes that overrun the final deadline retain their actual service time.

Retain final waits and release/silence intervals even when no later YM2413 write follows. The diagnostic events CSV must preserve those waits and event order so the source schedule can be inspected exactly. It is output only: CSV event input was removed on 2026-11-13, so nothing replays it.

Validate file bounds, header version/data offsets, command lengths, and clock flags. A malformed or unsupported command must not be skipped one byte at a time and reinterpreted as unrelated register writes. Report unsupported hardware configurations explicitly.

The renderer's supported-chip and loop behavior must be stated in the README and parser. Successful parsing of a file with other chips does not mean their audio has been synthesized.

## What verification establishes

Use synthetic tests for known pitch, sparse versus dense equivalent writes, final waits, silence, output duration, native strobe period, rhythm output, malformed input, and deterministic repeated generation as relevant to a change.

Separate observations from conclusions: source inspection establishes a branch difference; a test establishes behavior for its input and build; an FPGA recording establishes behavior through that particular hardware and recording path. None alone proves full simulation/hardware/SPDIF equivalence.

Sources: the locally supplied RTL and sample; [upstream IKAOPLL documentation](https://github.com/ika-musume/IKAOPLL), including clock enable, reset, signed accumulated output, strobe, and gain descriptions; [VGM specification](https://vgmrips.theclassicgamer.net/VGM_Specification) for file timing and layout. Experimental results are recorded separately.
