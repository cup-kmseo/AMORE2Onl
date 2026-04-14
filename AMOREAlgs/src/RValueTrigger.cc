#include "AMOREAlgs/RValueTrigger.hh"

RValueTrigger::RValueTrigger()
  : AbsSWTrigger("RValueTrigger")
{
}

RValueTrigger::RValueTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RValueTrigger::PrepareAlgo()
{
  // TODO: Initialize R-value parameters from config
  return true;
}

bool RValueTrigger::EvalChannel(int /*ch*/, unsigned short /*adcVal*/)
{
  // TODO: Compute R-value and return true when it exceeds fTHR[ch]
  return false;
}
