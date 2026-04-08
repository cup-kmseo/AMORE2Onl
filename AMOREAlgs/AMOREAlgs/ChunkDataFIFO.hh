#pragma once

#include <cstring>
#include <memory>
#include <vector>

#include "AMORESystem/AMOREADCConf.hh"
#include "DAQUtils/ConcurrentDeque.hh"

struct Chunk {
  std::vector<unsigned long> fTime;
  std::vector<std::vector<unsigned short>> fADC;

  Chunk(int nch, int ndp)
  {
    fTime.resize(ndp);
    fADC.assign(nch, std::vector<unsigned short>(ndp));
  }
};

class ChunkDataFIFO {
public:
  ChunkDataFIFO();
  ChunkDataFIFO(int nch, int head, int tail);
  virtual ~ChunkDataFIFO() = default;

  void BookFIFO(int nch, int head, int tail);

  // Producer methods
  int PushChunk(unsigned short ** adc, unsigned long * time, int ndp);
  int PushChunk(unsigned char * data, int ndp, AMOREADCConf * conf);

  // Consumer methods
  int PopCurrent(unsigned short * adc, unsigned long & time);
  int DumpCurrent(unsigned short ** outADC, unsigned long * outTime);

  // Controls and status
  void Stop();
  void Restart();
  bool IsStopped() const;
  bool Empty() const;
  std::size_t GetQueueSize() const;

  // Statistics
  void DumpStat();

private:
  int fNChannel;
  int fHead; // Head window size
  int fTail; // Tail window size

  ConcurrentDeque<std::unique_ptr<Chunk>> fQueue;

  std::unique_ptr<Chunk> fLastChunk;
  std::unique_ptr<Chunk> fCurrentChunk;
  std::unique_ptr<Chunk> fNextChunk;
  size_t fCurrentSampleIndex;

  // for dump statistics
  unsigned long fTotalChunks;
  unsigned long fTotalSamples;
  unsigned long fFirstTime;
  unsigned long fLastTime;
};

inline void ChunkDataFIFO::Stop() { fQueue.stop(); }
inline void ChunkDataFIFO::Restart() { fQueue.restart(); }
inline bool ChunkDataFIFO::IsStopped() const { return fQueue.is_stopped(); }
inline bool ChunkDataFIFO::Empty() const
{
  const bool current_empty =
      (!fCurrentChunk) ||
      (fCurrentSampleIndex >= fCurrentChunk->fTime.size());

  return fQueue.empty() && !fNextChunk && current_empty;
}
inline std::size_t ChunkDataFIFO::GetQueueSize() const { return fQueue.size(); }