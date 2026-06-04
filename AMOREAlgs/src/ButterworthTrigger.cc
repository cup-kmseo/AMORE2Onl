#include <iostream>

#include "AMOREAlgs/ButterworthTrigger.hh"

ButterworthTrigger::ButterworthTrigger()
  : AbsSWTrigger("ButterworthTrigger")
{
}

ButterworthTrigger::ButterworthTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool ButterworthTrigger::PrepareAlgo()
{
  int    order = fConfig->BWFOrder();
  double lc    = fConfig->BWFLC();
  double uc    = fConfig->BWFUC();
  double sf    = static_cast<double>(fConfig->SR());

  if (lc <= 0 || uc <= lc || sf <= 0) {
    std::cerr << "[ButterworthTrigger] Invalid filter params: "
              << "ORDER=" << order << " LC=" << lc << " UC=" << uc
              << " SR=" << sf << std::endl;
    return false;
  }

  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) {
    if (!fTrgOn[ch]) continue;
    fFilter[ch].CreateFilter(order, lc, uc, sf);
  }
  return true;
}

bool ButterworthTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  double filtered = fFilter[ch].Filter(static_cast<double>(adcVal) - fBaseline[ch]);
  return filtered > fTHR[ch];
}
