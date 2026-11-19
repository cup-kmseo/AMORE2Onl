#include <algorithm>
#include <cstring>
#include <string>

#include "TError.h"

#include "AMOREHDF5/H5AMOREEvent.hh"

ClassImp(H5AMOREEvent)

H5AMOREEvent::H5AMOREEvent()
  : AbsH5Event()
{
}

H5AMOREEvent::~H5AMOREEvent()
{
  if (fWriteTag) { FlushBuffer(); }
}

bool H5AMOREEvent::CreateGroup(int group)
{
  WGroup & g = fW[group];
  const std::string grp = GroupPath(group);

  htri_t gexists = H5Lexists(fFile, grp.c_str(), H5P_DEFAULT);
  if (gexists < 0) {
    Error("Open", "H5Lexists(%s) failed", grp.c_str());
    return false;
  }
  if (gexists == 0) {
    hid_t gid = H5Gcreate2(fFile, grp.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (gid < 0) {
      Error("Open", "Failed to create group %s", grp.c_str());
      return false;
    }
    H5Gclose(gid);
  }

  // Extendable 1D datasets for metadata
  hsize_t dims[1] = {0};
  hsize_t maxdims[1] = {H5S_UNLIMITED};
  hid_t space1d = H5Screate_simple(1, dims, maxdims);

  hid_t dcpl1d = H5Pcreate(H5P_DATASET_CREATE);
  hsize_t chunk1d = (fBufCap > 0) ? static_cast<hsize_t>(fBufCap) : 1024;
  H5Pset_chunk(dcpl1d, 1, &chunk1d);
  H5Pset_deflate(dcpl1d, fCompressionLevel);

  g.dsetInfo  = H5Dcreate2(fFile, (grp + "/info").c_str(), fEvtType, space1d, H5P_DEFAULT,
                           dcpl1d, H5P_DEFAULT);
  g.dsetIndex = H5Dcreate2(fFile, (grp + "/index").c_str(), H5T_NATIVE_ULLONG, space1d,
                           H5P_DEFAULT, dcpl1d, H5P_DEFAULT);
  g.dsetChs   = H5Dcreate2(fFile, (grp + "/chs").c_str(), fChType, space1d, H5P_DEFAULT, dcpl1d,
                           H5P_DEFAULT);

  H5Pclose(dcpl1d);
  H5Sclose(space1d);

  // Extendable 2D datasets for dual waveforms (phonon & photon)
  hsize_t dims2[2] = {0, static_cast<hsize_t>(fNDP)};
  hsize_t maxdims2[2] = {H5S_UNLIMITED, static_cast<hsize_t>(fNDP)};
  hid_t space2d = H5Screate_simple(2, dims2, maxdims2);

  hid_t dcpl2d = H5Pcreate(H5P_DATASET_CREATE);
  // Chunk size along the event axis must be small to avoid huge memory allocation.
  hsize_t chunk2d[2] = {2, static_cast<hsize_t>(fNDP)};
  H5Pset_chunk(dcpl2d, 2, chunk2d);
  H5Pset_deflate(dcpl2d, fCompressionLevel);

  g.dsetPhonon = H5Dcreate2(fFile, (grp + "/phonon").c_str(), H5T_NATIVE_USHORT, space2d,
                            H5P_DEFAULT, dcpl2d, H5P_DEFAULT);
  g.dsetPhoton = H5Dcreate2(fFile, (grp + "/photon").c_str(), H5T_NATIVE_USHORT, space2d,
                            H5P_DEFAULT, dcpl2d, H5P_DEFAULT);

  H5Pclose(dcpl2d);
  H5Sclose(space2d);

  g.nEvents = 0;
  g.nCrystals = 0;
  g.evtBuf.clear();
  g.chBuf.clear();
  g.phononBuf.clear();
  g.photonBuf.clear();

  if (g.dsetInfo < 0 || g.dsetIndex < 0 || g.dsetChs < 0 || g.dsetPhonon < 0 ||
      g.dsetPhoton < 0) {
    Error("Open", "Failed to create datasets in %s", grp.c_str());
    return false;
  }
  return true;
}

void H5AMOREEvent::Open()
{
  fEvtType = EventInfo_t::BuildType();
  fChType = CrystalHeader_t::BuildType();

  InitSubRun();

  if (!fWriteTag) { return; }

  if (fFile < 0) {
    Error("Open", "invalid file id (fFile = %d). SetFileId must be called before Open().",
          static_cast<int>(fFile));
    return;
  }

  if (fNDP <= 0 || fNDP > kH5AMORENDPMAX) {
    Error("Open", "Invalid NDP: %d (max %d)", fNDP, kH5AMORENDPMAX);
    return;
  }

  for (int group = 0; group < kNGroup; ++group)
    if (!CreateGroup(group)) return;

  fMemSize = 0;
  fBufCount = 0;
  fBufBytesUsed = 0;
}

int H5AMOREEvent::GetNDP()
{
  if (fWriteTag || fNDP > 0) { return fNDP; }

  if (fFileSpacePhonon >= 0) {
    hsize_t dims[2] = {0, 0};
    H5Sget_simple_extent_dims(fFileSpacePhonon, dims, nullptr);
    fNDP = static_cast<int>(dims[1]);
    return fNDP;
  }

  hid_t fid = H5I_INVALID_HID;
  int dummy_evt = 0;

  if (fChain && fChain->GetNFile() > 0) { fid = fChain->GetFileId(0, dummy_evt); }
  else if (fFile >= 0) {
    fid = fFile;
  }

  if (fid < 0) { return 0; }

  const std::string path = std::string(GroupPath(fReadGroup)) + "/phonon";
  hid_t dset = H5Dopen2(fid, path.c_str(), H5P_DEFAULT);
  if (dset < 0) { return 0; }

  hid_t space = H5Dget_space(dset);
  hsize_t dims[2] = {0, 0};
  H5Sget_simple_extent_dims(space, dims, nullptr);
  H5Sclose(space);
  H5Dclose(dset);

  fNDP = (dims[1] > 0 && dims[1] <= static_cast<hsize_t>(kH5AMORENDPMAX))
             ? static_cast<int>(dims[1])
             : 0;
  return fNDP;
}

herr_t H5AMOREEvent::FlushGroup(WGroup & g)
{
  const std::size_t nEvtBuf = g.evtBuf.size();
  if (nEvtBuf == 0) return 0;

  const std::size_t nChBuf = g.chBuf.size();
  herr_t status = 0;

  auto append1d = [&](hid_t dset, hid_t mtype, std::uint64_t old_n, std::size_t n,
                      const void * buf) -> herr_t {
    hsize_t new_dim[1] = {old_n + n};
    H5Dset_extent(dset, new_dim);
    hid_t file_space = H5Dget_space(dset);
    hsize_t offset[1] = {old_n};
    hsize_t count[1] = {n};
    H5Sselect_hyperslab(file_space, H5S_SELECT_SET, offset, nullptr, count, nullptr);
    hid_t mem_space = H5Screate_simple(1, count, nullptr);
    herr_t st = H5Dwrite(dset, mtype, mem_space, file_space, H5P_DEFAULT, buf);
    H5Sclose(mem_space);
    H5Sclose(file_space);
    return st;
  };

  // 1. events info
  status = append1d(g.dsetInfo, fEvtType, g.nEvents, nEvtBuf, g.evtBuf.data());
  if (status < 0) return status;

  // 2. event index (first crystal of each event)
  {
    std::vector<std::uint64_t> indexBuf(nEvtBuf);
    std::uint64_t running = g.nCrystals;
    for (std::size_t i = 0; i < nEvtBuf; ++i) {
      indexBuf[i] = running;
      running += static_cast<std::uint64_t>(g.evtBuf[i].nhit);
    }
    status = append1d(g.dsetIndex, H5T_NATIVE_ULLONG, g.nEvents, nEvtBuf, indexBuf.data());
    if (status < 0) return status;
  }

  // 3. crystal headers and phonon / photon waveforms
  if (nChBuf > 0) {
    status = append1d(g.dsetChs, fChType, g.nCrystals, nChBuf, g.chBuf.data());
    if (status < 0) return status;

    hsize_t new_dims[2] = {g.nCrystals + nChBuf, static_cast<hsize_t>(fNDP)};
    H5Dset_extent(g.dsetPhonon, new_dims);
    H5Dset_extent(g.dsetPhoton, new_dims);

    hsize_t offset_wave[2] = {g.nCrystals, 0};
    hsize_t count_wave[2] = {nChBuf, static_cast<hsize_t>(fNDP)};
    hid_t mem_space_wave = H5Screate_simple(2, count_wave, nullptr);

    for (auto [dset, buf] : {std::make_pair(g.dsetPhonon, g.phononBuf.data()),
                             std::make_pair(g.dsetPhoton, g.photonBuf.data())}) {
      hid_t file_space = H5Dget_space(dset);
      H5Sselect_hyperslab(file_space, H5S_SELECT_SET, offset_wave, nullptr, count_wave, nullptr);
      status = H5Dwrite(dset, H5T_NATIVE_USHORT, mem_space_wave, file_space, H5P_DEFAULT, buf);
      H5Sclose(file_space);
      if (status < 0) break;
    }
    H5Sclose(mem_space_wave);
    if (status < 0) return status;
  }

  g.nEvents += static_cast<std::uint64_t>(nEvtBuf);
  g.nCrystals += static_cast<std::uint64_t>(nChBuf);

  g.evtBuf.clear();
  g.chBuf.clear();
  g.phononBuf.clear();
  g.photonBuf.clear();
  return status;
}

herr_t H5AMOREEvent::FlushBuffer()
{
  herr_t status = 0;
  bool wrote = false;
  for (auto & g : fW) {
    if (g.evtBuf.empty()) continue;
    status = FlushGroup(g);
    if (status < 0) return status;
    wrote = true;
  }

  fBufCount = 0;
  fBufBytesUsed = 0;

  if (wrote) H5Fflush(fFile, H5F_SCOPE_GLOBAL);
  return status;
}

void H5AMOREEvent::CloseGroup(WGroup & g)
{
  for (hid_t * d : {&g.dsetInfo, &g.dsetIndex, &g.dsetChs, &g.dsetPhonon, &g.dsetPhoton}) {
    if (*d >= 0) {
      H5Dclose(*d);
      *d = H5I_INVALID_HID;
    }
  }
}

void H5AMOREEvent::Close()
{
  if (fWriteTag) { FlushBuffer(); }

  for (auto & g : fW) CloseGroup(g);

  for (hid_t * sp : {&fFileSpaceInfo, &fFileSpaceIndex, &fFileSpaceChs, &fFileSpacePhonon,
                     &fFileSpacePhoton}) {
    if (*sp >= 0) {
      H5Sclose(*sp);
      *sp = H5I_INVALID_HID;
    }
  }
  for (hid_t * d : {&fDsetInfo, &fDsetIndex, &fDsetChs, &fDsetPhonon, &fDsetPhoton}) {
    if (*d >= 0) {
      H5Dclose(*d);
      *d = H5I_INVALID_HID;
    }
  }

  fCurrentReadFid = H5I_INVALID_HID;

  fReadBufInfo.clear();
  fReadBufIndex.clear();
  fPrefetchChs.clear();
  fPrefetchPhonon.clear();
  fPrefetchPhoton.clear();

  if (fEvtType >= 0) {
    H5Tclose(fEvtType);
    fEvtType = H5I_INVALID_HID;
  }
  if (fChType >= 0) {
    H5Tclose(fChType);
    fChType = H5I_INVALID_HID;
  }

  CloseSubRun();
}

herr_t H5AMOREEvent::AppendEvent(const EventInfo_t & info, const std::vector<Crystal_t> & data,
                                 int group)
{
  if (!fWriteTag) { return -1; }
  if (group < 0 || group >= kNGroup) {
    Error("AppendEvent", "Invalid group: %d", group);
    return -1;
  }
  if (fNDP <= 0 || fNDP > kH5AMORENDPMAX) {
    Error("AppendEvent", "Invalid NDP: %d", fNDP);
    return -1;
  }

  WGroup & g = fW[group];
  const std::uint16_t nhit = static_cast<std::uint16_t>(data.size());

  EventInfo_t info_local = info;
  info_local.nhit = nhit;
  g.evtBuf.push_back(info_local);

  const std::size_t addBytes =
      sizeof(EventInfo_t) +
      static_cast<std::size_t>(nhit) *
          (sizeof(CrystalHeader_t) + 2 * static_cast<std::size_t>(fNDP) * sizeof(std::uint16_t));

  for (std::size_t i = 0; i < nhit; ++i) {
    CrystalHeader_t h{};
    h.id = data[i].id;
    h.ndp = static_cast<std::uint16_t>(fNDP);
    h.ttime = data[i].ttime;
    h.trgbit = data[i].trgbit;
    h.npulse = data[i].npulse;
    g.chBuf.push_back(h);

    const std::uint16_t * src_pn = data[i].phonon;
    g.phononBuf.insert(g.phononBuf.end(), src_pn, src_pn + static_cast<std::size_t>(fNDP));

    const std::uint16_t * src_pt = data[i].photon;
    g.photonBuf.insert(g.photonBuf.end(), src_pt, src_pt + static_cast<std::size_t>(fNDP));
  }

  // The subrun record follows the signal event numbers.
  if (group == kSignal) UpdateSubRun(info.tnum);

  fMemSize += static_cast<hsize_t>(addBytes);
  fBufCount += 1;
  fBufBytesUsed += addBytes;

  if ((fBufCap > 0 && fBufCount >= fBufCap) ||
      (fBufMaxBytes > 0 && fBufBytesUsed >= fBufMaxBytes)) {
    return FlushBuffer();
  }

  return 0;
}

herr_t H5AMOREEvent::ReadEvent(int n)
{
  int evtno = n;
  bool file_changed = false;
  hid_t fid = H5I_INVALID_HID;

  if (fChain && fChain->GetNFile() > 0) { fid = fChain->GetFileId(n, evtno, &file_changed); }
  else {
    fid = fFile;
  }
  if (fCurrentReadFid != fid) { file_changed = true; }  // new file or read group changed

  if (fid < 0) return -1;

  if (file_changed) {
    if (fFileSpaceInfo >= 0) { H5Sclose(fFileSpaceInfo); }
    if (fFileSpaceIndex >= 0) { H5Sclose(fFileSpaceIndex); }
    if (fFileSpaceChs >= 0) { H5Sclose(fFileSpaceChs); }
    if (fFileSpacePhonon >= 0) { H5Sclose(fFileSpacePhonon); }
    if (fFileSpacePhoton >= 0) { H5Sclose(fFileSpacePhoton); }

    if (fDsetInfo >= 0) { H5Dclose(fDsetInfo); }
    if (fDsetIndex >= 0) { H5Dclose(fDsetIndex); }
    if (fDsetChs >= 0) { H5Dclose(fDsetChs); }
    if (fDsetPhonon >= 0) { H5Dclose(fDsetPhonon); }
    if (fDsetPhoton >= 0) { H5Dclose(fDsetPhoton); }

    hid_t dapl = H5Pcreate(H5P_DATASET_ACCESS);
    H5Pset_chunk_cache(dapl, 10007, 256 * 1024 * 1024,
                       1.0); // Increase cache for large AMORE arrays

    const std::string grp = GroupPath(fReadGroup);
    fDsetInfo = H5Dopen2(fid, (grp + "/info").c_str(), H5P_DEFAULT);
    fDsetIndex = H5Dopen2(fid, (grp + "/index").c_str(), H5P_DEFAULT);
    fDsetChs = H5Dopen2(fid, (grp + "/chs").c_str(), H5P_DEFAULT);
    fDsetPhonon = H5Dopen2(fid, (grp + "/phonon").c_str(), dapl);
    fDsetPhoton = H5Dopen2(fid, (grp + "/photon").c_str(), dapl);
    H5Pclose(dapl);

    if (fDsetInfo < 0 || fDsetIndex < 0 || fDsetChs < 0 || fDsetPhonon < 0 || fDsetPhoton < 0)
      return -1;

    fFileSpaceInfo = H5Dget_space(fDsetInfo);
    fFileSpaceIndex = H5Dget_space(fDsetIndex);
    fFileSpaceChs = H5Dget_space(fDsetChs);
    fFileSpacePhonon = H5Dget_space(fDsetPhonon);
    fFileSpacePhoton = H5Dget_space(fDsetPhoton);

    fCurrentReadFid = fid;
    fNDP = GetNDP();
    fReadBufStart = -1;
    fReadBufSize = 0;
  }

  if (evtno < fReadBufStart || evtno >= fReadBufStart + fReadBufSize) {
    hsize_t dims[1];
    H5Sget_simple_extent_dims(fFileSpaceInfo, dims, nullptr);
    int total_events = static_cast<int>(dims[0]);

    std::size_t bytes_per_event = sizeof(EventInfo_t) + sizeof(std::uint64_t) +
                                  4 * (sizeof(CrystalHeader_t) + 2 * fNDP * sizeof(std::uint16_t));
    int max_safe_events = static_cast<int>(fBufMaxBytes / bytes_per_event);
    if (max_safe_events <= 0) max_safe_events = 1; // At least prefetch 1 to prevent deadlock

    int fetch_size = std::min(max_safe_events, total_events - evtno);

    if (fetch_size > 0) {
      fReadBufInfo.resize(fetch_size);
      fReadBufIndex.resize(fetch_size);

      hsize_t offset_blk[1] = {static_cast<hsize_t>(evtno)};
      hsize_t count_blk[1] = {static_cast<hsize_t>(fetch_size)};
      hid_t mem_space_blk = H5Screate_simple(1, count_blk, nullptr);

      H5Sselect_hyperslab(fFileSpaceInfo, H5S_SELECT_SET, offset_blk, nullptr, count_blk, nullptr);
      H5Dread(fDsetInfo, fEvtType, mem_space_blk, fFileSpaceInfo, H5P_DEFAULT, fReadBufInfo.data());

      H5Sselect_hyperslab(fFileSpaceIndex, H5S_SELECT_SET, offset_blk, nullptr, count_blk, nullptr);
      H5Dread(fDsetIndex, H5T_NATIVE_ULLONG, mem_space_blk, fFileSpaceIndex, H5P_DEFAULT,
              fReadBufIndex.data());
      H5Sclose(mem_space_blk);

      std::uint64_t first_ch = fReadBufIndex[0];
      std::uint64_t total_chs =
          fReadBufIndex[fetch_size - 1] + fReadBufInfo[fetch_size - 1].nhit - first_ch;

      if (total_chs > 0) {
        fPrefetchChs.resize(total_chs);
        fPrefetchPhonon.resize(total_chs * fNDP);
        fPrefetchPhoton.resize(total_chs * fNDP);

        hsize_t offset_ch[1] = {first_ch};
        hsize_t count_ch[1] = {total_chs};
        H5Sselect_hyperslab(fFileSpaceChs, H5S_SELECT_SET, offset_ch, nullptr, count_ch, nullptr);
        hid_t mem_space_chs = H5Screate_simple(1, count_ch, nullptr);
        H5Dread(fDsetChs, fChType, mem_space_chs, fFileSpaceChs, H5P_DEFAULT, fPrefetchChs.data());
        H5Sclose(mem_space_chs);

        hsize_t offset_wave[2] = {first_ch, 0};
        hsize_t count_wave[2] = {total_chs, static_cast<hsize_t>(fNDP)};
        hid_t mem_space_wave = H5Screate_simple(2, count_wave, nullptr);

        H5Sselect_hyperslab(fFileSpacePhonon, H5S_SELECT_SET, offset_wave, nullptr, count_wave,
                            nullptr);
        H5Dread(fDsetPhonon, H5T_NATIVE_USHORT, mem_space_wave, fFileSpacePhonon, H5P_DEFAULT,
                fPrefetchPhonon.data());

        H5Sselect_hyperslab(fFileSpacePhoton, H5S_SELECT_SET, offset_wave, nullptr, count_wave,
                            nullptr);
        H5Dread(fDsetPhoton, H5T_NATIVE_USHORT, mem_space_wave, fFileSpacePhoton, H5P_DEFAULT,
                fPrefetchPhoton.data());

        H5Sclose(mem_space_wave);
      }

      fReadBufStart = evtno;
      fReadBufSize = fetch_size;
      fPrefetchChStart = first_ch;
    }
    else {
      return -1;
    }
  }

  int local_idx = evtno - fReadBufStart;
  fEvtInfo = fReadBufInfo[local_idx];
  std::uint64_t ch_offset = fReadBufIndex[local_idx];

  const std::uint16_t nhit = fEvtInfo.nhit;
  fDataBuf.resize(nhit);

  if (nhit > 0) {
    std::uint64_t local_ch_idx = ch_offset - fPrefetchChStart;

    for (std::size_t ich = 0; ich < nhit; ++ich) {
      fDataBuf[ich].id = fPrefetchChs[local_ch_idx + ich].id;
      fDataBuf[ich].ttime = fPrefetchChs[local_ch_idx + ich].ttime;
      fDataBuf[ich].trgbit = fPrefetchChs[local_ch_idx + ich].trgbit;
      fDataBuf[ich].npulse = fPrefetchChs[local_ch_idx + ich].npulse;

      std::uint16_t * dst_pn = fDataBuf[ich].phonon;
      const std::uint16_t * src_pn = &fPrefetchPhonon[(local_ch_idx + ich) * fNDP];
      std::memcpy(dst_pn, src_pn, static_cast<std::size_t>(fNDP) * sizeof(std::uint16_t));

      std::uint16_t * dst_pt = fDataBuf[ich].photon;
      const std::uint16_t * src_pt = &fPrefetchPhoton[(local_ch_idx + ich) * fNDP];
      std::memcpy(dst_pt, src_pt, static_cast<std::size_t>(fNDP) * sizeof(std::uint16_t));
    }
  }

  return 0;
}