#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

class ButterworthTrigger : public AbsSWTrigger {
public:
  ButterworthTrigger();
  ButterworthTrigger(const char * name);
  virtual ~ButterworthTrigger() = default;

  bool Prepare() override;
  int DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                unsigned long * timetag = nullptr) override;

private:
  // TODO: Add specific configuration variables for Butterworth filter
  // double fCutoffFreq;
  // int fOrder;
};