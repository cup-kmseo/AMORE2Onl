#include "AMOREAlgs/HeightTrigger.hh"

HeightTrigger::HeightTrigger()
  : AbsSWTrigger("HeightTrigger")
{
}

HeightTrigger::HeightTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool HeightTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  return adcVal > static_cast<unsigned short>(fTHR[ch]);
}
