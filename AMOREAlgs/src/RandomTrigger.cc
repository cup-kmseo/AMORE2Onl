#include <iostream>

#include "AMOREAlgs/RandomTrigger.hh"

RandomTrigger::RandomTrigger()
  : AbsSWTrigger("RandomTrigger")
{
}

RandomTrigger::RandomTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RandomTrigger::PrepareAlgo()
{
  int dsr  = fConfig->SR();
  int rate = fConfig->PTRG();

  fProbPerBin = (rate * static_cast<double>(dsr)) / 1e12;

  std::random_device rd;
  fRNG.seed(rd());
  fDist = std::uniform_real_distribution<double>(0.0, 1.0);

  return true;
}

bool RandomTrigger::EvalChannel(int /*ch*/, unsigned short /*adcVal*/)
{
  return fDist(fRNG) < fProbPerBin;
}
