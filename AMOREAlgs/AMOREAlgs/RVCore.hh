#pragma once

#include <string>
#include <vector>

#include "AMOREAlgs/ButterworthFilter.hh"

// Single-channel R-value (Pearson correlation with a signal template) engine.
// ROOT-free: the caller supplies template histograms as plain arrays.
//
// Shared by RValueTrigger (per-channel AbsSWTrigger path) and RVCondition
// (SignalTrigger condition), so the algorithm validated against read_amod.C
// lives in exactly one place.
struct RVParams {
  int    bwfOrder{2};
  double bwfLC{0};
  double bwfUC{0};
  int    nwin{100};      // template window [downsampled samples]
  int    ds{8};          // downsampling factor
  double sr{1e5};        // raw sampling rate [Hz]
  int    warmup{0};      // raw samples during which trigger decisions are suppressed
  double thr{0.9};       // RV threshold (fires when local-max RV > thr)
  double thry{0};        // ellipse RV²  axis (0 = disabled)
  double thrx{0};        // ellipse ARV² axis (0 = disabled)
};

class RVCore {
public:
  // tmpl:  full-rate template histogram contents (bin 1..N), already scaled to ADC units.
  // fr50:  histogram used to locate the 50% rise; nullptr -> use tmpl.
  // Returns false and fills err on failure.
  bool Prepare(const RVParams & par, const std::vector<double> & tmpl,
               const std::vector<double> * fr50, std::string & err);

  // Feed one raw sample (baseline already subtracted). Returns true when a
  // trigger fires on this sample.
  bool Eval(double filterInput);

  double LastRV()     const { return fLastRV; }
  double LastARV()    const { return fLastARV; }
  double RVPeak()     const { return fRVPeak; }
  double ARVPeak()    const { return fARVPeak; }
  double CurrentRV()  const { return fRVPrev; }
  double CurrentARV() const { return fARVPrev; }

private:
  RVParams fPar{};

  ButterworthFilter fFilter;  // streaming, at decimated rate SR/DS

  // Template deviation stored reversed: fTdev[k] = (hftmp[nwin-1-k] - mean),
  // so the cross-correlation is a forward-sequential dot product.
  std::vector<double> fTdev;
  double fTstd{};

  // Warmup countdown [raw samples]: mirrors "nprocessed >= fDELAY" in read_amod.C.
  int fWarmupLeft{};

  // Circular buffer of filtered samples; fBufHead is the index of the newest one.
  std::vector<double> fFiltBuf;
  int    fBufHead{};
  double fRunSum{};
  double fRunSum2{};
  int    fDSCounter{};

  // RV/ARV history for local-maximum detection (same 2-sample lag as read_amod.C)
  double fRVPrev{};
  double fRVPrevPrev{};
  double fARVPrev{};

  // Peak RV and the ARV at that peak since the last trigger (a_rvh / a_arvh in read_amod.C).
  double fRVPeak{-1.0};
  double fARVPeak{-1.0};

  double fLastRV{};
  double fLastARV{};
};
