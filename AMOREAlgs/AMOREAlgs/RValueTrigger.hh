#pragma once

#include <deque>
#include <vector>

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/ButterworthFilter.hh"
#include "AMORE/amoreconsts.hh"

// Per-channel R-value (Pearson correlation) trigger.
// Bandpass-filters the streaming data and computes the normalized
// cross-correlation with a pre-loaded signal template.
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
  double GetLastRV(int ch)  const { return fLastRV[ch]; }
  double GetLastARV(int ch) const { return fLastARV[ch]; }

  // Peak RV accumulated since the last trigger (useful for monitoring / diagnostics).
  double GetRVPeak(int ch)  const { return fRVPeak[ch]; }
  double GetARVPeak(int ch) const { return fARVPeak[ch]; }

  // Most recently computed RV and ARV (updated every DS-th raw sample).
  double GetCurrentRV(int ch)  const { return fRVPrev[ch]; }
  double GetCurrentARV(int ch) const { return fARVPrev[ch]; }

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  // Per-channel Butterworth filter (streaming, at decimated rate SR/fDS)
  ButterworthFilter fFilter[AMORE::kNCHPERADC];

  int fNWin{100};
  int fDS{8};

  // Template statistics computed in PrepareAlgo
  std::vector<double> fTdev[AMORE::kNCHPERADC];  // template deviation from mean
  double fTstd[AMORE::kNCHPERADC]{};             // template std

  // Warmup countdown [raw samples]: mirrors "nprocessed >= fDELAY" in read_amod.C.
  // Trigger decisions are suppressed until the countdown reaches zero.
  int fWarmupLeft[AMORE::kNCHPERADC]{};

  // Streaming state per channel
  std::deque<double> fFiltBuf[AMORE::kNCHPERADC]; // filtered sample buffer (newest at [0])
  double fRunSum[AMORE::kNCHPERADC]{};
  double fRunSum2[AMORE::kNCHPERADC]{};
  int    fDSCounter[AMORE::kNCHPERADC]{};

  // RV/ARV history for local-maximum detection (same 2-sample lag as read_amod.C)
  double fRVPrev[AMORE::kNCHPERADC]{};
  double fRVPrevPrev[AMORE::kNCHPERADC]{};
  double fARVPrev[AMORE::kNCHPERADC]{};

  // Peak RV and ARV tracked since the last trigger (reset after each fire).
  // fARVPeak holds the ARV observed at the moment fRVPeak was last updated,
  // mirroring the a_rvh / a_arvh bookkeeping in read_amod.C.
  double fRVPeak[AMORE::kNCHPERADC]{};
  double fARVPeak[AMORE::kNCHPERADC]{};

  // Values at the last fired trigger (for external readout via GetLast*).
  double fLastRV[AMORE::kNCHPERADC]{};
  double fLastARV[AMORE::kNCHPERADC]{};
};
