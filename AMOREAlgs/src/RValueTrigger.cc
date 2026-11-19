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
    const std::string & pfx    = fConfig->RVTmpltHistPfx();
    const std::string & pfxFR50 = fConfig->RVTmpltHistPfxFR50();
    auto * hh = (TH1D *)ftmplt->Get(Form("%s%02d", pfx.c_str(), xid));
    if (!hh) {
      std::cerr << "[RValueTrigger] ch" << ch
                << ": histogram " << pfx << Form("%02d", xid) << " not found" << std::endl;
      ftmplt->Close();
      return false;
    }

    int nbins = hh->GetNbinsX();

    // Build full-rate histogram array (0.25 converts 32-bit ADC to 14-bit range)
    std::vector<double> h0full(nbins);
    for (int i = 0; i < nbins; ++i)
      h0full[i] = 0.25 * hh->GetBinContent(i + 1);

    // Locate the 50% rise: use pfxFR50 histogram if different from pfx (e.g. light
    // template uses heat histogram for fr50 to match read_amod.C behaviour).
    ButterworthFilter peakFinder;
    peakFinder.CreateFilter(2, 1000.0, 3000.0, static_cast<double>(fConfig->SR()));

    int nfr50 = nbins;  // size of histogram used for fr50 search
    std::vector<double> hffull(nbins);
    if (pfxFR50 != pfx) {
      auto * hhfr50 = (TH1D *)ftmplt->Get(Form("%s%02d", pfxFR50.c_str(), xid));
      if (!hhfr50) {
        std::cerr << "[RValueTrigger] ch" << ch
                  << ": fr50 histogram " << pfxFR50 << Form("%02d", xid) << " not found" << std::endl;
        ftmplt->Close();
        return false;
      }
      nfr50 = hhfr50->GetNbinsX();
      std::vector<double> h0fr50(nfr50), hffr50(nfr50);
      for (int i = 0; i < nfr50; ++i)
        h0fr50[i] = 0.25 * hhfr50->GetBinContent(i + 1);
      peakFinder.Filter(nfr50, h0fr50.data(), hffr50.data());
      hffull = std::move(hffr50);
    } else {
      peakFinder.Filter(nbins, h0full.data(), hffull.data());
    }

    int    maxx = 0;
    double fmaxv = 0;
    for (int i = 0; i < nfr50; ++i)
      if (hffull[i] > fmaxv) { fmaxv = hffull[i]; maxx = i; }

    int fr50 = maxx;
    for (int i = maxx; i > 0; --i)
      if (hffull[i] > 0.5 * fmaxv && hffull[i - 1] <= 0.5 * fmaxv) { fr50 = i; break; }

    // Extract raw window: start 60 full-rate samples before 50% rise,
    // sample every fDS — exactly replicates read_amod.C's htmp0 extraction.
    int start_full = fr50 - 60;
    if (start_full < 0) start_full = 0;
    if (start_full + (fNWin - 1) * fDS >= nbins)
      start_full = nbins - (fNWin - 1) * fDS - 1;

    std::vector<double> htmp(fNWin);
    for (int i = 0; i < fNWin; ++i)
      htmp[i] = h0full[start_full + i * fDS];

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
      double d = hftmp[i] - mean;
      fTdev[ch][fNWin - 1 - i] = d;   // reversed: aligns with circular-buffer traversal order
      asum2 += d * d;
    }
    fTstd[ch] = std::sqrt(asum2 / fNWin);

    if (fTstd[ch] <= 0) {
      std::cerr << "[RValueTrigger] ch" << ch << ": template std is zero" << std::endl;
      ftmplt->Close();
      return false;
    }

    // Initialize streaming state
    fFiltBuf[ch].assign(fNWin, 0.0);
    fBufHead[ch]    = 0;
    fRunSum[ch]     = 0.0;
    fRunSum2[ch]    = 0.0;
    fDSCounter[ch]  = fDS - 1;
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

bool RValueTrigger::EvalImpl(int ch, double filterInput)
{
  // Warmup: count raw samples, suppress trigger decisions until fDLY elapsed
  // (mirrors "nprocessed >= fDELAY" in read_amod.C).
  const bool in_warmup = (fWarmupLeft[ch] > 0);
  if (in_warmup) --fWarmupLeft[ch];

  // Downsample: only compute R-value every fDS raw samples
  if (++fDSCounter[ch] < fDS) return false;
  fDSCounter[ch] = 0;

  double filt = fFilter[ch].Filter(filterInput);

  // Slide window: decrement head (wrapping), overwrite oldest slot with newest sample.
  int head = fBufHead[ch];
  if (--head < 0) head = fNWin - 1;
  double dropped = fFiltBuf[ch][head];
  fFiltBuf[ch][head] = filt;
  fBufHead[ch] = head;
  fRunSum[ch]  += filt - dropped;
  fRunSum2[ch] += filt * filt - dropped * dropped;

  // Cross-correlation with reversed template: cc = sum_k buf[(head+k)%N] * fTdev[k].
  // Split into two forward-sequential (cache-friendly) loops to avoid per-iteration modulo.
  const double * buf  = fFiltBuf[ch].data();
  const double * tdev = fTdev[ch].data();
  double cc   = 0;
  int    cnt1 = fNWin - head;
  for (int k = 0; k < cnt1; ++k) cc += buf[head + k] * tdev[k];
  for (int k = 0; k < head;  ++k) cc += buf[k]        * tdev[cnt1 + k];

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

bool RValueTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  return EvalImpl(ch, static_cast<double>(adcVal) - fBaseline[ch]);
}

bool RValueTrigger::EvalChannelD(int ch, double adcVal)
{
  return EvalImpl(ch, adcVal - fBaselineD[ch]);
}
