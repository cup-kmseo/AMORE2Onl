#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel rising-edge amplitude (height) trigger.
//
// Fires on the first sample where ADC[ch] > THR[ch].
// The per-channel deadtime (DT) configured in the base class prevents
// re-triggering within the same pulse.
class HeightTrigger : public AbsSWTrigger {
public:
  HeightTrigger();
  HeightTrigger(const char * name);
  virtual ~HeightTrigger() = default;

protected:
  bool EvalChannel(int ch, unsigned short adcVal) override;
};
