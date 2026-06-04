#include <cmath>
#include <iostream>

#include "TFile.h"
#include "TH1D.h"

#include "AMOREAlgs/RValueTrigger.hh"

RValueTrigger::RValueTrigger()
  : AbsSWTrigger("RValueTrigger")
{
}

RValueTrigger::RValueTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RValueTrigger::PrepareAlgo()
{
  fNWin = fConfig->RVNWin();
  fDS   = fConfig->RVDS();

  int    order = fConfig->BWFOrder();
  double lc    = fConfig->BWFLC();
  double uc    = fConfig->BWFUC();
  double sfDS  = static_cast<double>(fConfig->SR()) / fDS;

  if (lc <= 0 || uc <= lc || sfDS <= 0) {
    std::cerr << "[RValueTrigger] Invalid filter params: "
              << "ORDER=" << order << " LC=" << lc << " UC=" << uc
              << " SR/DS=" << sfDS << std::endl;
    return false;
  }

  const std::string & path = fConfig->RVTmpltFile();
  if (path.empty()) {
    std::cerr << "[RValueTrigger] No template file specified (RV_TMPLT)" << std::endl;
    return false;
  }

  TFile * ftmplt = TFile::Open(path.c_str(), "READ");
  if (!ftmplt || ftmplt->IsZombie()) {
    std::cerr << "[RValueTrigger] Cannot open template file: " << path << std::endl;
    return false;
  }

  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) {
    if (!fTrgOn[ch]) continue;

    fFilter[ch].CreateFilter(order, lc, uc, sfDS);

    int xid = fConfig->CID(ch);
    auto * hh = (TH1D *)ftmplt->Get(Form("htmp%02d", xid));
    if (!hh) {
      std::cerr << "[RValueTrigger] ch" << ch
                << ": histogram htmp" << Form("%02d", xid) << " not found" << std::endl;
      ftmplt->Close();
      return false;
    }

    int nbins = hh->GetNbinsX();
    int nds   = nbins / fDS;

    // Downsample template from histogram (0.25 converts 14-bit range)
    std::vector<double> h0ds(nds);
    for (int i = 0; i < nds; ++i)
      h0ds[i] = 0.25 * hh->GetBinContent(i * fDS + 1);

    // Apply batch filter to full downsampled template — used to locate the rise point
    std::vector<double> hfds(nds);
    fFilter[ch].Filter(nds, h0ds.data(), hfds.data());

    // Find peak
    int    peakIdx = 0;
    double peakVal = 0;
    for (int i = 0; i < nds; ++i)
      if (hfds[i] > peakVal) { peakVal = hfds[i]; peakIdx = i; }

    // Find 50% rise (backward from peak)
    int r50 = peakIdx;
    for (int i = peakIdx; i > 0; --i)
      if (hfds[i] < 0.5 * peakVal) { r50 = i; break; }

    // Extract raw window: 60 samples before 50% rise, length fNWin (downsampled)
    int start = r50 - 60;
    if (start < 0) start = 0;
    if (start + fNWin > nds) start = nds - fNWin;

    std::vector<double> htmp(fNWin);
    for (int i = 0; i < fNWin; ++i)
      htmp[i] = h0ds[start + i];

    // Apply batch filter to the extracted window — this is the actual template
    std::vector<double> hftmp(fNWin);
    fFilter[ch].Filter(fNWin, htmp.data(), hftmp.data());

    // Compute deviation from mean and std
    double asum = 0;
    for (int i = 0; i < fNWin; ++i) asum += hftmp[i];
    double mean = asum / fNWin;

    fTdev[ch].resize(fNWin);
    double asum2 = 0;
    for (int i = 0; i < fNWin; ++i) {
      fTdev[ch][i] = hftmp[i] - mean;
      asum2 += fTdev[ch][i] * fTdev[ch][i];
    }
    fTstd[ch] = std::sqrt(asum2 / fNWin);

    if (fTstd[ch] <= 0) {
      std::cerr << "[RValueTrigger] ch" << ch << ": template std is zero" << std::endl;
      ftmplt->Close();
      return false;
    }

    // Initialize streaming state
    fFiltBuf[ch].assign(fNWin, 0.0);
    fRunSum[ch]     = 0.0;
    fRunSum2[ch]    = 0.0;
    fDSCounter[ch]  = 0;
    fWarmupLeft[ch] = fConfig->DLY();
    fRVPrev[ch]     = 0.0;
    fRVPrevPrev[ch] = 0.0;
    fARVPrev[ch]    = 0.0;
    fRVPeak[ch]     = -1.0;
    fARVPeak[ch]    = -1.0;
    fLastRV[ch]     = 0.0;
    fLastARV[ch]    = 0.0;
  }

  ftmplt->Close();
  return true;
}

bool RValueTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  // Warmup: count raw samples, suppress trigger decisions until fDLY elapsed
  // (mirrors "nprocessed >= fDELAY" in read_amod.C).
  const bool in_warmup = (fWarmupLeft[ch] > 0);
  if (in_warmup) --fWarmupLeft[ch];

  // Downsample: only compute R-value every fDS raw samples
  if (++fDSCounter[ch] < fDS) return false;
  fDSCounter[ch] = 0;

  // Streaming Butterworth filter (baseline already removed)
  double filt = fFilter[ch].Filter(static_cast<double>(adcVal) - fBaseline[ch]);

  // Slide window: push new sample, drop oldest
  double dropped = fFiltBuf[ch].back();
  fFiltBuf[ch].pop_back();
  fFiltBuf[ch].push_front(filt);
  fRunSum[ch]  += filt - dropped;
  fRunSum2[ch] += filt * filt - dropped * dropped;

  // Cross-correlation with template deviation
  // fFiltBuf[0]=newest aligns with fTdev[fNWin-1] (template end)
  double cc = 0;
  for (int i = 0; i < fNWin; ++i)
    cc += fFiltBuf[ch][i] * fTdev[ch][fNWin - 1 - i];

  // Current window std
  double mean  = fRunSum[ch] / fNWin;
  double var   = fRunSum2[ch] / fNWin - mean * mean;
  double sigma = (var > 0) ? std::sqrt(var) : 0.0;

  double rv  = (sigma > 0 && fTstd[ch] > 0) ? cc / (sigma * fTstd[ch] * fNWin) : 0.0;
  double arv = (fTstd[ch] > 0) ? sigma / fTstd[ch] : 0.0;

  // Local maximum detection: fRVPrev[ch] was a peak when it is >= both neighbours.
  // This mirrors the dq_rvh[1] check in read_amod.C.
  bool fired = false;
  if (!in_warmup && fRVPrevPrev[ch] <= fRVPrev[ch] && fRVPrev[ch] >= rv) {
    // Update peak RV and the ARV recorded at that peak (a_rvh / a_arvh in read_amod.C).
    if (fRVPrev[ch] > fRVPeak[ch]) {
      fRVPeak[ch]  = fRVPrev[ch];
      fARVPeak[ch] = fARVPrev[ch];
    }

    double rv_thr = static_cast<double>(fTHR[ch]) * 1e-3;
    if (fRVPrev[ch] > rv_thr) {
      // Ellipse cut: RV² / THRY + ARV² / THRX > 1.
      // Disabled (pass-through) when either threshold is zero (not configured).
      double thry = fConfig->RVTHRY(ch);
      double thrx = fConfig->RVTHRX(ch);
      bool ellipse_ok = (thry <= 0 || thrx <= 0) ||
                        (fRVPrev[ch] * fRVPrev[ch] / thry +
                         fARVPeak[ch] * fARVPeak[ch] / thrx > 1.0);
      if (ellipse_ok) {
        fired        = true;
        fLastRV[ch]  = fRVPrev[ch];
        fLastARV[ch] = fARVPeak[ch];
        fRVPeak[ch]  = -1.0;
        fARVPeak[ch] = -1.0;
      }
    }
  }

  fRVPrevPrev[ch] = fRVPrev[ch];
  fRVPrev[ch]     = rv;
  fARVPrev[ch]    = arv;

  return fired;
}
