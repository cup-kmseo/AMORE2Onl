#pragma once

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
};

inline void AbsSWTrigger::SetName(const char * name) { fName = name; }

inline void AbsSWTrigger::SetDebug(int level) { fDebug = level; }

inline void AbsSWTrigger::SetConfig(AMOREADCConf * config) { fConfig = config; }

inline const char * AbsSWTrigger::GetName() const { return fName.c_str(); }

inline void AbsSWTrigger::Stop() { if (fFIFO) fFIFO->Stop(); }

inline bool AbsSWTrigger::IsFIFOEmpty() const { return !fFIFO || fFIFO->Empty(); }