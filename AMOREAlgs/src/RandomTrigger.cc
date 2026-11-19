#include <algorithm>
#include <cmath>
#include <iostream>

#include "AMOREAlgs/RandomTrigger.hh"

namespace {
// splitmix64 finaliser: a well-mixed 64-bit hash of the time bin.
std::uint64_t Mix(std::uint64_t x)
{
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}
constexpr std::uint64_t kSeed = 0xA40E2ULL;  // fixed: identical on every ADC / DAQ server
} // namespace

RandomTrigger::RandomTrigger()
  : AbsSWTrigger("RandomTrigger")
{
}

RandomTrigger::RandomTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RandomTrigger::PrepareAlgo()
{
  const int sr   = fConfig->SR();    // [us / sample]
  const int rate = fConfig->PTRG();  // [uHz]

  if (sr <= 0) {
    std::cerr << "[" << fName << "] invalid SR: " << sr << std::endl;
    return false;
  }

  // Probability per sample = rate[uHz] * 1e-6 * SR[us] * 1e-6
  const double p = std::clamp(rate * static_cast<double>(sr) / 1e12, 0.0, 1.0);
  fThreshold = (p >= 1.0) ? ~0ULL : static_cast<std::uint64_t>(std::ldexp(p, 64));
  fBinNs     = static_cast<unsigned long>(sr) * 1000UL;
  fLastBin   = ~0UL;
  fNXtal     = std::clamp(fConfig->NCH() / 2, 0, AMORE::kNXTALPERADC);
  return true;
}

bool RandomTrigger::FiresAt(unsigned long timeNs) const
{
  return Mix((timeNs / fBinNs) ^ kSeed) < fThreshold;
}

int RandomTrigger::DoTriggerXtal(std::vector<XtalTrigger> & out)
{
  if (!fFIFO) return -1;

  unsigned short binADC[AMORE::kNCHPERADC];
  unsigned long  binTime = 0;
  const int rl = fConfig->RL();

  while (true) {
    const int pop = fFIFO->PopCurrent(binADC, binTime);
    if (pop == 1) return 0;  // wait for more data
    if (pop < 0)  return -1;

    const unsigned long bin = binTime / fBinNs;
    if (bin == fLastBin || !FiresAt(binTime)) continue;
    fLastBin = bin;

    if (fFIFO->DumpCurrent(fDumpADC.data(), fDumpTime.data()) <= 0) return -1;

    for (int x = 0; x < fNXtal; ++x) {
      XtalTrigger rec;
      rec.xtal  = x;
      rec.ttime = binTime;
      rec.phonon.assign(fDumpADC[2 * x],     fDumpADC[2 * x] + rl);
      rec.photon.assign(fDumpADC[2 * x + 1], fDumpADC[2 * x + 1] + rl);
      out.push_back(std::move(rec));
    }
    return fNXtal;
  }
}
