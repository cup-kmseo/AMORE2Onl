#include "AMOREAlgs/RisingEdgeTrigger.hh"

RisingEdgeTrigger::RisingEdgeTrigger()
  : AbsSWTrigger("RisingEdgeTrigger")
{
}

RisingEdgeTrigger::RisingEdgeTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RisingEdgeTrigger::PrepareAlgo()
{
  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch)
    fPrevVal[ch] = -fTHR[ch] - 1; // initialise well below threshold
  return true;
}

bool RisingEdgeTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  int curr      = static_cast<int>(adcVal) - fBaseline[ch] - fTHR[ch];
  bool fired    = (fPrevVal[ch] <= 0 && curr > 0);
  fPrevVal[ch]  = curr;
  return fired;
}
