# R-value Trigger (RVH / RVL) — Pearson Correlation Software Trigger

## Overview

The R-value trigger is a condition (`RVCondition`, type `RV`) of the single signal
trigger path `SignalTrigger` in the AMoRE online DAQ; the algorithm itself lives in
`RVCore` (one engine per crystal and condition, e.g. RVH on heat, RVL on light).
It fires when the streaming ADC signal strongly resembles a pre-loaded reference
pulse shape (template).  The resemblance is measured by the **Pearson correlation
coefficient** (called **R-value** or **RV**), computed over a sliding window of
bandpass-filtered, downsampled samples.

A secondary quantity, **ARV** (Amplitude R-value), captures the signal amplitude
relative to the template.  An optional ellipse cut in the RV–ARV plane provides
background rejection beyond a simple threshold.

The algorithm reproduces the online trigger logic of the offline analysis script
`read_amod.C`, using the same template alignment and filter coefficients.

---

## Physics context

AMoRE detectors are bolometric crystal calorimeters that produce two signal types:

| Channel type | Signal | ADC prefix | Typical bandwidth |
|---|---|---|---|
| Phonon (heat) | thermal pulse, slow (~10 ms rise) | `htmp` | 150–2000 Hz |
| Photon (light) | scintillation, faster | `ltmp` | 800–2400 Hz |

The R-value trigger runs independently for each channel type with its own
Butterworth bandpass filter and template.

---

## Signal processing pipeline

```
Raw ADC samples (100 kHz, uint16/uint32)
        │
        │  baseline subtraction
        ▼
  [every fDS raw samples — decimation]
        │
        │  Butterworth bandpass IIR filter  (streaming, single sample)
        ▼
  Filtered sample → circular buffer  [fNWin slots]
        │
        ├──▶  Running sum / sum-of-squares  →  window mean & std (σ)
        │
        └──▶  Dot product with template deviation vector  →  cross-correlation (cc)
                        │
                        ▼
              RV  =  cc / (σ · σ_template · fNWin)   ∈ [−1, 1]
              ARV =  σ / σ_template
                        │
                        ▼
              Local-maximum detection (3-point)
                        │
                   RV_peak > THR  ?
                        │
              Ellipse cut (optional):  RV² / THRY + ARV² / THRX > 1
                        │
                      FIRE
```

### 1. Downsampling

Every `fDS` raw samples, one filtered sample is computed.  The effective sample
rate seen by the trigger is `SR / fDS`.

- Heat default: `fDS = 8`, SR = 100 kHz → effective 12.5 kHz
- Light default: `fDS = 4`, SR = 100 kHz → effective 25 kHz

### 2. Butterworth bandpass filter

A Butterworth IIR bandpass filter (`ButterworthFilter`) processes one downsampled
sample at a time using the Direct Form II transposed structure.  The same filter
object is used for both template preparation (batch) and online trigger (streaming).

Config parameters:

| Parameter | YAML key | Meaning |
|---|---|---|
| `BWF_ORDER` | `BWFH` / `BWFL` | Filter order (default 2) |
| `BWF_LC` | `BWFH` / `BWFL` | Lower cutoff frequency [Hz] |
| `BWF_UC` | `BWFH` / `BWFL` | Upper cutoff frequency [Hz] |

### 3. Sliding window and R-value

A circular buffer of length `fNWin` (default 100 downsampled samples) holds the
most recent filtered samples.  Running sums are maintained for O(1) mean and
variance:

```
fRunSum  = Σ buf[i]
fRunSum2 = Σ buf[i]²
σ        = sqrt(fRunSum2/N − (fRunSum/N)²)
```

The cross-correlation with the template is a dot product:

```
cc = Σ_{k=0}^{N−1}  buf[(head+k) % N]  ×  tdev[k]
```

where `tdev[k] = hftmp[N−1−k] − mean_template` (stored reversed for
cache-friendly sequential access).

The **R-value** is the Pearson correlation coefficient:

```
RV = cc / (σ · σ_template · N)
```

This equals `Cov(signal_window, template) / (σ_signal · σ_template)`, with the
signal mean cancelled algebraically because `Σ tdev[k] = 0` by construction.
RV ∈ [−1, 1]; a perfect shape match gives RV = 1.

The **ARV** (Amplitude R-value) is the ratio of window standard deviation to
template standard deviation:

```
ARV = σ / σ_template
```

It is a dimensionless proxy for signal amplitude: ARV = 1 means the signal has
the same amplitude as the template.

### 4. Local maximum detection

The trigger fires only at the peak of an RV excursion, not on every sample above
threshold.  A 3-point local maximum check with a 2-sample lag (matching
`read_amod.C`) is used:

```
RV_prev_prev  ≤  RV_prev  ≥  RV_current
```

When this condition holds, `RV_prev` was a local peak.  The ARV recorded at the
RV peak (`ARV_peak`) is saved for the ellipse cut.

### 5. Threshold and ellipse cut

Two independent gates must both pass for a trigger to fire:

**RV threshold** (required):
```
RV_peak > THR × 1e−3
```
`THR` is an integer configured per channel (e.g., `THR = 500` → threshold 0.500).

**Ellipse cut** (optional, disabled when either threshold is zero):
```
RV_peak² / THRY  +  ARV_peak² / THRX  > 1
```

`THRY = (RV_THR × 1e−3)²`, `THRX = (ARV_THR × 1e−3)²`.
This keeps events *outside* the ellipse in RV–ARV space — i.e., those with
sufficiently high correlation AND/OR sufficient amplitude.  Events with both low
RV and low ARV (noise) are rejected.

---

## Template preparation (`PrepareAlgo`)

The template is loaded from a ROOT file containing averaged, summed pulse
histograms.  The following steps are performed once at startup per channel:

### Step 1 — Load histogram

A `TH1D` named `{prefix}{xid:02d}` is read from the template ROOT file.  Bin
contents are scaled by 0.25 (converts 32-bit ADC units to the 14-bit working
range).

Config: `RV_TMPLT` (file path), `RV_TMPLT_PFX` (histogram prefix, default `htmp`).

### Step 2 — Find the 50% rise point (fr50)

A narrow 2nd-order Butterworth bandpass (1–3 kHz) is applied to the full
histogram to locate the pulse peak without noise.  The fr50 index is the last
sample where the filtered pulse crosses 50% of its maximum on the rising edge,
scanning backward from the peak.

For light channels, the **heat** histogram is used for this step
(`RV_TMPLT_PFX_FR50 = htmp`) to share the same timing reference as the heat
trigger — matching `read_amod.C` behaviour.

### Step 3 — Extract and filter the template window

Starting 60 full-rate samples before fr50, `fNWin` samples are taken by
downsampling (stride `fDS`).  This `fNWin`-sample raw window is then passed
through the same Butterworth filter (batch mode) to produce the filtered template
`hftmp`.

### Step 4 — Normalise

Mean and standard deviation of `hftmp` are computed:

```
tdev[k] = hftmp[N−1−k] − mean_template    (stored reversed)
σ_template = sqrt( Σ tdev[k]² / N )
```

Storing `tdev` in reversed order means the online dot product accesses both the
circular buffer and the template array in forward-sequential order (cache-friendly,
auto-vectorisable by the compiler).

---

## Configuration reference

Per ADC in the yml (`AMOREADC`), the conditions of the signal path go in `TRGCOND`:

```yaml
TRGMODE: [RandomTrigger, SignalTrigger]   # pedestal path + signal path
RV_TMPLT: /path/T000576.root             # template file (ADC-wide)
DLY: 13000                               # pre-trigger samples = RV warmup
DT: [1000]                               # crystal dead time = phonon channel DT
TRGCOND:
  RVH: { TRGON: [1], THR: [940], THRY: [1], THRX: [1], BWF: [1, 150, 2000], NWIN: 200, DS: 8 }
  RVL: { TRGON: [1], THR: [990], BWF: [2, 800, 2400], NWIN: 60, DS: 4 }
```

| Key (in `TRGCOND.<name>`) | Default | Description |
|---|---|---|
| `TRGON` | 0 | Per crystal (8 per ADC) enable |
| `THR` | — | Per crystal RV threshold × 1000 |
| `THRY` / `THRX` | 0 (off) | Per crystal ellipse axes, val × 1e-3 (cut RV²/THRY² + ARV²/THRX² > 1) |
| `BWF` | `[2, 0, 0]` | Butterworth `[order, lc, uc]` [Hz] at the downsampled rate |
| `NWIN` | 100 | Window length [downsampled samples] |
| `DS` | 8 | Downsampling factor |
| `TMPLT_PFX` | RVH `htmp`, RVL `ltmp` | Template histogram prefix (`<pfx><crystal id:02d>`) |
| `FR50_PFX` | RVL `htmp` | Histogram used to find the 50% rise (template cut point) |

Known names set `TYPE: RV`, the target channel and the trigger bit: RVH = heat
(phonon, 2x), bit 0; RVL = light (photon, 2x+1), bit 2. The template is looked up
by crystal id = `PID(phonon ch) / 2`. `SR` is [µs / sample]; the filters use
`1e6 / SR` Hz.

---

## Class structure

```
SignalTrigger         (AMOREAlgs/SignalTrigger.hh)   single signal path
│  - FIFO, baselines, per-crystal dead time, trgbit OR, waveform snapshot
│  - evaluates every condition on every sample
│
└── RVCondition       (AMOREAlgs/TrgCondition.hh)    type "RV" (RVH, RVL)
       - PrepareAlgo(): loads templates (RVTemplateFile), one RVCore per crystal
       - Eval(xtal, value) -> RVCore::Eval
       - GetLastRV / GetLastARV(xtal): values when the last trigger fired

RVCore                (AMOREAlgs/RVCore.hh / src/RVCore.cc)   ROOT-free engine
       - Prepare(par, template, fr50): builds tdev, initialises state
       - Eval(input): streaming filter, sliding correlation, local-max decision
       - LastRV / LastARV, CurrentRV / CurrentARV, RVPeak / ARVPeak
```

`ButterworthFilter` (`AMOREAlgs/ButterworthFilter.hh`) is embedded in each `RVCore`
and provides both a batch `Filter(n, in, out)` method (used during template
preparation) and a streaming `Filter(x)` method (used per sample in `RVCore::Eval`).

---

## Warmup

When `Prepare()` is first called, `fWarmupLeft` is set to `DLY` (raw
samples).  Until the countdown reaches zero, `RVCore::Eval` processes samples normally
(advancing the filter state and filling the buffer) but does not fire triggers.
This allows the IIR filter to settle and the sliding window to fill before any
decision is made.

---

## Template ROOT file format

```
template.root
├── htmp00    TH1D  — phonon (heat) template for crystal XID 00
├── htmp01    TH1D  — phonon (heat) template for crystal XID 01
├── ...
├── ltmp00    TH1D  — photon (light) template for crystal XID 00
└── ltmp01    TH1D  — photon (light) template for crystal XID 01
```

- Bin content: summed raw 32-bit ADC value (will be scaled by 0.25 internally).
- Number of bins: full-rate waveform length (e.g., 30000 at SR = 100 kHz, 300 ms).
- XID numbering is 0-based.
- The prefix and XID mapping per ADC channel are set via `SetRVTmpltHistPfx` and
  `SetCID(ch, xid)`.

---

## Usage (test harness)

`AMOREDAQ/test/rv_trigger_test` runs the trigger offline on HDF5 continuous-mode
data files:

```bash
rv_trigger_test  <config_file>  <template.root>  <output.h5>  <data1.h5> [data2.h5 ...]
```

Multiple HDF5 files are processed sequentially with all filter and trigger state
maintained across file boundaries, replicating a continuous run.

Output per trigger event (stdout):
```
# board  xid  ch  global_sample  trgtime_ms  trgbit  rv  arv
```

`trgbit` flags: `1` = heat (RVH), `4` = light (RVL), `5` = coincidence.

The output HDF5 file contains per-XID waveforms at `/{xid{NN}}/waveform [N, RL]`
with the trigger point at sample `DLY` within each waveform.

---

## Per-sample cost (hot path)

At `SR = 100 kHz` and `fDS = 8`, `RVCore::Eval` runs the full computation every
8 raw samples (12.5 kHz).  The dominant cost per call:

| Operation | Notes |
|---|---|
| IIR filter (1 sample) | order × 2 multiply-adds |
| Circular buffer update | 2 array writes + 2 add/subtract |
| Dot product (`fNWin` iterations) | ~100 FMAs, split into 2 contiguous loops |
| Running mean/variance | 2 divides + 1 `sqrt` |

The dot product uses two forward-sequential passes over contiguous `double` arrays
(`fFiltBuf` vector + reversed `fTdev` vector) so the compiler can auto-vectorise
with SIMD instructions.
