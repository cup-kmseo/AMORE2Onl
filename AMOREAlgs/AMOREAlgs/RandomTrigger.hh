#pragma once

#include <cstdint>

#include "AMOREAlgs/AbsSWTrigger.hh"

// Pedestal (random) trigger.
//
// Fires on every crystal of the ADC at once, at a mean rate PTRG [uHz].
// The decision for a sample is a hash of its time tag (TCB clock, common to all
// ADCs) on an SR-wide time grid, so every ADC — each in its own thread — fires on
// the same time bins without talking to each other, and the pedestal event is
// built across ADCs by the coincidence window. Records go to the pedestal group.
class RandomTrigger : public AbsSWTrigger {
public:
  RandomTrigger();
  RandomTrigger(const char * name);
  virtual ~RandomTrigger() = default;

  int  DoTriggerXtal(std::vector<XtalTrigger> & out) override;
  bool IsPedestal() const override { return true; }

  // Same decision on every ADC for the same time bin.
  bool FiresAt(unsigned long timeNs) const;

protected:
  bool PrepareAlgo() override;

private:
  std::uint64_t fThreshold{0};   // fire when hash(bin) < fThreshold  (= p * 2^64)
  unsigned long fBinNs{1};       // time grid = one sample (SR us)
  unsigned long fLastBin{~0UL};  // avoid firing twice in one bin
  int           fNXtal{0};       // crystal slots read out on this ADC
};
