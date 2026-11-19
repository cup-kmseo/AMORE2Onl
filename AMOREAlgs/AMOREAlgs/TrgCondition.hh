#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "AMOREAlgs/RVCore.hh"
#include "AMORE/amoreconsts.hh"

class AMOREADCConf;

// Trigger conditions evaluated inside the single signal path (SignalTrigger).
//
// A crystal occupies a channel pair on the ADC: phonon/heat = 2*xtal (even),
// photon/light = 2*xtal + 1 (odd). Each condition targets one side of the pair
// and owns its own parameters and streaming state, so conditions with different
// thresholds / filters / templates (e.g. RVH and RVL) coexist in one path.

namespace AMORE {
inline constexpr int kNXTALPERADC = kNCHPERADC / 2;
}

// Signal trgbit layout (software-defined, independent of CUPDAQ hardware bits).
//   bits 0-7  : conditions that fired while the crystal was open (OR)
//                 bit 0 = RVH, bit 1 = AMH (heat amplitude), bit 2 = RVL
//                 (AMoRE-I read_amod.C convention), bits 3-7 reserved
//   bits 8-15 : condition(s) that opened the crystal = fired bits << 8
//   bits 16-31: reserved
// Pedestal triggers are written to their own group and carry no trigger bit.
namespace TrgBit {
inline constexpr std::uint32_t kRVH = 1u << 0;
inline constexpr std::uint32_t kAMH = 1u << 1;
inline constexpr std::uint32_t kRVL = 1u << 2;

inline constexpr int           kOpenShift = 8;
inline constexpr std::uint32_t kFiredMask = 0xFFu;
inline constexpr std::uint32_t kOpenMask  = 0xFFu << kOpenShift;
} // namespace TrgBit

enum class TrgTarget : int { kHeat = 0, kLight = 1 };

// Configuration of one condition. Per-crystal arrays are indexed by the
// crystal slot on this ADC (0 .. kNXTALPERADC-1).
struct TrgCondConf {
  std::string   name{};             // e.g. "RVH"
  std::string   type{};             // algorithm: "RV"
  std::uint32_t bit{0};
  TrgTarget     target{TrgTarget::kHeat};

  int on [AMORE::kNXTALPERADC]{};
  int thr[AMORE::kNXTALPERADC]{};   // RV: threshold × 1000
  int thry[AMORE::kNXTALPERADC]{};  // RV ellipse RV²  axis, ×1000 (0 = disabled)
  int thrx[AMORE::kNXTALPERADC]{};  // RV ellipse ARV² axis, ×1000 (0 = disabled)

  // Filter / RV parameters
  int    bwfOrder{2};
  double bwfLC{0};
  double bwfUC{0};
  int    nwin{100};
  int    ds{8};
  std::string tmpltPfx{"htmp"};
  std::string fr50Pfx{};            // empty = same as tmpltPfx
};

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
