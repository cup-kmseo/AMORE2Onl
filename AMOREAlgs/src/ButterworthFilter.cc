#include <cmath>
#include <complex>
#include <cstring>

#include "AMOREAlgs/ButterworthFilter.hh"

void ButterworthFilter::CreateFilter(int order, double lc, double uc, double sf)
{
  fOrder   = order;
  fNCoeffs = 2 * order + 1;
  fLC      = 2.0 * lc / sf;
  fUC      = 2.0 * uc / sf;

  ComputeDenomCoeffs();
  ComputeNumCoeffs();

  fStateX.assign(fNCoeffs, 0.0);
  fStateY.assign(fNCoeffs, 0.0);
  fStateIdx = 0;
}

// ── Batch ────────────────────────────────────────────────────────────────────

void ButterworthFilter::Filter(int np, const double * x, double * y) const
{
  for (int i = 0; i < np; ++i) {
    y[i] = 0.0;
    for (int j = 0; j < fNCoeffs; ++j) {
      int k = i - j;
      if (k >= 0) {
        y[i] += fNumC[j] * (x[k] - x[0]);
        if (j > 0) y[i] -= fDenC[j] * y[k];
      }
    }
  }
}

// ── Streaming ────────────────────────────────────────────────────────────────

double ButterworthFilter::Filter(double x)
{
  // Store new input
  fStateX[fStateIdx] = x;

  double y = 0.0;
  for (int j = 0; j < fNCoeffs; ++j) {
    int idx = (fStateIdx - j + fNCoeffs) % fNCoeffs;
    y += fNumC[j] * fStateX[idx];
    if (j > 0) y -= fDenC[j] * fStateY[(fStateIdx - j + fNCoeffs) % fNCoeffs];
  }

  fStateY[fStateIdx] = y;
  fStateIdx          = (fStateIdx + 1) % fNCoeffs;
  return y;
}

void ButterworthFilter::ResetState()
{
  std::fill(fStateX.begin(), fStateX.end(), 0.0);
  std::fill(fStateY.begin(), fStateY.end(), 0.0);
  fStateIdx = 0;
}

// ── Filter design (ported from rv_trig/ButterworthFilter.cc, ROOT-free) ──────

std::vector<double> ButterworthFilter::ComputeLP() const
{
  std::vector<double> c(fOrder + 1, 0.0);
  c[0] = 1.0;
  c[1] = fOrder;
  int m = fOrder / 2;
  for (int i = 2; i <= m; ++i) {
    c[i]             = static_cast<double>(fOrder - i + 1) * c[i - 1] / i;
    c[fOrder - i]    = c[i];
  }
  c[fOrder - 1] = fOrder;
  c[fOrder]     = 1.0;
  return c;
}

std::vector<double> ButterworthFilter::ComputeHP() const
{
  auto c = ComputeLP();
  for (int i = 0; i <= fOrder; ++i)
    if (i % 2) c[i] = -c[i];
  return c;
}

std::vector<double> ButterworthFilter::TrinomialMultiply(const std::vector<double> & b,
                                                          const std::vector<double> & c) const
{
  std::vector<double> r(4 * fOrder, 0.0);
  r[2] = c[0]; r[3] = c[1];
  r[0] = b[0]; r[1] = b[1];

  for (int i = 1; i < fOrder; ++i) {
    r[2 * (2 * i + 1)]     += c[2 * i] * r[2 * (2 * i - 1)]     - c[2 * i + 1] * r[2 * (2 * i - 1) + 1];
    r[2 * (2 * i + 1) + 1] += c[2 * i] * r[2 * (2 * i - 1) + 1] + c[2 * i + 1] * r[2 * (2 * i - 1)];

    for (int j = 2 * i; j > 1; --j) {
      r[2 * j]     += b[2 * i] * r[2 * (j - 1)]     - b[2 * i + 1] * r[2 * (j - 1) + 1]
                    + c[2 * i] * r[2 * (j - 2)]     - c[2 * i + 1] * r[2 * (j - 2) + 1];
      r[2 * j + 1] += b[2 * i] * r[2 * (j - 1) + 1] + b[2 * i + 1] * r[2 * (j - 1)]
                    + c[2 * i] * r[2 * (j - 2) + 1] + c[2 * i + 1] * r[2 * (j - 2)];
    }

    r[2] += b[2 * i] * r[0] - b[2 * i + 1] * r[1] + c[2 * i];
    r[3] += b[2 * i] * r[1] + b[2 * i + 1] * r[0] + c[2 * i + 1];
    r[0] += b[2 * i];
    r[1] += b[2 * i + 1];
  }
  return r;
}

void ButterworthFilter::ComputeDenomCoeffs()
{
  double cp  = std::cos(M_PI * (fUC + fLC) / 2.0);
  double theta = M_PI * (fUC - fLC) / 2.0;
  double st  = std::sin(theta);
  double ct  = std::cos(theta);
  double s2t = 2.0 * st * ct;
  double c2t = 2.0 * ct * ct - 1.0;

  std::vector<double> RC(2 * fOrder, 0.0);
  std::vector<double> TC(2 * fOrder, 0.0);

  for (int k = 0; k < fOrder; ++k) {
    double pole = M_PI * (2 * k + 1) / (2.0 * fOrder);
    double sp   = std::sin(pole);
    double csp  = std::cos(pole);
    double a    = 1.0 + s2t * sp;
    RC[2 * k]     = c2t / a;
    RC[2 * k + 1] = s2t * csp / a;
    TC[2 * k]     = -2.0 * cp * (ct + st * sp) / a;
    TC[2 * k + 1] = -2.0 * cp * st * csp / a;
  }

  auto d = TrinomialMultiply(TC, RC);

  fDenC.resize(fNCoeffs);
  fDenC[1] = d[0];
  fDenC[0] = 1.0;
  for (int k = 3; k <= 2 * fOrder; ++k)
    fDenC[k] = d[2 * k - 2];
}

void ButterworthFilter::ComputeNumCoeffs()
{
  auto hp = ComputeHP();

  std::vector<std::complex<double>> nc(fNCoeffs);
  for (int i = 0; i < fOrder; ++i) {
    nc[2 * i]     = hp[i];
    nc[2 * i + 1] = 0.0;
  }
  nc[2 * fOrder] = hp[fOrder];

  double Wn = std::sqrt(2.0 * 2.0 * std::tan(M_PI * fLC / 2.0) *
                        2.0 * 2.0 * std::tan(M_PI * fUC / 2.0));
  Wn = 2.0 * std::atan2(Wn, 4.0);

  const std::complex<double> j(0, -1);
  std::vector<std::complex<double>> kernel(fNCoeffs);
  for (int k = 0; k < fNCoeffs; ++k)
    kernel[k] = std::exp(j * Wn * static_cast<double>(k));

  double b = 0.0, den = 0.0;
  for (int d = 0; d < fNCoeffs; ++d) {
    b   += std::real(kernel[d] * nc[d]);
    den += std::real(kernel[d] * std::complex<double>(fDenC[d], 0.0));
  }

  fNumC.resize(fNCoeffs);
  for (int c = 0; c < fNCoeffs; ++c)
    fNumC[c] = std::real(nc[c]) * den / b;
}
