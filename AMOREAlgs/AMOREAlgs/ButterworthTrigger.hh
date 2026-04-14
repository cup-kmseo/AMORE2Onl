#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel Butterworth low-pass filtered amplitude trigger.
// TODO: Implement filter coefficients and per-channel filter state.
class ButterworthTrigger : public AbsSWTrigger {
public:
  ButterworthTrigger();
  ButterworthTrigger(const char * name);
  virtual ~ButterworthTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  // TODO: Add Butterworth filter coefficients and per-channel state
  // double fCoeffB[], fCoeffA[];
  // double fState[kNCHPERADC][kFilterOrder]{};
};
