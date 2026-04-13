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

  int fTrgOn[AMORE::kNCHPERADC]; // Trigger Enable flag per channel
  int fDeadtime;                  // Deadtime in bins (global, from config DT of first TRGON ch)
  int fDeadtimeCounter;           // Remaining bins in deadtime

  double fProbPerBin; // Calculated probability per single bin

  std::mt19937 fRNG;
  std::uniform_real_distribution<double> fDist;
};