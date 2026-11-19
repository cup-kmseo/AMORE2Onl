#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/TrgCondition.hh"

// One finalised crystal trigger produced by SignalTrigger.
struct XtalTrigger {
  int           xtal{-1};    // crystal slot on this ADC (channels 2*xtal, 2*xtal+1)
  unsigned long ttime{0};    // time tag of the sample that opened the trigger
  std::uint32_t trgbit{0};   // fired conditions (bits 0-7) | opening condition(s) << 8, see TrgBit
  std::vector<std::uint16_t> phonon;
  std::vector<std::uint16_t> photon;
};

// Single signal trigger path.
//
// Every raw sample, all conditions (RVH, RVL, ...) are evaluated on their
// target channel — always, so filter/window state never goes stale. Triggers
// are combined per crystal, following read_amod.C / rv_trigger_test XIDState:
//   - idle crystal + any condition fired -> open: waveform snapshot taken now
//     (FIFO DumpCurrent), dead time = DT of the phonon channel
//   - open crystal + condition fired     -> trgbit |= bit
//   - dead time elapsed                  -> record emitted
// Taking the waveform at open time means the emission delay (DT samples) is
// independent of the FIFO head/tail.
//
// The pedestal trigger stays a separate path (RandomTrigger).
class SignalTrigger : public AbsSWTrigger {
public:
  SignalTrigger();
  SignalTrigger(const char * name);
  ~SignalTrigger() override = default;

  // Must be called before Prepare().
  void SetConditions(const std::vector<TrgCondConf> & confs) { fCondConfs = confs; }

  const std::vector<std::unique_ptr<AbsTrgCondition>> & GetConditions() const { return fConds; }

  // Process samples until at least one crystal record is finalised.
  // Returns the number of records appended to out (> 0), 0 when more data is
  // needed, -1 on FIFO error / stop.
  int DoTriggerXtal(std::vector<XtalTrigger> & out);

  // Emit crystals still inside their dead time (end of run).
  void Flush(std::vector<XtalTrigger> & out);

protected:
  bool PrepareAlgo() override;

  // Unused: conditions are evaluated per crystal in DoTriggerXtal().
  bool EvalChannel(int, unsigned short) override { return false; }

private:
  struct XtalState {
    bool        open{false};
    int         dtcnt{0};
    XtalTrigger rec{};
  };

  std::vector<TrgCondConf>                      fCondConfs;
  std::vector<std::unique_ptr<AbsTrgCondition>> fConds;

  XtalState fXtal[AMORE::kNXTALPERADC];
  int       fXtalDT[AMORE::kNXTALPERADC]{};

  // Full-ADC waveform snapshot buffers (one row per channel, RL samples)
  std::vector<std::vector<unsigned short>> fDumpStore;
  std::vector<unsigned short *>            fDumpADC;
  std::vector<unsigned long>               fDumpTime;
};
