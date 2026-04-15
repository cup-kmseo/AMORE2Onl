#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "AMORE/amoreconsts.hh"
#include "AMOREAlgs/ChunkDataFIFO.hh"
#include "AMORESystem/AMOREADCConf.hh"

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

  void Stop();
  bool IsFIFOEmpty() const;
  void SetBaselines(const int * vals, int nch);

  // Count distinct pulses in a single-channel waveform using the same
  // baseline and threshold already configured for this trigger.
  // Returns >= 1 (the triggering pulse itself); >= 2 means pile-up.
  int CountPulses(int ch, const std::uint16_t * wave, int ndp) const;

  // Common per-channel self-trigger framework.
  // Subclasses implement PrepareAlgo() and EvalChannel() only.
  bool Prepare();
  int DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                unsigned long * timetag = nullptr);

protected:
  // Override in subclasses to perform algorithm-specific initialisation.
  // Called by Prepare() after InitFIFO() and InitChannels().
  virtual bool PrepareAlgo() { return true; }

  // Per-channel trigger decision for a single time-bin sample.
  // Return true to fire a trigger on this channel.
  virtual bool EvalChannel(int ch, unsigned short adcVal) = 0;

  void InitFIFO();
  void InitChannels();

protected:
  std::string fName{};

  AMOREADCConf * fConfig{nullptr};

  // variables for chunk data process
  int fNHeadBin{};
  int fNTailBin{};
  std::unique_ptr<ChunkDataFIFO> fFIFO;

  int fDebug{};

  std::mutex fMutex;

  // Per-channel self-trigger state (populated by InitChannels)
  int fTrgOn[AMORE::kNCHPERADC]{};
  int fTHR[AMORE::kNCHPERADC]{};
  int fDeadtime[AMORE::kNCHPERADC]{};
  int fDeadtimeCounter[AMORE::kNCHPERADC]{};
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