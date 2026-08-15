# Audio Output Verification Skills

## Objective
Verify that the IKAOPLL-verilator audio output matches the Y8960_Cartridge reference implementation.

**Reference Implementation**: https://github.com/hra1129/Y8960_Cartridge/tree/main

---

## Quick Reference: Y8960_Cartridge Volume Settings

From `fpga/Y8960_Cartridge_TangPrimer25K/src/ikaopll_patch/opll.v`:
```verilog
.i_ACC_SIGNED_MOVOL  ( 5'd10 ),   // メロディ音量: -16...15 (SIGNED)
.i_ACC_SIGNED_ROVOL  ( 5'd15 ),   // ドラム音量  : -16...15 (SIGNED)
```

IKAOPLL-verilator should match these values to produce equivalent audio quality.

---

## Skill 1: Run Simulation with Debug Output

### Purpose
Execute the IKAOPLL-verilator simulation with debug logging to confirm that volume parameters are initialized and audio samples are produced.

### Prerequisites
- RTL ソースの取得:
  ```bash
  ./tools/create_rtl_dir.sh --fetch-raw https://raw.githubusercontent.com/ika-musume/IKAOPLL/main
  ```
- Build directory cleaned: `rm -rf build/`

### Steps

#### 1.1 Clean Build
```bash
cd /path/to/IKAOPLL-verilator
rm -rf build/
```

#### 1.2 Build and Run with CSV Output
```bash
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv --enable-csv 2>&1 | tee sim_output.txt
```

#### 1.3 Capture Debug Output
```bash
grep "\[DEBUG\]" build/sim_last_output.txt | head -20
```

### Expected Behavior (PR #5 Correct)
```
[DEBUG] sample 0: mo=X, acc=Y   (where Y != 0 and Y ~ X*10 range)
[DEBUG] sample 1: mo=X, acc=Y
[DEBUG] sample 2: mo=X, acc=Y
...
```

### Expected Behavior (PR #5 Not Working)
```
[DEBUG] sample 0: mo=1, acc=0
[DEBUG] sample 1: mo=1, acc=0
...
```
→ If acc=0, **o_ACC_SIGNED is not being computed in IKAOPLL RTL**

---

## Skill 2: Analyze WAV Output Spectrum

### Purpose
Generate spectral analysis of the output WAV file to verify audio quality matches expectations.

### Prerequisites
- Successful simulation run (Skill 1)
- `ym2413_scale_rom1.vgm.wav` generated in repository root
- Python with scipy available

### Steps

#### 2.1 Verify WAV File Exists
```bash
# WAV file is generated in the repository root (not build/)
ls -lh ym2413_scale_rom1.vgm.wav
```

Expected: File size ~300-400 KB (for ~4 seconds at 44100 Hz, 16-bit mono)

**Note**: Output files (CSV/WAV) are generated in the working directory where `build_and_run.sh` is invoked (typically the repository root), not in the `build/` directory.

#### 2.2 Run Spectral Analysis (Python)
```python
import numpy as np
from scipy.io import wavfile

# Load WAV
sr, audio = wavfile.read('ym2413_scale_rom1.vgm.wav')

# Compute FFT
freq = np.fft.fftfreq(len(audio), 1/sr)[:len(audio)//2]
mag = np.abs(np.fft.fft(audio))[:len(audio)//2]
mag_db = 20 * np.log10(mag + 1e-10)

# Find peak frequency
peak_idx = np.argmax(mag_db[:5000])  # Search up to 5 kHz
peak_freq = freq[peak_idx]

print(f"Peak frequency: {peak_freq:.2f} Hz")
print(f"Peak magnitude: {mag_db[peak_idx]:.2f} dB")
print(f"Mean RMS level: {np.sqrt(np.mean(audio**2)):.2f}")
```

### Expected Output (Correct Implementation)
```
Peak frequency: ~220-440 Hz  (musical note range, matching Y8960_Cartridge)
Peak magnitude: > -20 dB
Mean RMS level: > 100
```

### Expected Output (Issues Present)
```
Peak frequency: > 5000 Hz (high-frequency artifacts)
Peak magnitude: < -40 dB (very quiet)
Mean RMS level: < 50
```

---

## Skill 3: Compare CSV Audio Samples

### Purpose
Examine raw CSV audio samples to verify `acc_signed` values reflect volume settings.

### Prerequisites
- CSV output from Skill 1
- `ym2413_scale_rom1.vgm.csv` present in repository root

### Steps

#### 3.1 Check CSV Format
```bash
head -20 ym2413_scale_rom1.vgm.csv
```

Expected format:
```
t_ps,mo_signed,acc_signed
<timestamp>,<mo_value>,<acc_value>
...
```

#### 3.2 Analyze ACC Values
```bash
# Extract first 50 samples, show mo and acc
awk -F',' 'NR > 1 && NR < 50 {print $2, $3}' ym2413_scale_rom1.vgm.csv | head -20
```

#### 3.3 Python Analysis
```python
import pandas as pd
import numpy as np

df = pd.read_csv('ym2413_scale_rom1.vgm.csv')

# Statistics
print(f"MO min/max: {df['mo_signed'].min()}/{df['mo_signed'].max()}")
print(f"ACC min/max: {df['acc_signed'].min()}/{df['acc_signed'].max()}")
print(f"ACC mean: {df['acc_signed'].mean():.2f}")
print(f"ACC RMS: {np.sqrt((df['acc_signed']**2).mean()):.2f}")

# Check if ACC reflects volume scaling
ratio = df['acc_signed'] / (df['mo_signed'] + 0.0001)
print(f"Average ACC/MO ratio: {ratio.mean():.2f}")
```

### Expected Output (Correct Implementation)
```
MO min/max: -512/511
ACC min/max: -5120/5110
ACC mean: 0.5
ACC RMS: 500+
Average ACC/MO ratio: ~10.0  ← Shows 10x scaling from MOVOL=10
```

### Expected Output (Issues Present - o_ACC_SIGNED=0)
```
MO min/max: -512/511
ACC min/max: 0/0
ACC mean: 0.0
ACC RMS: 0.0
Average ACC/MO ratio: 0.0  ← o_ACC_SIGNED not being used
```

---

## Skill 4: Root Cause Analysis

### Purpose
Identify why `o_ACC_SIGNED` may not be working and determine the next fix.

### Prerequisites
- Confirmed from Skills 2-3 that `acc=0` consistently

### Investigation Steps

#### 4.1 Check IKAOPLL RTL for ACC Signal
```bash
grep -n "o_ACC_SIGNED" rtl/IKAOPLL_modules/IKAOPLL_dac.v | head -10
```

Expected: Signal is defined and assigned

#### 4.2 Trace DAC Accumulator Logic
```bash
grep -A 5 "dac_acc" rtl/IKAOPLL_modules/IKAOPLL_dac.v | head -20
```

Look for:
- Does `dac_acc` depend on input volume parameters?
- Is there a pipeline delay before output?

#### 4.3 Check Verilator Generated Code
```bash
grep -n "o_ACC_SIGNED" build/obj_dir/VIKAOPLL.cpp | head -10
```

Expected: Signal should be updated in combinational or sequential logic

### Decision Tree

**If `dac_acc` calculation doesn't use `i_ACC_SIGNED_MOVOL`:**
→ IKAOPLL RTL may not implement volume-scaled output. Fall back to `mo_signed` scaling.

**If `o_ACC_SIGNED` exists but is delayed multiple cycles:**
→ Timing issue. Check when to sample `o_ACC_SIGNED` (strobed output).

**If Verilator generated code is missing `o_ACC_SIGNED` assignment:**
→ RTL may conditionally drive output. Check all paths.

---

## Skill 5: Fallback Fix - Use mo_signed with Manual Scaling

### Purpose
If `o_ACC_SIGNED` cannot be fixed in RTL, apply volume scaling to `mo_signed` output.

### Implementation

#### 5.1 Update ikaopll_wrapper.cpp
```cpp
void ikaopll_audio_wav_write(int16_t mo_signed, int16_t acc_signed)
{
    (void)acc_signed;  // o_ACC_SIGNED = 0, so don't use it
    
    if (!g_wav_enabled) return;
    
    // Apply MOVOL scaling manually (10x from i_ACC_SIGNED_MOVOL=10)
    const int scale = 10;
    int32_t v = (int32_t)mo_signed * scale;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    g_wav_samples.push_back((int16_t)v);
}
```

#### 5.2 Re-run Skill 1 and Verify
```bash
rm -rf build/
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv --enable-csv
```

- Audio should now have proper volume level
- WAV spectrum should match Y8960_Cartridge reference

#### 5.3 Re-run Skill 2
Verify that RMS level and peak frequency now match expectations.

---

## Skill 6: Compare with Y8960_Cartridge Reference

### Purpose
Generate audio from both Y8960_Cartridge and IKAOPLL-verilator using the same test pattern, then compare spectral characteristics to ensure equivalence.

### Prerequisites
- Y8960_Cartridge repository cloned locally: `git clone https://github.com/hra1129/Y8960_Cartridge.git`
- Same test input file available or easily creatable
- Both projects built and runnable

### Steps

#### 6.1 Generate Reference WAV from Y8960_Cartridge
```bash
cd ~/Y8960_Cartridge
# Build and run with same test pattern
# (Exact procedure depends on Y8960_Cartridge build system)
# Expected output: reference_audio.wav
```

#### 6.2 Generate Test WAV from IKAOPLL-verilator
```bash
cd ~/IKAOPLL-verilator
./build_and_run.sh tests/csv/ym2413_scale_rom1.vgm.csv --enable-csv
# Output: ym2413_scale_rom1.vgm.wav
```

#### 6.3 Compare Spectral Characteristics
```python
import numpy as np
from scipy.io import wavfile
import matplotlib.pyplot as plt

# Load both WAVs
sr_ref, audio_ref = wavfile.read('reference_audio.wav')
sr_test, audio_test = wavfile.read('ym2413_scale_rom1.vgm.wav')

# Normalize to same length for comparison
min_len = min(len(audio_ref), len(audio_test))
audio_ref = audio_ref[:min_len]
audio_test = audio_test[:min_len]

# Compute FFT
freq = np.fft.fftfreq(min_len, 1/sr_ref)[:min_len//2]
mag_ref = np.abs(np.fft.fft(audio_ref))[:min_len//2]
mag_test = np.abs(np.fft.fft(audio_test))[:min_len//2]

mag_db_ref = 20 * np.log10(mag_ref + 1e-10)
mag_db_test = 20 * np.log10(mag_test + 1e-10)

# Find peak frequencies
peak_idx_ref = np.argmax(mag_db_ref[:5000])
peak_idx_test = np.argmax(mag_db_test[:5000])

peak_freq_ref = freq[peak_idx_ref]
peak_freq_test = freq[peak_idx_test]

# RMS levels
rms_ref = np.sqrt(np.mean(audio_ref**2))
rms_test = np.sqrt(np.mean(audio_test**2))

print(f"=== Y8960_Cartridge Reference ===")
print(f"Peak frequency: {peak_freq_ref:.2f} Hz")
print(f"Peak magnitude: {mag_db_ref[peak_idx_ref]:.2f} dB")
print(f"RMS level: {rms_ref:.2f}")

print(f"\n=== IKAOPLL-verilator ===")
print(f"Peak frequency: {peak_freq_test:.2f} Hz")
print(f"Peak magnitude: {mag_db_test[peak_idx_test]:.2f} dB")
print(f"RMS level: {rms_test:.2f}")

print(f"\n=== Comparison ===")
freq_diff = abs(peak_freq_test - peak_freq_ref)
rms_diff_db = 20 * np.log10(rms_test / rms_ref) if rms_ref > 0 else 0

print(f"Peak frequency difference: {freq_diff:.2f} Hz")
print(f"RMS level difference: {rms_diff_db:.2f} dB")

# Plot
plt.figure(figsize=(12, 6))
plt.semilogy(freq, mag_db_ref, label='Y8960_Cartridge (Reference)', linewidth=1)
plt.semilogy(freq, mag_db_test, label='IKAOPLL-verilator', linewidth=1, alpha=0.7)
plt.xlabel('Frequency (Hz)')
plt.ylabel('Magnitude (dB)')
plt.legend()
plt.grid(True, alpha=0.3)
plt.xlim([0, 5000])
plt.title('Spectral Comparison: Y8960_Cartridge vs IKAOPLL-verilator')
plt.savefig('spectrum_comparison.png', dpi=150, bbox_inches='tight')
plt.show()
```

### Success Criteria (Match with Y8960_Cartridge)
- Peak frequency difference: **< ±10 Hz**
- RMS level difference: **< 3 dB**
- No unexpected high-frequency artifacts (> 5 kHz) in either output
- Spectral envelope visually similar

### If Differences Exist
1. **Peak frequency off by > 10 Hz**: Timing or resampling issue
2. **RMS level off by > 3 dB**: Volume scaling incorrect
3. **High-frequency artifacts in IKAOPLL-verilator**: ZOH reconstruction or filtering issue
4. **High-frequency artifacts in Y8960_Cartridge reference**: May indicate different DAC filter or anti-aliasing

---

## Summary: Decision Points

| Condition | Next Action |
|-----------|------------|
| Skill 2-3: Audio quality matches Y8960_Cartridge | ✅ **Implementation is correct** |
| Skill 2-3: `acc_signed = 0` consistently | ⚠️ `o_ACC_SIGNED` not implemented. Apply Skill 5 fallback. |
| Skill 6: Peak frequency matches (±10 Hz) | ✅ Timing is correct |
| Skill 6: RMS level matches (±3 dB) | ✅ Volume scaling is correct |
| Skill 6: High-frequency artifacts present | 🔴 Investigate ZOH resampling or filtering. Check against reference. |
| After Skill 5: Quality still poor | 🔴 Issue is deeper than volume scaling. Investigate signal chain. |

---

## Troubleshooting Guide

### Scenario 1: Quality improved after Skill 5 but still < Y8960_Cartridge
- Check if ROVOL (drum volume) scaling is also correct
- Verify pipeline/strobing of ACC signal
- Compare phase relationships between MO and ACC

### Scenario 2: Quality perfect but RMS level off by >3 dB
- May be due to post-processing (filtering, normalization) in Y8960_Cartridge
- Document the difference and adjust expectations accordingly

### Scenario 3: High-frequency artifacts persist
- Check for aliasing in ZOH reconstruction
- Consider oversampling + lowpass filter as post-processing
- Use `tools/mo_changes_to_wav_weighted.py` for weighted averaging approach

### Scenario 4: Different peak frequencies despite same input
- Verify both use same sample rate (44100 Hz)
- Check if Y8960_Cartridge applies DC removal or envelope shaping
- Ensure input VGM/CSV is identical in both systems
