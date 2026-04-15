#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel falling-edge trigger.
//
// Fires when the threshold-subtracted signal crosses zero from above:
//   prev = adcVal[n-1] - baseline - THR >= 0
//   curr = adcVal[n]   - baseline - THR  < 0
//
// Fires exactly once per falling edge regardless of pulse shape.
class FallingEdgeTrigger : public AbsSWTrigger {
public:
  FallingEdgeTrigger();
  FallingEdgeTrigger(const char * name);
  virtual ~FallingEdgeTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  int fPrevVal[AMORE::kNCHPERADC]{};
};
