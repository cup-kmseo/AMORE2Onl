#include <algorithm>
#include <iostream>

#include "AMOREAlgs/SignalTrigger.hh"

SignalTrigger::SignalTrigger()
  : AbsSWTrigger("SignalTrigger")
{
}

SignalTrigger::SignalTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool SignalTrigger::PrepareAlgo()
{
  fConds.clear();
  if (fCondConfs.empty()) {
    std::cerr << "[" << fName << "] no trigger condition configured" << std::endl;
    return false;
  }

  for (const auto & cc : fCondConfs) {
    auto cond = MakeTrgCondition(cc.type);
    if (!cond) {
      std::cerr << "[" << fName << "] unknown condition type '" << cc.type
                << "' for " << cc.name << std::endl;
      return false;
    }
    if (!cond->Prepare(cc, fConfig)) return false;
    fConds.push_back(std::move(cond));
  }

  for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
    fXtal[x]  = XtalState{};
    fXtalDT[x] = std::max(1, fDeadtime[2 * x]);  // crystal dead time = phonon channel DT
  }

  const int rl = fConfig->RL();
  fDumpStore.assign(AMORE::kNCHPERADC, std::vector<unsigned short>(rl));
  fDumpADC.resize(AMORE::kNCHPERADC);
  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) fDumpADC[ch] = fDumpStore[ch].data();
  fDumpTime.assign(rl, 0);

  return true;
}

int SignalTrigger::DoTriggerXtal(std::vector<XtalTrigger> & out)
{
  if (!fFIFO) return -1;

  const int nch = AMORE::kNCHPERADC;
  const int rl  = fConfig->RL();
  unsigned short binADC[nch];
  unsigned long  binTime = 0;
  const std::size_t nout0 = out.size();

  while (true) {
    const int pop = fFIFO->PopCurrent(binADC, binTime);
    if (pop == 1) return static_cast<int>(out.size() - nout0);  // wait for more data
    if (pop < 0)  return -1;

    // 1. Evaluate every condition on every enabled crystal (state always updated).
    std::uint32_t fired[AMORE::kNXTALPERADC]{};
    for (auto & cond : fConds) {
      for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
        if (!cond->IsOn(x)) continue;
        const int ch = cond->Channel(x);
        if (cond->Eval(x, static_cast<double>(binADC[ch]) - fBaseline[ch]))
          fired[x] |= cond->Bit();
      }
    }

    // 2. Open idle crystals / merge bits into open ones.
    bool needDump = false;
    for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
      if (!fired[x]) continue;
      auto & st = fXtal[x];
      if (!st.open) {
        st.open       = true;
        st.dtcnt      = fXtalDT[x];
        st.rec.xtal   = x;
        st.rec.ttime  = binTime;
        st.rec.trgbit = fired[x] | (fired[x] << TrgBit::kOpenShift);  // opening condition(s)
        st.rec.phonon.clear();  // marks "snapshot pending"
        needDump = true;
      }
      else {
        st.rec.trgbit |= fired[x];
      }
    }

    // One full-ADC snapshot serves every crystal opened on this sample.
    if (needDump) {
      if (fFIFO->DumpCurrent(fDumpADC.data(), fDumpTime.data()) <= 0) return -1;
      for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
        auto & st = fXtal[x];
        if (!st.open || !st.rec.phonon.empty()) continue;
        st.rec.phonon.assign(fDumpADC[2 * x],     fDumpADC[2 * x] + rl);
        st.rec.photon.assign(fDumpADC[2 * x + 1], fDumpADC[2 * x + 1] + rl);
      }
    }

    // 3. Count down dead time; emit finished crystals.
    for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
      auto & st = fXtal[x];
      if (!st.open) continue;
      if (--st.dtcnt == 0) {
        out.push_back(std::move(st.rec));
        st = XtalState{};
      }
    }

    if (out.size() > nout0) return static_cast<int>(out.size() - nout0);
  }
}

void SignalTrigger::Flush(std::vector<XtalTrigger> & out)
{
  for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
    auto & st = fXtal[x];
    if (!st.open) continue;
    out.push_back(std::move(st.rec));
    st = XtalState{};
  }
}
