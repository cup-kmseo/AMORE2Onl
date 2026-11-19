#include <cmath>
#include <sstream>

#include "AMOREAlgs/RVCore.hh"

bool RVCore::Prepare(const RVParams & par, const std::vector<double> & tmpl,
                     const std::vector<double> * fr50, std::string & err)
{
  fPar = par;
  const int nwin = fPar.nwin;
  const int ds   = fPar.ds;
  const double sfDS = fPar.sr / ds;

  if (nwin <= 0 || ds <= 0 || fPar.bwfLC <= 0 || fPar.bwfUC <= fPar.bwfLC || sfDS <= 0) {
    std::ostringstream os;
    os << "invalid params: ORDER=" << fPar.bwfOrder << " LC=" << fPar.bwfLC
       << " UC=" << fPar.bwfUC << " NWIN=" << nwin << " DS=" << ds << " SR/DS=" << sfDS;
    err = os.str();
    return false;
  }

  fFilter.CreateFilter(fPar.bwfOrder, fPar.bwfLC, fPar.bwfUC, sfDS);

  const int nbins = static_cast<int>(tmpl.size());
  if (nbins <= (nwin - 1) * ds) {
    err = "template histogram shorter than (NWIN-1)*DS";
    return false;
  }

  // Locate the 50% rise on the full-rate BW(2, 1000-3000 Hz) filtered histogram
  // (fr50 source may differ from the template, e.g. light uses the heat histogram).
  const std::vector<double> & src = fr50 ? *fr50 : tmpl;
  const int nfr50 = static_cast<int>(src.size());

  ButterworthFilter peakFinder;
  peakFinder.CreateFilter(2, 1000.0, 3000.0, fPar.sr);
  std::vector<double> hffull(nfr50);
  peakFinder.Filter(nfr50, src.data(), hffull.data());

  int    maxx  = 0;
  double fmaxv = 0;
  for (int i = 0; i < nfr50; ++i)
    if (hffull[i] > fmaxv) { fmaxv = hffull[i]; maxx = i; }

  int r50 = maxx;
  for (int i = maxx; i > 0; --i)
    if (hffull[i] > 0.5 * fmaxv && hffull[i - 1] <= 0.5 * fmaxv) { r50 = i; break; }

  // Extract raw window: start 60 full-rate samples before the 50% rise,
  // sample every DS — exactly replicates read_amod.C's htmp0 extraction.
  int start_full = r50 - 60;
  if (start_full < 0) start_full = 0;
  if (start_full + (nwin - 1) * ds >= nbins)
    start_full = nbins - (nwin - 1) * ds - 1;

  std::vector<double> htmp(nwin);
  for (int i = 0; i < nwin; ++i)
    htmp[i] = tmpl[start_full + i * ds];

  // Batch-filter the extracted window — this is the actual template
  std::vector<double> hftmp(nwin);
  fFilter.Filter(nwin, htmp.data(), hftmp.data());

  double asum = 0;
  for (int i = 0; i < nwin; ++i) asum += hftmp[i];
  const double mean = asum / nwin;

  fTdev.resize(nwin);
  double asum2 = 0;
  for (int i = 0; i < nwin; ++i) {
    double d = hftmp[i] - mean;
    fTdev[nwin - 1 - i] = d;  // reversed: aligns with circular-buffer traversal order
    asum2 += d * d;
  }
  fTstd = std::sqrt(asum2 / nwin);

  if (fTstd <= 0) {
    err = "template std is zero";
    return false;
  }

  // Initialize streaming state
  fFiltBuf.assign(nwin, 0.0);
  fBufHead     = 0;
  fRunSum      = 0.0;
  fRunSum2     = 0.0;
  fDSCounter   = ds - 1;  // sampling phase identical to read_amod.C
  fWarmupLeft  = fPar.warmup;
  fRVPrev      = 0.0;
  fRVPrevPrev  = 0.0;
  fARVPrev     = 0.0;
  fRVPeak      = -1.0;
  fARVPeak     = -1.0;
  fLastRV      = 0.0;
  fLastARV     = 0.0;
  return true;
}

bool RVCore::Eval(double filterInput)
{
  const int nwin = fPar.nwin;

  // Warmup: count raw samples, suppress trigger decisions until it elapses.
  const bool in_warmup = (fWarmupLeft > 0);
  if (in_warmup) --fWarmupLeft;

  // Downsample: only compute R-value every DS raw samples
  if (++fDSCounter < fPar.ds) return false;
  fDSCounter = 0;

  const double filt = fFilter.Filter(filterInput);

  // Slide window: decrement head (wrapping), overwrite oldest slot with newest sample.
  int head = fBufHead;
  if (--head < 0) head = nwin - 1;
  const double dropped = fFiltBuf[head];
  fFiltBuf[head] = filt;
  fBufHead = head;
  fRunSum  += filt - dropped;
  fRunSum2 += filt * filt - dropped * dropped;

  // Cross-correlation with reversed template: cc = sum_k buf[(head+k)%N] * fTdev[k].
  // Split into two forward-sequential (cache-friendly) loops to avoid per-iteration modulo.
  const double * buf  = fFiltBuf.data();
  const double * tdev = fTdev.data();
  double cc   = 0;
  const int cnt1 = nwin - head;
  for (int k = 0; k < cnt1; ++k) cc += buf[head + k] * tdev[k];
  for (int k = 0; k < head;  ++k) cc += buf[k]        * tdev[cnt1 + k];

  // Current window std
  const double mean  = fRunSum / nwin;
  const double var   = fRunSum2 / nwin - mean * mean;
  const double sigma = (var > 0) ? std::sqrt(var) : 0.0;

  const double rv  = (sigma > 0 && fTstd > 0) ? cc / (sigma * fTstd * nwin) : 0.0;
  const double arv = (fTstd > 0) ? sigma / fTstd : 0.0;

  // Local maximum detection: fRVPrev was a peak when it is >= both neighbours
  // (dq_rvh[1] check in read_amod.C).
  bool fired = false;
  if (!in_warmup && fRVPrevPrev <= fRVPrev && fRVPrev >= rv) {
    if (fRVPrev > fRVPeak) {
      fRVPeak  = fRVPrev;
      fARVPeak = fARVPrev;
    }

    if (fRVPrev > fPar.thr) {
      // Ellipse cut: RV² / THRY + ARV² / THRX > 1 (pass-through when not configured).
      const bool ellipse_ok = (fPar.thry <= 0 || fPar.thrx <= 0) ||
                              (fRVPrev * fRVPrev / fPar.thry +
                               fARVPeak * fARVPeak / fPar.thrx > 1.0);
      if (ellipse_ok) {
        fired    = true;
        fLastRV  = fRVPrev;
        fLastARV = fARVPeak;
        fRVPeak  = -1.0;
        fARVPeak = -1.0;
      }
    }
  }

  fRVPrevPrev = fRVPrev;
  fRVPrev     = rv;
  fARVPrev    = arv;

  return fired;
}
