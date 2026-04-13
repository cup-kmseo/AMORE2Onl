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

bool RandomTrigger::Prepare()
{
  if (!fConfig) {
    std::cerr << "RandomTrigger::Prepare - Configuration is not set!" << std::endl;
    return false;
  }

  try {
    InitFIFO();
  }
  catch (const std::exception & e) {
    std::cerr << "RandomTrigger::Prepare - FIFO Init failed: " << e.what() << std::endl;
    return false;
  }

  fDSR = fConfig->SR();
  fRate = fConfig->PTRG();

  // Calculate the probability for a single bin to fire.
  fProbPerBin = (fRate * static_cast<double>(fDSR)) / 1000000.0;

  fDeadtime = 0;
  for (int i = 0; i < AMORE::kNCHPERADC; ++i) {
    fTrgOn[i] = fConfig->TRGON(i);
    if (fTrgOn[i] && fDeadtime == 0) fDeadtime = fConfig->DT(i);
  }
  fDeadtimeCounter = 0;

  std::random_device rd;
  fRNG.seed(rd());
  fDist = std::uniform_real_distribution<double>(0.0, 1.0);

  return true;
}

int RandomTrigger::DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                             unsigned long * timetag)
{
  if (!fFIFO) return -1;

  const int nChannels = AMORE::kNCHPERADC;
  unsigned short currentBinADC[AMORE::kNCHPERADC];
  unsigned long currentBinTime = 0;

  while (true) {
    int popStatus = fFIFO->PopCurrent(currentBinADC, currentBinTime);

    if (popStatus == 1) return 0; // Wait for more data
    if (popStatus < 0) return -1; // FIFO Error

    bool fired = false;

    if (trgbit) {
      for (int ch = 0; ch < nChannels; ++ch) trgbit[ch] = false;
    }

    if (fDeadtimeCounter > 0) {
      --fDeadtimeCounter;
    }
    else if (fDist(fRNG) < fProbPerBin) {
      // Single trigger decision per time bin
      fired = true;
      fDeadtimeCounter = fDeadtime;
      for (int ch = 0; ch < nChannels; ++ch) {
        if (fTrgOn[ch] && trgbit) trgbit[ch] = true;
      }
    }

    if (fired) {
      trgtime = currentBinTime;

      // Extract waveform window
      int copied = fFIFO->DumpCurrent(adcval, timetag);

      if (copied > 0) {
        return 1; // Trigger generated successfully
      }
    }
  }

  return 0;
}