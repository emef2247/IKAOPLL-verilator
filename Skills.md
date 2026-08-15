# Audio Output Verification Skills for PR #5

## Objective
Verify that PR #5 (IKAOPLL testbench volume initialization fix) correctly produces audio output matching the Y8960_Cartridge reference implementation.

---

## Skill 1: Run Simulation with Debug Output

### Purpose
Execute the IKAOPLL-verilator simulation with debug logging to confirm that volume parameters are initialized and audio samples are produced.

### Prerequisites
- Build directory cleaned: `rm -rf build/`
- PR #5 merged into main branch

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
- `ym2413_scale_rom1.vgm.wav` generated in build directory
- Python with librosa/scipy available

### Steps

#### 2.1 Verify WAV File Exists
```bash
ls -lh build/ym2413_scale_rom1.vgm.wav
```

Expected: File size > 1 MB (for ~4 seconds at 44100 Hz)

#### 2.2 Run Spectral Analysis (Python)
```python
import numpy as np
from scipy import signal
from scipy.io import wavfile

# Load WAV
sr, audio = wavfile.read('build/ym2413_scale_rom1.vgm.wav')

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

### Expected Output (PR #5 Correct)
```
Peak frequency: ~220-440 Hz  (musical note range)
Peak magnitude: > -20 dB
Mean RMS level: > 100
```

### Expected Output (PR #5 Issue)
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
- `build/audio_samples.csv` present

### Steps

#### 3.1 Check CSV Format
```bash
head -20 build/audio_samples.csv
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
awk -F',' 'NR > 1 && NR < 50 {print $2, $3}' build/audio_samples.csv | head -20
```

#### 3.3 Python Analysis
```python
import pandas as pd

df = pd.read_csv('build/audio_samples.csv')

# Statistics
print(f"MO min/max: {df['mo_signed'].min()}/{df['mo_signed'].max()}")
print(f"ACC min/max: {df['acc_signed'].min()}/{df['acc_signed'].max()}")
print(f"ACC mean: {df['acc_signed'].mean():.2f}")
print(f"ACC RMS: {np.sqrt((df['acc_signed']**2).mean()):.2f}")

# Check if ACC reflects volume scaling
ratio = df['acc_signed'] / (df['mo_signed'] + 0.0001)
print(f"Average ACC/MO ratio: {ratio.mean():.2f}")
```

### Expected Output (PR #5 Correct)
```
MO min/max: -512/511
ACC min/max: -5120/5110
ACC mean: 0.5
ACC RMS: 500+
Average ACC/MO ratio: ~10.0  ← Shows 10x scaling from MOVOL=10
```

### Expected Output (PR #5 Issue - o_ACC_SIGNED=0)
```
MO min/max: -512/511
ACC min/max: 0/0
ACC mean: 0.0
ACC RMS: 0.0
Average ACC/MO ratio: 0.0  ← o_ACC_SIGNED not implemented
```

---

## Skill 4: Root Cause Analysis

### Purpose
Identify why `o_ACC_SIGNED` may not be working and determine the next fix.

### Prerequisites
- Confirmed from Skills 2-3 that `acc=0`

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
- Audio should now have proper volume level
- WAV spectrum should match Y8960_Cartridge reference

---

## Summary: Decision Points

| Condition | Next Action |
|-----------|------------|
| `acc_signed != 0` | ✅ PR #5 is working. Verify against reference audio. |
| `acc_signed = 0` | ⚠️ `o_ACC_SIGNED` not implemented. Apply Skill 5 fallback. |
| WAV RMS < 50 after fixes | 🔴 Investigate additional scaling or pipeline delays. |
| Spectrum peak > 5 kHz | 🔴 High-frequency artifacts remain. Check ZOH resampling. |

