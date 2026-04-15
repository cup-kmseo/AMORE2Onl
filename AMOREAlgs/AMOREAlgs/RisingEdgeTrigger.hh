#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel rising-edge trigger.
//
// Fires when the threshold-subtracted signal crosses zero from below:
//   prev = adcVal[n-1] - baseline - THR <= 0
//   curr = adcVal[n]   - baseline - THR  > 0
//
// Fires exactly once per rising edge regardless of how long the signal
// stays above threshold — no deadtime tuning required.
class RisingEdgeTrigger : public AbsSWTrigger {
public:
  RisingEdgeTrigger();
  RisingEdgeTrigger(const char * name);
  virtual ~RisingEdgeTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  int fPrevVal[AMORE::kNCHPERADC]{};
};
