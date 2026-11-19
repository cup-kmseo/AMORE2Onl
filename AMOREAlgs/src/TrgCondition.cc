#include <iostream>

#include "AMOREAlgs/RVTemplate.hh"
#include "AMOREAlgs/TrgCondition.hh"
#include "AMORESystem/AMOREADCConf.hh"

bool AbsTrgCondition::Prepare(const TrgCondConf & cc, const AMOREADCConf * adc)
{
  fCC  = cc;
  fADC = adc;
  if (!fADC) {
    std::cerr << "[" << GetName() << "] ADC config is not set" << std::endl;
    return false;
  }
  if (fCC.bit == 0) {
    std::cerr << "[" << GetName() << "] trigger bit is not set" << std::endl;
    return false;
  }
  return PrepareAlgo();
}

bool RVCondition::PrepareAlgo()
{
  RVParams par;
  par.bwfOrder = fCC.bwfOrder;
  par.bwfLC    = fCC.bwfLC;
  par.bwfUC    = fCC.bwfUC;
  par.nwin     = fCC.nwin;
  par.ds       = fCC.ds;
  par.sr       = fADC->SamplingFreqHz();  // SR is [us / sample]
  par.warmup   = fADC->DLY();

  RVTemplateFile tf;
  if (!tf.Open(fADC->RVTmpltFile())) {
    std::cerr << "[" << GetName() << "] " << tf.Error() << std::endl;
    return false;
  }

  const bool useFR50 = !fCC.fr50Pfx.empty() && fCC.fr50Pfx != fCC.tmpltPfx;

  for (int x = 0; x < AMORE::kNXTALPERADC; ++x) {
    if (!IsOn(x)) continue;

    // Template histograms are indexed by crystal id = PID(phonon ch) / 2, the same id
    // TF_SWTrigger writes to Crystal_t::id.
    const int xid = fADC->PID(2 * x) / 2;

    std::vector<double> tmpl, fr50;
    if (!tf.Load(fCC.tmpltPfx, xid, tmpl) || (useFR50 && !tf.Load(fCC.fr50Pfx, xid, fr50))) {
      std::cerr << "[" << GetName() << "] xtal" << x << ": " << tf.Error() << std::endl;
      return false;
    }

    par.thr  = static_cast<double>(fCC.thr[x]) * 1e-3;
    par.thry = 1e-6 * fCC.thry[x] * fCC.thry[x];  // same convention as AMOREADCConf::SetRVTHRY
    par.thrx = 1e-6 * fCC.thrx[x] * fCC.thrx[x];

    std::string err;
    if (!fCore[x].Prepare(par, tmpl, useFR50 ? &fr50 : nullptr, err)) {
      std::cerr << "[" << GetName() << "] xtal" << x << ": " << err << std::endl;
      return false;
    }
  }
  return true;
}

std::unique_ptr<AbsTrgCondition> MakeTrgCondition(const std::string & type)
{
  if (type == "RV") return std::make_unique<RVCondition>();
  return nullptr;
}
