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

void AbsSWTrigger::PushChunkData(unsigned char * data, int ndp)
{
  fFIFO->PushChunk(data, ndp, fConfig);
}
