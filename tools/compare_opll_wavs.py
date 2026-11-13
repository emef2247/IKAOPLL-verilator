#!/usr/bin/env python3
"""Compare reference-emulator and RTL-simulation PCM without changing either WAV.

Requires NumPy. A global delay, DC offset, and scalar gain can be fitted for
inspection, but original levels/durations remain separate in the JSON report.
No metric here establishes hardware equivalence or supplies a pass threshold.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import wave

import numpy as np


def read_pcm16(path: Path) -> tuple[int, np.ndarray, dict]:
    with wave.open(str(path), "rb") as stream:
        if stream.getcomptype() != "NONE" or stream.getsampwidth() != 2:
            raise ValueError(f"Only uncompressed 16-bit PCM is supported: {path}")
        channels = stream.getnchannels()
        if channels not in (1, 2):
            raise ValueError(f"Only mono or stereo PCM is supported: {path}")
        rate = stream.getframerate()
        frame_count = stream.getnframes()
        data = stream.readframes(frame_count)
    pcm = np.frombuffer(data, dtype="<i2")
    if pcm.size != frame_count * channels:
        raise ValueError(f"Truncated WAV data: {path}")
    matrix = pcm.reshape(-1, channels).astype(np.float64) / 32768.0
    mono = matrix.mean(axis=1)
    metrics = {
        "path": str(path.resolve()),
        "channels": channels,
        "sample_rate_hz": rate,
        "frames": frame_count,
        "duration_seconds": frame_count / rate,
        "pcm_min": int(pcm.min()) if pcm.size else None,
        "pcm_max": int(pcm.max()) if pcm.size else None,
        "channel_peak_full_scale": float(np.abs(matrix).max()) if pcm.size else 0.0,
        "channel_clipped_samples": int(np.count_nonzero((pcm == -32768) | (pcm == 32767))),
        "channel_dc_full_scale": matrix.mean(axis=0).tolist() if pcm.size else [0.0] * channels,
        "channel_rms_full_scale": np.sqrt(np.mean(matrix * matrix, axis=0)).tolist() if pcm.size else [0.0] * channels,
        "mono_peak_full_scale": float(np.abs(mono).max()) if mono.size else 0.0,
        "mono_dc_full_scale": float(mono.mean()) if mono.size else 0.0,
        "mono_rms_full_scale": float(np.sqrt(np.mean(mono * mono))) if mono.size else 0.0,
    }
    if channels == 2:
        metrics["stereo_collapse"] = "Arithmetic channel mean; stereo cancellation can reduce the compared signal."
    return rate, mono, metrics


def _correlation(a: np.ndarray, b: np.ndarray) -> float | None:
    if a.size < 2:
        return None
    a = a - a.mean()
    b = b - b.mean()
    denominator = float(np.linalg.norm(a) * np.linalg.norm(b))
    return float(np.clip(np.dot(a, b) / denominator, -1.0, 1.0)) if denominator > 1e-24 else None


def overlap(reference: np.ndarray, simulation: np.ndarray, lag: int) -> tuple[np.ndarray, np.ndarray]:
    # Positive lag means simulation is delayed: compare ref[i] with sim[i+lag].
    r0, s0 = max(0, -lag), max(0, lag)
    count = max(0, min(reference.size - r0, simulation.size - s0))
    return reference[r0:r0 + count], simulation[s0:s0 + count]


def best_lag(reference: np.ndarray, simulation: np.ndarray, maximum: int) -> tuple[int, float | None]:
    if min(reference.size, simulation.size) < 2:
        return 0, None
    r = reference - reference.mean()
    s = simulation - simulation.mean()
    if np.linalg.norm(r) < 1e-12 or np.linalg.norm(s) < 1e-12:
        return 0, None
    length = 1 << (reference.size + simulation.size - 2).bit_length()
    # FFT correlation at positive index k = sum_i ref[i] * sim[i+k].
    products = np.fft.irfft(np.fft.rfft(s, length) * np.conj(np.fft.rfft(r, length)), length)
    rsum = np.concatenate(([0.0], np.cumsum(r)))
    ssum = np.concatenate(([0.0], np.cumsum(s)))
    rpower = np.concatenate(([0.0], np.cumsum(r * r)))
    spower = np.concatenate(([0.0], np.cumsum(s * s)))
    best, best_corr = 0, None
    # Retain at least half of either short input; a two-sample tail
    # must not win an apparently perfect correlation by discarding the audio.
    maximum = min(maximum, min(r.size, s.size) // 2)
    for lag in range(-maximum, maximum + 1):
        r0, s0 = max(0, -lag), max(0, lag)
        count = min(r.size - r0, s.size - s0)
        rtotal = rsum[r0 + count] - rsum[r0]
        stotal = ssum[s0 + count] - ssum[s0]
        re = max(0.0, rpower[r0 + count] - rpower[r0] - rtotal * rtotal / count)
        se = max(0.0, spower[s0 + count] - spower[s0] - stotal * stotal / count)
        denominator = math.sqrt(re * se)
        if denominator <= 1e-24:
            continue
        corr = float(np.clip((products[lag % length] - rtotal * stotal / count) / denominator, -1.0, 1.0))
        if best_corr is None or abs(corr) > abs(best_corr) + 1e-10 or (
            abs(abs(corr) - abs(best_corr)) <= 1e-10 and abs(lag) < abs(best)
        ):
            best, best_corr = lag, corr
    return best, best_corr


def spectrum(signal: np.ndarray, rate: int) -> tuple[np.ndarray, dict]:
    if signal.size < 2:
        return np.zeros(1), {"centroid_hz": None, "dominant_spectral_bin_hz": None, "bands_power_fraction": {}}
    size = min(2048, signal.size)
    size = 1 << (size.bit_length() - 1)
    window = np.hanning(size)
    positions = np.arange(0, signal.size - size + 1, max(1, size // 2), dtype=np.int64)
    if positions.size > 128:
        positions = positions[np.linspace(0, positions.size - 1, 128, dtype=int)]
    power = np.zeros(size // 2 + 1)
    for start in positions:
        frame = signal[start:start + size]
        power += np.abs(np.fft.rfft((frame - frame.mean()) * window)) ** 2
    power /= max(1, positions.size)
    frequencies = np.fft.rfftfreq(size, 1.0 / rate)
    total = float(power.sum())
    edges = (0, 250, 1000, 4000, 10000, rate / 2 + 1)
    bands = {}
    for low, high in zip(edges, edges[1:]):
        if low >= rate / 2:
            continue
        selected = (frequencies >= low) & (frequencies < high)
        bands[f"{low:g}-{min(high, rate / 2):g}_Hz"] = float(power[selected].sum() / total) if total else 0.0
    return power, {
        "centroid_hz": float(np.dot(frequencies, power) / total) if total else None,
        "dominant_spectral_bin_hz": float(frequencies[int(np.argmax(power))]) if total else None,
        "fft_bin_width_hz": rate / size,
        "window": "Hann",
        "window_size_frames": size,
        "windows_analyzed": int(positions.size),
        "bands_power_fraction": bands,
    }


def compare(reference_path: Path, simulation_path: Path, *, label: str | None = None,
            maximum_lag_ms: float = 20.0) -> dict:
    rate, reference, reference_metrics = read_pcm16(reference_path)
    simulation_rate, simulation, simulation_metrics = read_pcm16(simulation_path)
    if rate != simulation_rate:
        raise ValueError("Sample rates differ. Render both at the same rate; this comparator does not resample.")
    lag, correlation = best_lag(reference, simulation, round(maximum_lag_ms * rate / 1000))
    raw_r, raw_s = overlap(reference, simulation, 0)
    r, s = overlap(reference, simulation, lag)
    report = {
        "label": label,
        "reference": reference_metrics,
        "simulation": simulation_metrics,
        "unadjusted": {
            "duration_difference_seconds_simulation_minus_reference": (simulation.size - reference.size) / rate,
            "frame_difference_simulation_minus_reference": int(simulation.size - reference.size),
            "overlap_frames_at_zero_lag": int(raw_r.size),
            "rmse_full_scale_at_zero_lag": float(np.sqrt(np.mean((raw_r - raw_s) ** 2))) if raw_r.size else None,
            "dc_centered_correlation_at_zero_lag": _correlation(raw_r, raw_s),
        },
        "aligned": {
            "search_limit_ms": maximum_lag_ms,
            "simulation_delay_samples": lag,
            "simulation_delay_ms": 1000 * lag / rate,
            "lag_sign_definition": "Positive means simulation is delayed; compare reference[i] with simulation[i+lag].",
            "overlap_frames": int(r.size),
            "discarded_reference_frames": int(reference.size - r.size),
            "discarded_simulation_frames": int(simulation.size - s.size),
            "dc_centered_correlation": correlation,
            "absolute_correlation": abs(correlation) if correlation is not None else None,
        },
        "limitations": [
            "Software emulation is an independent reference, not a physical hardware oracle.",
            "Original durations, DC, levels, clipping and unadjusted error are retained separately from fitted metrics.",
            "One bounded global offset is fitted; this does not measure timing drift or correct rate differences.",
            "Periodic tones can have several equivalent correlation peaks; a lag can reflect waveform phase rather than event timing.",
            "Gain and polarity fitting can conceal amplitude defects; inspect the original levels and fitted gain together.",
            "Spectral-bin peaks are not fundamental-pitch estimates; inspect register evidence or a dedicated pitch measurement.",
            "No universal pass threshold or hardware/acoustic-equivalence verdict is assigned.",
        ],
    }
    aligned = report["aligned"]
    if not r.size:
        aligned.update(gain_simulation_to_reference=None, polarity=None,
                       gain_matched_rmse_full_scale=None, gain_matched_snr_db=None)
        report["spectral"] = None
        report["envelope"] = None
        return report
    r = r - r.mean()
    s = s - s.mean()
    energy = float(np.dot(s, s))
    gain = float(np.dot(r, s) / energy) if energy > 1e-24 else None
    error = r - gain * s if gain is not None else r
    reference_rms = float(np.sqrt(np.mean(r * r)))
    error_rms = float(np.sqrt(np.mean(error * error)))
    aligned.update(
        gain_simulation_to_reference=gain,
        polarity=("inverted" if gain < 0 else "same") if gain is not None and gain != 0 else None,
        reference_dc_centered_rms_full_scale=reference_rms,
        simulation_dc_centered_rms_full_scale=float(np.sqrt(np.mean(s * s))),
        gain_matched_rmse_full_scale=error_rms,
        gain_matched_snr_db=(20 * math.log10(reference_rms / error_rms)) if reference_rms > 0 and error_rms > 0 else None,
        exact_dc_centered_match_after_gain=bool(error_rms <= 1e-14 and reference_rms > 0),
    )
    block = max(1, round(rate * 0.010))
    blocks = r.size // block
    if blocks:
        re = np.sqrt(np.mean(r[:blocks * block].reshape(blocks, block) ** 2, axis=1))
        se = np.sqrt(np.mean(s[:blocks * block].reshape(blocks, block) ** 2, axis=1))
        report["envelope"] = {
            "window_ms": block * 1000 / rate,
            "complete_windows": int(blocks),
            "rms_envelope_correlation": _correlation(re, se),
            "gain_matched_rms_envelope_rmse_full_scale": float(np.sqrt(np.mean((re - abs(gain or 0) * se) ** 2))),
        }
    else:
        report["envelope"] = None
    rp, rmetrics = spectrum(r, rate)
    sp, smetrics = spectrum(s, rate)
    denominator = float(np.linalg.norm(rp) * np.linalg.norm(sp))
    amplitude_denominator = float(np.linalg.norm(np.sqrt(rp)) * np.linalg.norm(np.sqrt(sp)))
    report["spectral"] = {
        "reference": rmetrics,
        "simulation": smetrics,
        "mean_power_spectrum_cosine": float(np.dot(rp, sp) / denominator) if denominator > 1e-24 else None,
        "mean_amplitude_spectrum_cosine": float(np.dot(np.sqrt(rp), np.sqrt(sp)) / amplitude_denominator) if amplitude_denominator > 1e-24 else None,
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("simulation", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--label")
    parser.add_argument("--maximum-lag-ms", type=float, default=20.0)
    args = parser.parse_args()
    if not math.isfinite(args.maximum_lag_ms) or not 0 <= args.maximum_lag_ms <= 1000:
        parser.error("maximum lag must be finite and between 0 and 1000 ms")
    for source in (args.reference, args.simulation):
        if args.output.resolve() == source.resolve() or (args.output.exists() and source.exists() and args.output.samefile(source)):
            parser.error("report output must not overwrite an input WAV")
    try:
        report = compare(args.reference, args.simulation, label=args.label,
                         maximum_lag_ms=args.maximum_lag_ms)
    except (ValueError, wave.Error, OSError) as exc:
        parser.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    aligned = report["aligned"]
    print(f"{args.label or args.reference.stem}: lag={aligned['simulation_delay_ms']:.3f} ms, "
          f"correlation={aligned['dc_centered_correlation']}, gain={aligned['gain_simulation_to_reference']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
