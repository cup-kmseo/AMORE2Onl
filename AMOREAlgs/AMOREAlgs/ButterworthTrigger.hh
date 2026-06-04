#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/ButterworthFilter.hh"

#include "AMORE/amoreconsts.hh"

// Per-channel Butterworth bandpass filtered amplitude trigger.
// Applies an IIR bandpass filter (streaming) to each channel and fires
// when the filtered amplitude exceeds fTHR[ch].
// Filter parameters (order, LC, UC) come from AMOREADCConf.
class ButterworthTrigger : public AbsSWTrigger {
public:
  ButterworthTrigger();
  ButterworthTrigger(const char * name);
  virtual ~ButterworthTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  ButterworthFilter fFilter[AMORE::kNCHPERADC];
};
