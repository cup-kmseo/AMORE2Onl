#pragma once

#include <vector>

// Bandpass Butterworth IIR filter — ROOT-free.
// Supports both batch processing (for template preparation) and
// sample-by-sample streaming (for online trigger in EvalChannel).
class ButterworthFilter {
public:
  ButterworthFilter() = default;

  void CreateFilter(int order, double lc, double uc, double sf = 1e5);

  // Batch: process full array at once (for template preparation)
  void Filter(int np, const double * x, double * y) const;

  // Streaming: process one sample, maintaining internal state across calls
  double Filter(double x);
  void   ResetState();

  int     Order()   const { return fOrder; }
  int     NCoeffs() const { return fNCoeffs; }
  const double * NumC() const { return fNumC.data(); }
  const double * DenC() const { return fDenC.data(); }

private:
  std::vector<double> ComputeLP() const;
  std::vector<double> ComputeHP() const;
  std::vector<double> TrinomialMultiply(const std::vector<double> & b,
                                        const std::vector<double> & c) const;
  void ComputeNumCoeffs();
  void ComputeDenomCoeffs();

  int    fOrder{};
  int    fNCoeffs{};
  double fLC{};
  double fUC{};

  std::vector<double> fNumC{};
  std::vector<double> fDenC{};

  // Streaming state: past inputs and outputs (circular, length = NCoeffs)
  std::vector<double> fStateX{};
  std::vector<double> fStateY{};
  int fStateIdx{0};
};
