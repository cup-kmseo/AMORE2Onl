#pragma once
#include <cstdint>
#include <vector>
#include "hdf5.h"

// Continuous-mode HDF5 writer for one AMOREADC (identified by SID).
// Writes sequential raw ADC data to:
//   /stream/SID_{sid}/time  [N_samples]        uint64 (nanoseconds)
//   /stream/SID_{sid}/adc   [N_samples, N_ch]  uint16
class H5AMOREStream {
public:
  H5AMOREStream() = default;
  ~H5AMOREStream();

  // Create datasets in an already-open HDF5 file.
  // buf_samples: internal write buffer depth (samples) before flushing to disk.
  herr_t Open(hid_t file_id, int sid, int nch, int buf_samples = 4096, int compression = 1);
  void   Close();
  herr_t Flush();

  // adc[ch][sample], ndp = number of samples in this chunk.
  herr_t AppendChunk(const std::uint16_t * const * adc, const std::uint64_t * time, int ndp);

  std::uint64_t GetNSamples() const { return fNSamples; }

private:
  int    fSID{-1};
  int    fNCH{0};
  int    fCompression{1};

  hid_t  fDsetTime{H5I_INVALID_HID};
  hid_t  fDsetADC{H5I_INVALID_HID};

  std::uint64_t fNSamples{0};

  // write buffer
  std::vector<std::uint64_t> fTimeBuf;
  std::vector<std::uint16_t> fADCBuf;    // row-major [sample, ch]
  std::size_t  fBufSamples{0};
  std::size_t  fBufMaxSamples{4096};

  herr_t FlushBuffer();
};
