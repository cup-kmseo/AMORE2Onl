#include <cstring>
#include <iostream>
#include <string>

#include "AMOREHDF5/H5AMOREStream.hh"

H5AMOREStream::~H5AMOREStream() { Close(); }

herr_t H5AMOREStream::Open(hid_t file_id, int sid, int nch, int buf_samples, int compression)
{
  if (file_id < 0 || nch <= 0) return -1;

  fSID         = sid;
  fNCH         = nch;
  fCompression = compression;
  fBufMaxSamples = static_cast<std::size_t>(buf_samples > 0 ? buf_samples : 4096);
  fBufSamples  = 0;
  fNSamples    = 0;
  fTimeBuf.resize(fBufMaxSamples);
  fADCBuf.resize(fBufMaxSamples * static_cast<std::size_t>(fNCH));

  // Create /stream group (once per file; ignore if already exists)
  if (!H5Lexists(file_id, "/stream", H5P_DEFAULT)) {
    hid_t g = H5Gcreate2(file_id, "/stream", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (g < 0) return -1;
    H5Gclose(g);
  }

  // Create /stream/SID_{sid} group
  std::string grp_name = "/stream/SID_" + std::to_string(sid);
  {
    hid_t g = H5Gcreate2(file_id, grp_name.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (g < 0) {
      std::cerr << "[H5AMOREStream] Failed to create group " << grp_name << std::endl;
      return -1;
    }
    H5Gclose(g);
  }

  // Create /stream/SID_{sid}/time — 1D extendable uint64
  {
    hsize_t dims[1]    = {0};
    hsize_t maxdims[1] = {H5S_UNLIMITED};
    hid_t space = H5Screate_simple(1, dims, maxdims);

    hid_t dcpl = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk[1] = {static_cast<hsize_t>(fBufMaxSamples)};
    H5Pset_chunk(dcpl, 1, chunk);
    H5Pset_deflate(dcpl, fCompression);

    std::string dset_name = grp_name + "/time";
    fDsetTime = H5Dcreate2(file_id, dset_name.c_str(), H5T_STD_U64LE, space,
                           H5P_DEFAULT, dcpl, H5P_DEFAULT);
    H5Pclose(dcpl);
    H5Sclose(space);

    if (fDsetTime < 0) {
      std::cerr << "[H5AMOREStream] Failed to create dataset " << dset_name << std::endl;
      return -1;
    }
  }

  // Create /stream/SID_{sid}/adc — 2D extendable uint16 [n_samples, n_ch]
  {
    hsize_t dims[2]    = {0, static_cast<hsize_t>(fNCH)};
    hsize_t maxdims[2] = {H5S_UNLIMITED, static_cast<hsize_t>(fNCH)};
    hid_t space = H5Screate_simple(2, dims, maxdims);

    hid_t dcpl = H5Pcreate(H5P_DATASET_CREATE);
    hsize_t chunk[2] = {static_cast<hsize_t>(fBufMaxSamples), static_cast<hsize_t>(fNCH)};
    H5Pset_chunk(dcpl, 2, chunk);
    H5Pset_deflate(dcpl, fCompression);

    std::string dset_name = grp_name + "/adc";
    fDsetADC = H5Dcreate2(file_id, dset_name.c_str(), H5T_STD_U16LE, space,
                          H5P_DEFAULT, dcpl, H5P_DEFAULT);
    H5Pclose(dcpl);
    H5Sclose(space);

    if (fDsetADC < 0) {
      std::cerr << "[H5AMOREStream] Failed to create dataset " << dset_name << std::endl;
      return -1;
    }
  }

  return 0;
}

void H5AMOREStream::Close()
{
  FlushBuffer();

  if (fDsetTime >= 0) { H5Dclose(fDsetTime); fDsetTime = H5I_INVALID_HID; }
  if (fDsetADC  >= 0) { H5Dclose(fDsetADC);  fDsetADC  = H5I_INVALID_HID; }

  fBufSamples = 0;
  fNSamples   = 0;
}

herr_t H5AMOREStream::Flush()
{
  return FlushBuffer();
}

herr_t H5AMOREStream::AppendChunk(const std::uint16_t * const * adc,
                                   const std::uint64_t * time, int ndp)
{
  if (ndp <= 0 || !adc || !time) return 0;

  int src = 0;
  while (src < ndp) {
    std::size_t space = fBufMaxSamples - fBufSamples;
    std::size_t n = static_cast<std::size_t>(ndp - src);
    if (n > space) n = space;

    std::memcpy(&fTimeBuf[fBufSamples], &time[src], n * sizeof(std::uint64_t));

    for (int ch = 0; ch < fNCH; ++ch) {
      for (std::size_t s = 0; s < n; ++s)
        fADCBuf[(fBufSamples + s) * static_cast<std::size_t>(fNCH) + static_cast<std::size_t>(ch)]
            = adc[ch][src + static_cast<int>(s)];
    }

    fBufSamples += n;
    src         += static_cast<int>(n);

    if (fBufSamples >= fBufMaxSamples) {
      herr_t ret = FlushBuffer();
      if (ret < 0) return ret;
    }
  }

  return 0;
}

herr_t H5AMOREStream::FlushBuffer()
{
  if (fBufSamples == 0) return 0;
  if (fDsetTime < 0 || fDsetADC < 0) return -1;

  const hsize_t n = static_cast<hsize_t>(fBufSamples);

  // --- extend and write time ---
  {
    hsize_t new_dim[1] = {fNSamples + n};
    H5Dset_extent(fDsetTime, new_dim);

    hid_t fspace = H5Dget_space(fDsetTime);
    hsize_t offset[1] = {fNSamples};
    hsize_t count[1]  = {n};
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, offset, nullptr, count, nullptr);
    hid_t mspace = H5Screate_simple(1, count, nullptr);
    herr_t ret = H5Dwrite(fDsetTime, H5T_STD_U64LE, mspace, fspace, H5P_DEFAULT, fTimeBuf.data());
    H5Sclose(mspace);
    H5Sclose(fspace);
    if (ret < 0) return ret;
  }

  // --- extend and write adc ---
  {
    hsize_t new_dims[2] = {fNSamples + n, static_cast<hsize_t>(fNCH)};
    H5Dset_extent(fDsetADC, new_dims);

    hid_t fspace = H5Dget_space(fDsetADC);
    hsize_t offset[2] = {fNSamples, 0};
    hsize_t count[2]  = {n, static_cast<hsize_t>(fNCH)};
    H5Sselect_hyperslab(fspace, H5S_SELECT_SET, offset, nullptr, count, nullptr);
    hid_t mspace = H5Screate_simple(2, count, nullptr);
    herr_t ret = H5Dwrite(fDsetADC, H5T_STD_U16LE, mspace, fspace, H5P_DEFAULT, fADCBuf.data());
    H5Sclose(mspace);
    H5Sclose(fspace);
    if (ret < 0) return ret;
  }

  fNSamples   += n;
  fBufSamples  = 0;
  return 0;
}
