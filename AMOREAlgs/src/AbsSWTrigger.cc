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
