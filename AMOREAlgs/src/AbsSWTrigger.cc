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
    fTHR[ch]             = fConfig->THR(ch);
    fDeadtime[ch]        = fConfig->DT(ch);
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

  const int rl = fConfig->RL();
  fDumpStore.assign(AMORE::kNCHPERADC, std::vector<unsigned short>(rl));
  fDumpADC.resize(AMORE::kNCHPERADC);
  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) fDumpADC[ch] = fDumpStore[ch].data();
  fDumpTime.assign(rl, 0);

  return PrepareAlgo();
}

void AbsSWTrigger::PushChunkData(unsigned char * data, int ndp)
{
  fFIFO->PushChunk(data, ndp, fConfig);
}

void AbsSWTrigger::PushChunkData(unsigned short ** adc, unsigned long * time, int ndp)
{
  fFIFO->PushChunk(adc, time, ndp);
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
