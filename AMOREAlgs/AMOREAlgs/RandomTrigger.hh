#pragma once

#include <random>

#include "AMOREAlgs/AbsSWTrigger.hh"

class RandomTrigger : public AbsSWTrigger {
public:
  RandomTrigger();
  RandomTrigger(const char * name);
  virtual ~RandomTrigger() = default;

  bool Prepare() override;
  int DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                unsigned long * timetag = nullptr) override;

private:
  int fDSR{10}; // Down sampling rate (Base: 1 MHz)
  int fRate{1}; // Trigger rate per second (Hz)

  // Channel-specific settings
  int fDeadtime[AMORE::kNCHPERADC];    // Deadtime in bins per channel
  int fTrgOn[AMORE::kNCHPERADC]; // Trigger Enable flag per channel

  double fProbPerBin; // Calculated probability per single bin

  // Track deadtime for each of the AMORE::kNCHPERADC channels
  int fDeadtimeCounters[AMORE::kNCHPERADC];

  std::mt19937 fRNG;
  std::uniform_real_distribution<double> fDist;
};