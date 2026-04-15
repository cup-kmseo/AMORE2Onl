#include "AMOREAlgs/FallingEdgeTrigger.hh"

FallingEdgeTrigger::FallingEdgeTrigger()
  : AbsSWTrigger("FallingEdgeTrigger")
{
}

FallingEdgeTrigger::FallingEdgeTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool FallingEdgeTrigger::PrepareAlgo()
{
  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch)
    fPrevVal[ch] = -fTHR[ch] - 1; // initialise below threshold — no false trigger at start
  return true;
}

bool FallingEdgeTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  int curr      = static_cast<int>(adcVal) - fBaseline[ch] - fTHR[ch];
  bool fired    = (fPrevVal[ch] >= 0 && curr < 0);
  fPrevVal[ch]  = curr;
  return fired;
}
