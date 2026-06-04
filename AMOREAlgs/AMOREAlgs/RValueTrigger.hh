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

  // Streaming state per channel
  std::deque<double> fFiltBuf[AMORE::kNCHPERADC]; // filtered sample buffer (newest at [0])
  double fRunSum[AMORE::kNCHPERADC]{};
  double fRunSum2[AMORE::kNCHPERADC]{};
  int    fDSCounter[AMORE::kNCHPERADC]{};

  // Last two R-values for local-maximum detection
  double fRVPrev[AMORE::kNCHPERADC]{};
  double fRVPrevPrev[AMORE::kNCHPERADC]{};
};
