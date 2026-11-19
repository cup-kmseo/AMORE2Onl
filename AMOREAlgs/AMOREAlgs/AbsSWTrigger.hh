#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AMORE/amoreconsts.hh"
#include "AMOREAlgs/ChunkDataFIFO.hh"
#include "AMORESystem/AMOREADCConf.hh"

// One finalised crystal trigger (channels 2*xtal = phonon, 2*xtal+1 = photon).
struct XtalTrigger {
  int           xtal{-1};    // crystal slot on this ADC
  unsigned long ttime{0};    // time tag of the sample that opened the trigger
  std::uint32_t trgbit{0};   // signal: fired conditions (bits 0-7) | opening condition(s) << 8
  std::vector<std::uint16_t> phonon;
  std::vector<std::uint16_t> photon;
};

class AbsSWTrigger {
public:
  AbsSWTrigger() = default;
  AbsSWTrigger(const char * name);
  virtual ~AbsSWTrigger() = default;

  virtual void SetName(const char * name);
  virtual void SetConfig(AMOREADCConf * config);
  virtual void SetDebug(int level);

  const char * GetName() const;

  virtual void PushChunkData(unsigned char * data, int ndp);
  // Push an already-decoded chunk (adc[ch][i], time tags in ns); used for replay/tests.
  void PushChunkData(unsigned short ** adc, unsigned long * time, int ndp);
  std::size_t GetFIFOQueueSize() const { return fFIFO ? fFIFO->GetQueueSize() : 0; }

  void Stop();
  bool IsFIFOEmpty() const;
  void SetBaselines(const int * vals, int nch);

  // Count distinct pulses in a single-channel waveform using the channel's
  // THR / SLOPE_LB / SLOPE_DT settings and baseline.
  // Returns >= 1 (the triggering pulse itself); >= 2 means pile-up.
  int CountPulses(int ch, const std::uint16_t * wave, int ndp) const;

  // Sets up the FIFO, per-channel settings and snapshot buffers, then PrepareAlgo().
  bool Prepare();

  // Append finalised crystal triggers. Returns the number appended (> 0),
  // 0 when more data is needed, -1 on FIFO error / stop.
  virtual int DoTriggerXtal(std::vector<XtalTrigger> & out) = 0;

  // End of run: emit crystals still held back (e.g. inside their dead time).
  virtual void FlushPending(std::vector<XtalTrigger> & /*out*/) {}

  // True for the pedestal (random) path: its records go to the pedestal group.
  virtual bool IsPedestal() const { return false; }

protected:
  // Override in subclasses to perform algorithm-specific initialisation.
  // Called by Prepare() after InitFIFO() and InitChannels().
  virtual bool PrepareAlgo() { return true; }

  void InitFIFO();
  void InitChannels();

  // Full-ADC waveform snapshot buffers (one row per channel, RL samples),
  // allocated in Prepare().
  std::vector<std::vector<unsigned short>> fDumpStore;
  std::vector<unsigned short *>            fDumpADC;
  std::vector<unsigned long>               fDumpTime;

protected:
  std::string fName{};

  AMOREADCConf * fConfig{nullptr};

  // variables for chunk data process
  int fNHeadBin{};
  int fNTailBin{};
  std::unique_ptr<ChunkDataFIFO> fFIFO;

  int fDebug{};

  std::mutex fMutex;

  // Per-channel settings (populated by InitChannels)
  int fTHR[AMORE::kNCHPERADC]{};       // pile-up counting threshold (CountPulses)
  int fDeadtime[AMORE::kNCHPERADC]{};  // DT; SignalTrigger uses the phonon channel's as crystal DT
  int fBaseline[AMORE::kNCHPERADC]{};
  int fSlopeLookBack[AMORE::kNCHPERADC]{};  // pile-up slope look-back window per channel
  int fSlopeDeadtime[AMORE::kNCHPERADC]{};  // pile-up slope deadtime per channel
};

inline void AbsSWTrigger::SetName(const char * name) { fName = name; }

inline void AbsSWTrigger::SetDebug(int level) { fDebug = level; }

inline void AbsSWTrigger::SetConfig(AMOREADCConf * config) { fConfig = config; }

inline const char * AbsSWTrigger::GetName() const { return fName.c_str(); }

inline void AbsSWTrigger::Stop() { if (fFIFO) fFIFO->Stop(); }

inline bool AbsSWTrigger::IsFIFOEmpty() const { return !fFIFO || fFIFO->Empty(); }

inline void AbsSWTrigger::SetBaselines(const int * vals, int nch)
{
  for (int ch = 0; ch < nch && ch < AMORE::kNCHPERADC; ++ch)
    fBaseline[ch] = vals[ch];
}