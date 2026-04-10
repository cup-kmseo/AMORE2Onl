#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "hdf5.h"

constexpr int kH5AMORENDPMAX = 500000; // 5 s for AMOREADC

struct Crystal_t {
  std::uint16_t id;
  std::uint16_t ndp;
  std::uint16_t phonon[kH5AMORENDPMAX];
  std::uint16_t photon[kH5AMORENDPMAX];
  std::uint64_t ttime;
  std::uint32_t trgbit; // bitmask: bit N set if trigger path N fired

  Crystal_t() noexcept
    : id(0),
      ndp(0),
      ttime(0),
      trgbit(0)
  {
    std::memset(phonon, 0, sizeof(phonon));
    std::memset(photon, 0, sizeof(photon));
  }

  static hid_t BuildType();
  hsize_t GetSize() const noexcept;

  void SetPhonon(const std::uint16_t * wave, int ndp) noexcept;
  void SetPhoton(const std::uint16_t * wave, int ndp) noexcept;
  void SetWaveforms(const std::uint16_t * pn, const std::uint16_t * pt, int ndp) noexcept;

  const std::uint16_t * GetPhonon() const noexcept;
  const std::uint16_t * GetPhoton() const noexcept;
  void GetWaveforms(std::uint16_t * pn, std::uint16_t * pt, int ndp) const noexcept;
};

struct CrystalHeader_t {
  std::uint16_t id;
  std::uint16_t ndp;
  std::uint64_t ttime;

  static hid_t BuildType();
  hsize_t GetSize() const noexcept;
};


// === inline definitions ===

inline hid_t Crystal_t::BuildType()
{
  hid_t type = H5Tcreate(H5T_COMPOUND, sizeof(Crystal_t));
  H5Tinsert(type, "id", HOFFSET(Crystal_t, id), H5T_STD_U16LE);

  hsize_t dim[1] = {kH5AMORENDPMAX};
  hid_t arrtype = H5Tarray_create2(H5T_STD_U16LE, 1, dim);
  H5Tinsert(type, "phonon", HOFFSET(Crystal_t, phonon), arrtype);
  H5Tinsert(type, "photon", HOFFSET(Crystal_t, photon), arrtype);
  H5Tinsert(type, "ttime",   HOFFSET(Crystal_t, ttime),  H5T_STD_U64LE);
  H5Tinsert(type, "trgbit", HOFFSET(Crystal_t, trgbit), H5T_STD_U32LE);

  return type;
}

inline void Crystal_t::SetPhonon(const std::uint16_t * wave, int ndp) noexcept
{
  std::memcpy(phonon, wave, static_cast<std::size_t>(ndp) * sizeof(std::uint16_t));
}

inline void Crystal_t::SetPhoton(const std::uint16_t * wave, int ndp) noexcept
{
  std::memcpy(photon, wave, static_cast<std::size_t>(ndp) * sizeof(std::uint16_t));
}

inline void Crystal_t::SetWaveforms(const std::uint16_t * pn, const std::uint16_t * pt,
                                    int ndp) noexcept
{
  SetPhonon(pn, ndp);
  SetPhoton(pt, ndp);
}

inline const std::uint16_t * Crystal_t::GetPhonon() const noexcept
{
  return phonon;
}

inline const std::uint16_t * Crystal_t::GetPhoton() const noexcept
{
  return photon;
}

inline void Crystal_t::GetWaveforms(std::uint16_t * pn, std::uint16_t * pt,
                                    int ndp) const noexcept
{
  if (pn) std::memcpy(pn, phonon, static_cast<std::size_t>(ndp) * sizeof(std::uint16_t));
  if (pt) std::memcpy(pt, photon, static_cast<std::size_t>(ndp) * sizeof(std::uint16_t));
}

inline hsize_t Crystal_t::GetSize() const noexcept { return sizeof(Crystal_t); }

inline hid_t CrystalHeader_t::BuildType()
{
  hid_t type = H5Tcreate(H5T_COMPOUND, sizeof(CrystalHeader_t));
  H5Tinsert(type, "id", HOFFSET(CrystalHeader_t, id), H5T_STD_U16LE);
  H5Tinsert(type, "ttime", HOFFSET(CrystalHeader_t, ttime), H5T_STD_U64LE);
  return type;
}

inline hsize_t CrystalHeader_t::GetSize() const noexcept { return sizeof(CrystalHeader_t); }
