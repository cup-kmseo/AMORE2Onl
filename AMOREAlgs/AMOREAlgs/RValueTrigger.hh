#pragma once

#include "AMOREAlgs/AbsSWTrigger.hh"

// Per-channel R-value based trigger.
// TODO: Implement R-value calculation per channel.
class RValueTrigger : public AbsSWTrigger {
public:
  RValueTrigger();
  RValueTrigger(const char * name);
  virtual ~RValueTrigger() = default;

protected:
  bool PrepareAlgo() override;
  bool EvalChannel(int ch, unsigned short adcVal) override;

private:
  // TODO: Add R-value calculation state per channel
};
