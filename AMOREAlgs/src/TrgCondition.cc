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

bool RECondition::PrepareAlgo()
{
  for (int x = 0; x < AMORE::kNXTALPERADC; ++x) fPrev[x] = fCC.thr[x] - 1.0;  // start below
  return true;
}

bool RECondition::Eval(int xtal, double value)
{
  const bool fired = (fPrev[xtal] <= fCC.thr[xtal] && value > fCC.thr[xtal]);
  fPrev[xtal] = value;
  return fired;
}

bool FECondition::PrepareAlgo()
{
  // Start below threshold so the first samples cannot fake a falling crossing
  // (same as the former FallingEdgeTrigger).
  for (int x = 0; x < AMORE::kNXTALPERADC; ++x) fPrev[x] = fCC.thr[x] - 1.0;
  return true;
}

bool FECondition::Eval(int xtal, double value)
{
  const bool fired = (fPrev[xtal] >= fCC.thr[xtal] && value < fCC.thr[xtal]);
  fPrev[xtal] = value;
  return fired;
}

bool BWCondition::PrepareAlgo()
{
  const double sf = fADC->SamplingFreqHz();  // SR is [us / sample]
  if (fCC.bwfLC <= 0 || fCC.bwfUC <= fCC.bwfLC || sf <= 0 || fCC.bwfUC >= 0.5 * sf) {
    std::cerr << "[" << GetName() << "] invalid BWF: order=" << fCC.bwfOrder
              << " lc=" << fCC.bwfLC << " uc=" << fCC.bwfUC << " fs=" << sf << std::endl;
    return false;
  }
  for (int x = 0; x < AMORE::kNXTALPERADC; ++x)
    if (IsOn(x)) fFilter[x].CreateFilter(fCC.bwfOrder, fCC.bwfLC, fCC.bwfUC, sf);
  return true;
}

std::unique_ptr<AbsTrgCondition> MakeTrgCondition(const std::string & type)
{
  if (type == "RV") return std::make_unique<RVCondition>();
  if (type == "HT") return std::make_unique<HTCondition>();
  if (type == "RE") return std::make_unique<RECondition>();
  if (type == "FE") return std::make_unique<FECondition>();
  if (type == "BW") return std::make_unique<BWCondition>();
  return nullptr;
}
