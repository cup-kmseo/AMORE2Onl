#pragma once

#include <cstdint>
#include <string>

#include "AMORE/amoreconsts.hh"

// Configuration of one trigger condition of the single signal path
// (SignalTrigger, AMOREAlgs). Filled from the YAML "TRGCOND" block of an AMOREADC.
//
// A crystal occupies a channel pair: phonon/heat = 2*xtal (even),
// photon/light = 2*xtal + 1 (odd). Per-crystal arrays are indexed by the
// crystal slot on this ADC (0 .. kNXTALPERADC-1).

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

struct TrgCondConf {
  std::string   name{};             // e.g. "RVH"
  std::string   type{};             // algorithm: "RV"
  std::uint32_t bit{0};             // trgbit bit (1 << n)
  TrgTarget     target{TrgTarget::kHeat};

  int on  [AMORE::kNXTALPERADC]{};
  int thr [AMORE::kNXTALPERADC]{};  // RV: threshold × 1000
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
