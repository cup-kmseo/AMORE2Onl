#include <iostream>

#include "AMOREAlgs/ButterworthTrigger.hh"

ButterworthTrigger::ButterworthTrigger()
  : AbsSWTrigger("ButterworthTrigger")
{
}

ButterworthTrigger::ButterworthTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool ButterworthTrigger::Prepare()
{
  if (!fConfig) {
    std::cerr << "ButterworthTrigger::Prepare - Configuration is not set!" << std::endl;
    return false;
  }

  try {
    InitFIFO();
  }
  catch (const std::exception & e) {
    std::cerr << "ButterworthTrigger::Prepare - FIFO Init failed: " << e.what() << std::endl;
    return false;
  }

  // TODO: Initialize Butterworth filter coefficients here

  return true;
}

int ButterworthTrigger::DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                                  unsigned long * timetag)
{
  if (!fFIFO) return -1;

  unsigned short currentBinADC[16];
  unsigned long currentBinTime = 0;

  while (true) {
    int popStatus = fFIFO->PopCurrent(currentBinADC, currentBinTime);

    if (popStatus == 1) return 0; // Wait for more data
    if (popStatus < 0) return -1; // FIFO Error

    // TODO: Implement Butterworth filter logic and threshold detection
    // For now, it just consumes data and never triggers.
  }

  return 0;
}