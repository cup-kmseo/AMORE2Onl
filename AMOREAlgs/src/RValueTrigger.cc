#include <iostream>

#include "AMOREAlgs/RValueTrigger.hh"

RValueTrigger::RValueTrigger()
  : AbsSWTrigger("RValueTrigger")
{
}

RValueTrigger::RValueTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RValueTrigger::Prepare()
{
  if (!fConfig) {
    std::cerr << "RValueTrigger::Prepare - Configuration is not set!" << std::endl;
    return false;
  }

  try {
    InitFIFO();
  }
  catch (const std::exception & e) {
    std::cerr << "RValueTrigger::Prepare - FIFO Init failed: " << e.what() << std::endl;
    return false;
  }

  // TODO: Initialize R-value parameters here

  return true;
}

int RValueTrigger::DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                             unsigned long * timetag)
{
  if (!fFIFO) return -1;

  unsigned short currentBinADC[16];
  unsigned long currentBinTime = 0;

  while (true) {
    int popStatus = fFIFO->PopCurrent(currentBinADC, currentBinTime);

    if (popStatus == 1) return 0; // Wait for more data
    if (popStatus < 0) return -1; // FIFO Error

    // TODO: Implement R-value calculation and trigger logic
    // For now, it just consumes data and never triggers.
  }

  return 0;
}