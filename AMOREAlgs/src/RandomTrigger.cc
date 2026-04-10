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

  // Reset deadtime counters
  for (int i = 0; i < AMORE::kNCHPERADC; ++i) {
    fDeadtime[i] = fConfig->DT(i);
    fTrgOn[i] = fConfig->TRGON(i);
    fDeadtimeCounters[i] = 0;
  }

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

    // Evaluate each channel
    for (int ch = 0; ch < nChannels; ++ch) {
      if (trgbit) trgbit[ch] = false;

      // 1. Process Deadtime counter (always decrement if time passes)
      if (fDeadtimeCounters[ch] > 0) { fDeadtimeCounters[ch]--; }

      // 2. Skip trigger evaluation if TRGON is false OR still in Deadtime
      if (!fTrgOn[ch] || fDeadtimeCounters[ch] > 0) { continue; }

      // 3. Evaluate random trigger condition
      if (fDist(fRNG) < fProbPerBin) {
        fired = true;
        if (trgbit) trgbit[ch] = true;

        // 4. Set Channel-specific Deadtime counter
        fDeadtimeCounters[ch] = fDeadtime[ch];
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