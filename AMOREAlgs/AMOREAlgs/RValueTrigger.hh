#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

class RValueTrigger : public AbsSWTrigger {
public:
  RValueTrigger();
  RValueTrigger(const char * name);
  virtual ~RValueTrigger() = default;

  bool Prepare() override;
  int DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                unsigned long * timetag = nullptr) override;

private:
  // TODO: Add specific variables for R-value calculation
  // double fThreshold;
};