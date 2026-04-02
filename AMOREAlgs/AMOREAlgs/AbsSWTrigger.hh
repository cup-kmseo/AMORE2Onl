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

  virtual bool Prepare() = 0;
  virtual int DoTrigger(unsigned long & trgtime, bool * trgbit, unsigned short ** adcval,
                        unsigned long * timetag = NULL) = 0;

protected:
  void InitFIFO();

protected:
  std::string fName{};

  AMOREADCConf * fConfig{nullptr};

  // variables for chunk data process
  int fNHeadBin{};
  int fNTailBin{};
  std::unique_ptr<ChunkDataFIFO> fFIFO;

  int fDebug{};

  std::mutex fMutex;
};

inline void AbsSWTrigger::SetName(const char * name) { fName = name; }

inline void AbsSWTrigger::SetDebug(int level) { fDebug = level; }

inline void AbsSWTrigger::SetConfig(AMOREADCConf * config) { fConfig = config; }

inline const char * AbsSWTrigger::GetName() const { return fName.c_str(); }