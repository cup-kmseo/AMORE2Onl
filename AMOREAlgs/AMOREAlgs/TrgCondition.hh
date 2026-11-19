#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "AMOREAlgs/RVCore.hh"
#include "AMORE/amoreconsts.hh"
#include "AMORESystem/TrgCondConf.hh"

class AMOREADCConf;

// Trigger conditions evaluated inside the single signal path (SignalTrigger).
//
// A crystal occupies a channel pair on the ADC: phonon/heat = 2*xtal (even),
// photon/light = 2*xtal + 1 (odd). Each condition targets one side of the pair
// and owns its own parameters and streaming state, so conditions with different
// thresholds / filters / templates (e.g. RVH and RVL) coexist in one path.

// Trigger bits (TrgBit) are defined in AMORESystem/TrgCondConf.hh.

class AbsTrgCondition {
public:
  virtual ~AbsTrgCondition() = default;

  // adc supplies ADC-wide settings (SR, DLY, template file, CID map).
  bool Prepare(const TrgCondConf & cc, const AMOREADCConf * adc);

  // Called for every raw sample of the target channel, regardless of dead time,
  // so streaming state stays current. value = ADC - baseline.
  virtual bool Eval(int xtal, double value) = 0;

  const TrgCondConf & Conf() const { return fCC; }
  const char *  GetName()  const { return fCC.name.c_str(); }
  std::uint32_t Bit()      const { return fCC.bit; }
  bool IsOn(int xtal)      const { return fCC.on[xtal] != 0; }
  int  Channel(int xtal)   const { return 2 * xtal + static_cast<int>(fCC.target); }

protected:
  virtual bool PrepareAlgo() = 0;

  TrgCondConf          fCC{};
  const AMOREADCConf * fADC{nullptr};
};

// R-value condition (RVH / RVL): one RVCore per enabled crystal.
class RVCondition : public AbsTrgCondition {
public:
  bool Eval(int xtal, double value) override { return fCore[xtal].Eval(value); }

  double GetLastRV(int xtal)  const { return fCore[xtal].LastRV(); }
  double GetLastARV(int xtal) const { return fCore[xtal].LastARV(); }

protected:
  bool PrepareAlgo() override;

private:
  RVCore fCore[AMORE::kNXTALPERADC];
};

// Factory by TrgCondConf::type. Returns nullptr for an unknown type.
std::unique_ptr<AbsTrgCondition> MakeTrgCondition(const std::string & type);
