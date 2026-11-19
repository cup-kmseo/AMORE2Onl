#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/TrgCondition.hh"

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
  int DoTriggerXtal(std::vector<XtalTrigger> & out) override;

  // Emit crystals still inside their dead time (end of run).
  void FlushPending(std::vector<XtalTrigger> & out) override;

protected:
  bool PrepareAlgo() override;


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
};
