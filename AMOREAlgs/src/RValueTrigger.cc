#include <iostream>

#include "AMOREAlgs/RValueTrigger.hh"
#include "AMOREAlgs/RVTemplate.hh"

RValueTrigger::RValueTrigger()
  : AbsSWTrigger("RValueTrigger")
{
}

RValueTrigger::RValueTrigger(const char * name)
  : AbsSWTrigger(name)
{
}

bool RValueTrigger::PrepareAlgo()
{
  RVParams par;
  par.bwfOrder = fConfig->BWFOrder();
  par.bwfLC    = fConfig->BWFLC();
  par.bwfUC    = fConfig->BWFUC();
  par.nwin     = fConfig->RVNWin();
  par.ds       = fConfig->RVDS();
  par.sr       = fConfig->SamplingFreqHz();  // SR is [us / sample]
  par.warmup   = fConfig->DLY();

  RVTemplateFile tf;
  if (!tf.Open(fConfig->RVTmpltFile())) {
    std::cerr << "[" << fName << "] " << tf.Error() << std::endl;
    return false;
  }

  const std::string & pfx     = fConfig->RVTmpltHistPfx();
  const std::string & pfxFR50 = fConfig->RVTmpltHistPfxFR50();

  for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) {
    if (!fTrgOn[ch]) continue;

    // Template histograms are indexed by the channel's CID.
    const int xid = fConfig->CID(ch);

    std::vector<double> tmpl, fr50;
    const bool useFR50 = !pfxFR50.empty() && pfxFR50 != pfx;
    if (!tf.Load(pfx, xid, tmpl) || (useFR50 && !tf.Load(pfxFR50, xid, fr50))) {
      std::cerr << "[" << fName << "] ch" << ch << ": " << tf.Error() << std::endl;
      return false;
    }

    par.thr  = static_cast<double>(fTHR[ch]) * 1e-3;
    par.thry = fConfig->RVTHRY(ch);
    par.thrx = fConfig->RVTHRX(ch);

    std::string err;
    if (!fCore[ch].Prepare(par, tmpl, useFR50 ? &fr50 : nullptr, err)) {
      std::cerr << "[" << fName << "] ch" << ch << ": " << err << std::endl;
      return false;
    }
  }

  return true;
}

bool RValueTrigger::EvalChannel(int ch, unsigned short adcVal)
{
  return fCore[ch].Eval(static_cast<double>(adcVal) - fBaseline[ch]);
}

bool RValueTrigger::EvalChannelD(int ch, double adcVal)
{
  return fCore[ch].Eval(adcVal - fBaselineD[ch]);
}
