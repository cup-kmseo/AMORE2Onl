#include <iostream>
#include <stdexcept>

#include "AMOREAlgs/AbsSWTrigger.hh"

AbsSWTrigger::AbsSWTrigger(const char * name) { fName = name; }

void AbsSWTrigger::InitFIFO()
{
  if (!fConfig) {
    throw std::runtime_error("AbsSWTrigger::InitFIFO - fConfig is null. Configuration must be set "
                             "before initializing FIFO.");
  }

  int rl = fConfig->RL();
  int head = fConfig->DLY();
  int tail = rl - head;

  fFIFO = std::make_unique<ChunkDataFIFO>(AMORE::kNCHPERADC, head, tail);
}

void AbsSWTrigger::InitChannels()
{
  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) {
    fTrgOn[ch]           = fConfig->TRGON(ch);
    fTHR[ch]             = fConfig->THR(ch);
    fDeadtime[ch]        = fConfig->DT(ch);
    fDeadtimeCounter[ch] = 0;
    fSlopeLookBack[ch]   = fConfig->SlopeLookBack(ch);
    fSlopeDeadtime[ch]   = fConfig->SlopeDeadtime(ch);
  }
}

bool AbsSWTrigger::Prepare()
{
  if (!fConfig) {
    std::cerr << "[" << fName << "] Prepare - Configuration is not set!" << std::endl;
    return false;
  }

  try {
    InitFIFO();
  }
  catch (const std::exception & e) {
    std::cerr << "[" << fName << "] Prepare - FIFO Init failed: " << e.what() << std::endl;
    return false;
  }

  InitChannels();

  return PrepareAlgo();
}

int AbsSWTrigger::DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                             unsigned long * timetag)
{
  if (!fFIFO) return -1;

  const int nch = AMORE::kNCHPERADC;
  unsigned short binADC[nch];
  unsigned long binTime = 0;

  while (true) {
    int pop = fFIFO->PopCurrent(binADC, binTime);

    if (pop == 1) return 0; // wait for more data
    if (pop < 0) return -1; // FIFO error

    bool fired = false;

    if (trgbit) {
      for (int ch = 0; ch < nch; ++ch) trgbit[ch] = false;
    }

    for (int ch = 0; ch < nch; ++ch) {
      if (!fTrgOn[ch]) continue;

      if (fDeadtimeCounter[ch] > 0) {
        --fDeadtimeCounter[ch];
        continue;
      }

      if (EvalChannel(ch, binADC[ch])) {
        fired = true;
        fDeadtimeCounter[ch] = fDeadtime[ch];
        if (trgbit) trgbit[ch] = true;
      }
    }

    if (fired) {
      trgtime = binTime;
      int copied = fFIFO->DumpCurrent(adcval, timetag);
      if (copied > 0) return 1;
    }
  }

  return 0;
}

void AbsSWTrigger::PushChunkData(unsigned char * data, int ndp)
{
  fFIFO->PushChunk(data, ndp, fConfig);
}

int AbsSWTrigger::CountPulses(int ch, const std::uint16_t * wave, int ndp) const
{
  // Two detection modes:
  //   1. Below threshold  → level crossing (same as HeightTrigger)
  //   2. Above threshold  → slope detection: a new pulse riding on the tail
  //                         of the previous one is identified by a sudden rise
  //                         of > THR counts over lookBack samples.
  // lookBack / deadtime are per-channel (configured via SLOPE_LB / SLOPE_DT in yml;
  // defaults: 200 / 300 samples ≈ 2 ms / 3 ms at SR=10 µs).
  const int lookBack = fSlopeLookBack[ch];
  const int slopeDT  = fSlopeDeadtime[ch];

  int  count    = 0;
  bool above    = false;
  int  deadtime = 0;

  for (int j = 0; j < ndp; ++j) {
    if (deadtime > 0) { --deadtime; continue; }

    int val = static_cast<int>(wave[j]) - fBaseline[ch] - fTHR[ch];

    if (!above) {
      // --- level crossing: signal rises from below threshold ---
      if (val > 0) {
        ++count;
        above    = true;
        deadtime = slopeDT;
      }
    }
    else {
      // --- slope detection: look for a new pulse on top of the tail ---
      if (j >= lookBack) {
        int slope = static_cast<int>(wave[j]) - static_cast<int>(wave[j - lookBack]);
        if (slope > fTHR[ch]) {
          ++count;
          deadtime = slopeDT;
        }
      }
      if (val <= 0) above = false;
    }
  }

  return (count > 0) ? count : 1; // at least 1 (the triggering pulse itself)
}
