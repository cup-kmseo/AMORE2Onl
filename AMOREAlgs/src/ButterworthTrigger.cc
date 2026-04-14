#include "AMOREAlgs/ButterworthTrigger.hh"

ButterworthTrigger::ButterworthTrigger()
  : AbsSWTrigger("ButterworthTrigger")
{
}

ButterworthTrigger::ButterworthTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool ButterworthTrigger::PrepareAlgo()
{
  // TODO: Initialize Butterworth filter coefficients from config
  return true;
}

bool ButterworthTrigger::EvalChannel(int /*ch*/, unsigned short /*adcVal*/)
{
  // TODO: Apply Butterworth filter and compare filtered amplitude to fTHR[ch]
  return false;
}
