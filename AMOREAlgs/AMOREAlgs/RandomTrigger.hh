#pragma once

#include <random>

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel random trigger for testing.
// Each enabled channel fires independently with probability proportional to PTRG (Hz).
class RandomTrigger : public AbsSWTrigger {
public:
  RandomTrigger();
  RandomTrigger(const char * name);
  virtual ~RandomTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  double fProbPerBin{};

  std::mt19937 fRNG;
  std::uniform_real_distribution<double> fDist;
};
