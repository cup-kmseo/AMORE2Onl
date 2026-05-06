#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "hdf5.h"

#include "DAQUtils/ELog.hh"
#include "AMOREHDF5/H5AMOREStream.hh"
#include "AMORESystem/AMOREADCConf.hh"
#include "AMOREDAQ/AMOREDAQManager.hh"

static hid_t OpenStreamFile(const std::string & path, int compression)
{
  namespace fs = std::filesystem;
  fs::path parent = fs::path(path).parent_path();
  if (!parent.empty() && !fs::exists(parent))
    fs::create_directories(parent);

  hid_t fid = H5Fcreate(path.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (fid < 0) return -1;

  hid_t spc = H5Screate(H5S_SCALAR);
  hid_t att = H5Acreate2(fid, "compression_level", H5T_STD_I32LE, spc, H5P_DEFAULT, H5P_DEFAULT);
  if (att >= 0) { H5Awrite(att, H5T_NATIVE_INT, &compression); H5Aclose(att); }
  H5Sclose(spc);

  return fid;
}

void AMOREDAQManager::TF_WriteStream_AMORE()
{
  if (!ThreadWait(fRunStatus, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  const int nadc = GetEntries();
  const int nch  = AMORE::kNCHPERADC;

  std::string first_file = fOutputFilename + ".00000";
  hid_t fid = OpenStreamFile(first_file, fCompressionLevel);
  if (fid < 0) {
    ERROR("Cannot create stream HDF5 file: %s", first_file.c_str());
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }
  INFO("%s opened", first_file.c_str());

  std::vector<std::unique_ptr<H5AMOREStream>> streams(nadc);
  for (int i = 0; i < nadc; ++i) {
    auto * adc = static_cast<AbsADC *>(fCont[i]);
    streams[i] = std::make_unique<H5AMOREStream>();
    if (streams[i]->Open(fid, adc->GetSID(), nch, 4096, fCompressionLevel) < 0) {
      ERROR("H5AMOREStream::Open failed for SID %d", adc->GetSID());
      RUNSTATE::SetError(fRunStatus);
      fWriteStatus = PROCSTATE::ERROR;
      H5Fclose(fid);
      return;
    }
  }

  // Per-ADC decode buffers
  std::vector<std::vector<std::vector<std::uint16_t>>> adc_buf(
      nadc, std::vector<std::vector<std::uint16_t>>(nch));
  std::vector<std::vector<std::uint64_t>> time_buf(nadc);
  std::vector<const std::uint16_t *> ch_ptrs(nch);

  // Accumulate total samples written across all files (for final log)
  std::vector<std::uint64_t> total_samples(nadc, 0);

  fWriteStatus = RUNNING;

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    // --- File split ---
    if (fDoSplitOutputFile) {
      for (int i = 0; i < nadc; ++i) {
        total_samples[i] += streams[i]->GetNSamples();
        streams[i]->Close();
      }
      H5Fflush(fid, H5F_SCOPE_GLOBAL);
      H5Fclose(fid);

      fSubRunNumber += 1;
      char newname[512];
      std::snprintf(newname, sizeof(newname), "%s.%05d",
                    fOutputFilename.c_str(), fSubRunNumber);
      fid = OpenStreamFile(newname, fCompressionLevel);
      if (fid < 0) {
        ERROR("Cannot create split stream file: %s", newname);
        RUNSTATE::SetError(fRunStatus);
        fWriteStatus = PROCSTATE::ERROR;
        break;
      }
      INFO("%s opened", newname);

      for (int i = 0; i < nadc; ++i) {
        auto * adc = static_cast<AbsADC *>(fCont[i]);
        if (streams[i]->Open(fid, adc->GetSID(), nch, 4096, fCompressionLevel) < 0) {
          ERROR("H5AMOREStream::Open failed for SID %d after split", adc->GetSID());
          RUNSTATE::SetError(fRunStatus);
          fWriteStatus = PROCSTATE::ERROR;
          break;
        }
      }
      if (RUNSTATE::CheckError(fRunStatus)) break;
      fDoSplitOutputFile = false;
    }

    // --- End condition: ReadData ended and all ADC buffers empty ---
    if (fReadStatus == ENDED) {
      int remain = 0;
      for (int i = 0; i < nadc; ++i)
        remain += static_cast<AbsADC *>(fCont[i])->Bsize();
      if (remain == 0) break;
    }

    // --- Pop and write one chunk per ADC ---
    bool any_data = false;
    for (int i = 0; i < nadc; ++i) {
      auto * adc  = static_cast<AbsADC *>(fCont[i]);
      auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());

      auto chunk = adc->Bpop_front();
      if (!chunk) continue;
      any_data = true;

      const int ndp           = kKILOBYTES * chunk->size / 64;
      const unsigned char * data = chunk->data;

      for (int ch = 0; ch < nch; ++ch) adc_buf[i][ch].resize(ndp);
      time_buf[i].resize(ndp);

      for (int j = 0; j < ndp; ++j) {
        const int off = j * 64;
        for (int ch = 0; ch < nch; ++ch) {
          if (conf->ZSU() && conf->PID(ch) == 0) { adc_buf[i][ch][j] = 0; continue; }
          std::uint32_t val  = static_cast<std::uint32_t>(data[off + ch * 3]     & 0xFF);
          val               |= static_cast<std::uint32_t>(data[off + ch * 3 + 1] & 0xFF) << 8;
          adc_buf[i][ch][j]  = static_cast<std::uint16_t>(val);
        }
        std::uint64_t t = 0;
        for (int k = 0; k < 6; ++k)
          t |= static_cast<std::uint64_t>(data[off + 48 + k] & 0xFF) << (k * 8);
        time_buf[i][j] = t * 1000; // ns
      }

      for (int ch = 0; ch < nch; ++ch) ch_ptrs[ch] = adc_buf[i][ch].data();

      if (streams[i]->AppendChunk(ch_ptrs.data(), time_buf[i].data(), ndp) < 0) {
        ERROR("H5AMOREStream::AppendChunk failed for SID %d", adc->GetSID());
        RUNSTATE::SetError(fRunStatus);
        fWriteStatus = PROCSTATE::ERROR;
        break;
      }
    }

    if (RUNSTATE::CheckError(fRunStatus)) break;
    if (!any_data) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // --- Final flush and close ---
  for (int i = 0; i < nadc; ++i) {
    total_samples[i] += streams[i]->GetNSamples();
    streams[i]->Close();
  }
  H5Fflush(fid, H5F_SCOPE_GLOBAL);
  H5Fclose(fid);

  if (fWriteStatus != PROCSTATE::ERROR) fWriteStatus = ENDED;

  std::uint64_t total = 0;
  for (auto n : total_samples) total += n;
  INFO("Stream writing ended. Total samples (all ADCs): %llu", (unsigned long long)total);
}
