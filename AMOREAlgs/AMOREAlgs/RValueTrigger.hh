#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/RVCore.hh"
#include "AMORE/amoreconsts.hh"

// Per-channel R-value (Pearson correlation) trigger.
// Bandpass-filters the streaming data and computes the normalized
// cross-correlation with a pre-loaded signal template (see RVCore).
// Fires when a local maximum of the R-value exceeds fTHR[ch]*1e-3.
//
// Config (AMOREADCConf / YAML):
//   BWF_ORDER, BWF_LC, BWF_UC  — Butterworth bandpass parameters
//   RV_DS                      — downsampling factor before filtering (default 8)
//   RV_NWIN                    — template window length in downsampled samples (default 100)
//   RV_TMPLT                   — path to ROOT template file (TH1D: "htmp%02d")
//   THR                        — R-value threshold × 1000 (e.g. 500 → 0.500)
class RValueTrigger : public AbsSWTrigger {
public:
  RValueTrigger();
  RValueTrigger(const char * name);
  virtual ~RValueTrigger() = default;

  // RV and ARV at the moment the last trigger fired, per channel.
  double GetLastRV(int ch)  const { return fCore[ch].LastRV(); }
  double GetLastARV(int ch) const { return fCore[ch].LastARV(); }

  // Peak RV accumulated since the last trigger (useful for monitoring / diagnostics).
  double GetRVPeak(int ch)  const { return fCore[ch].RVPeak(); }
  double GetARVPeak(int ch) const { return fCore[ch].ARVPeak(); }

  // Most recently computed RV and ARV (updated every DS-th raw sample).
  double GetCurrentRV(int ch)  const { return fCore[ch].CurrentRV(); }
  double GetCurrentARV(int ch) const { return fCore[ch].CurrentARV(); }

  // Float-precision baseline (for high-precision input path via EvalChannelD).
  // Set via SetBaselinesD(); defaults to 0.0 (no subtraction).
  void SetBaselinesD(const double * vals, int nch) {
    for (int i = 0; i < nch && i < AMORE::kNCHPERADC; ++i) fBaselineD[i] = vals[i];
  }

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

  // Float-precision variant of EvalChannel — accepts a pre-scaled double ADC value
  // (e.g. raw_uint32 * 0.25) and subtracts fBaselineD[ch].  Used by test harnesses
  // that need to match the precision of read_amod.C (which uses 0.25× float multiply).
  bool EvalChannelD(int ch, double adcVal);

private:
  // Per-channel float baseline used by EvalChannelD.
  double fBaselineD[AMORE::kNCHPERADC]{};

  RVCore fCore[AMORE::kNCHPERADC];
};
