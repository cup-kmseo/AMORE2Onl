#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Rtypes.h"

#include "AMOREHDF5/AMOREEDM.hh"
#include "AbsH5Event.hh"

#include "hdf5.h"

// Built AMORE events in one HDF5 file, split by trigger type into groups with
// the same layout (info / index / chs / phonon / photon):
//   /events    signal triggers   (EventInfo_t::ttype = 0)
//   /pedestal  pedestal triggers (EventInfo_t::ttype = 1, CUPDAQ convention)
// Writing: AppendEvent(info, data, group). Reading: SetReadGroup() before ReadEvent().
// The subrun record tracks the signal group's event numbers.
class H5AMOREEvent : public AbsH5Event {
public:
  enum Group : int { kSignal = 0, kPedestal = 1, kNGroup = 2 };
  static const char * GroupPath(int group);

  H5AMOREEvent();
  ~H5AMOREEvent() override;

  void Open() override;
  void Close() override;

  // Append a global event containing multiple Crystal hits to the given group
  herr_t AppendEvent(const EventInfo_t & info, const std::vector<Crystal_t> & data,
                     int group = kSignal);
  herr_t ReadEvent(int n) override;

  herr_t Flush() { return fWriteTag ? FlushBuffer() : 0; }

  void SetNDP(int ndp);
  int GetNDP();

  // Group read by ReadEvent()/GetNDP() (default: kSignal). Call before the first read.
  void SetReadGroup(int group);

  std::uint64_t GetNWritten(int group) const;

  // Accessor for the currently read event data
  Crystal_t * GetData();

protected:
  herr_t FlushBuffer() override;

private:
  // Per-group write state
  struct WGroup {
    hid_t dsetInfo{H5I_INVALID_HID};
    hid_t dsetIndex{H5I_INVALID_HID};
    hid_t dsetChs{H5I_INVALID_HID};
    hid_t dsetPhonon{H5I_INVALID_HID};
    hid_t dsetPhoton{H5I_INVALID_HID};

    std::uint64_t nEvents{0};
    std::uint64_t nCrystals{0};

    std::vector<EventInfo_t>     evtBuf;
    std::vector<CrystalHeader_t> chBuf;
    std::vector<std::uint16_t>   phononBuf;
    std::vector<std::uint16_t>   photonBuf;
  };

  bool   CreateGroup(int group);
  herr_t FlushGroup(WGroup & g);
  void   CloseGroup(WGroup & g);

  WGroup fW[kNGroup];

  // HDF5 Types and read-side datasets (fDsetInfo lives in AbsH5Event)
  hid_t fChType{H5I_INVALID_HID};
  hid_t fDsetIndex{H5I_INVALID_HID};
  hid_t fDsetChs{H5I_INVALID_HID};
  hid_t fDsetPhonon{H5I_INVALID_HID}; // Separated dataset for phonon
  hid_t fDsetPhoton{H5I_INVALID_HID}; // Separated dataset for photon

  int fNDP{0};
  int fReadGroup{kSignal};

  // Pre-allocated vector buffer for zero-overhead user access
  std::vector<Crystal_t> fDataBuf;

  // Trackers and Cached Dataspaces
  hid_t fCurrentReadFid{H5I_INVALID_HID};
  hid_t fFileSpaceInfo{H5I_INVALID_HID};
  hid_t fFileSpaceIndex{H5I_INVALID_HID};
  hid_t fFileSpaceChs{H5I_INVALID_HID};
  hid_t fFileSpacePhonon{H5I_INVALID_HID};
  hid_t fFileSpacePhoton{H5I_INVALID_HID};

  // ==========================================
  // Full Prefetching Buffers (Memory Safe Window)
  // ==========================================
  std::vector<EventInfo_t> fReadBufInfo;
  std::vector<std::uint64_t> fReadBufIndex;
  std::vector<CrystalHeader_t> fPrefetchChs;
  std::vector<std::uint16_t> fPrefetchPhonon;
  std::vector<std::uint16_t> fPrefetchPhoton;

  int fReadBufStart{-1};
  int fReadBufSize{0};
  std::uint64_t fPrefetchChStart{0};

  ClassDef(H5AMOREEvent, 0)
};

// === inline definitions ===

inline const char * H5AMOREEvent::GroupPath(int group)
{
  return group == kPedestal ? "/pedestal" : "/events";
}

inline void H5AMOREEvent::SetNDP(int ndp) { fNDP = ndp; }

inline void H5AMOREEvent::SetReadGroup(int group)
{
  fReadGroup = (group == kPedestal) ? kPedestal : kSignal;
  fCurrentReadFid = H5I_INVALID_HID;  // force dataset reopen on next read
}

inline std::uint64_t H5AMOREEvent::GetNWritten(int group) const
{
  return (group >= 0 && group < kNGroup) ? fW[group].nEvents : 0;
}

inline Crystal_t * H5AMOREEvent::GetData() { return fDataBuf.data(); }
