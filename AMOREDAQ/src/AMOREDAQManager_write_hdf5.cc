#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "ELog.hh"
#include "H5DataWriter.hh"

#include "AMOREHDF5/AMOREEDM.hh"
#include "AMOREHDF5/H5AMOREEvent.hh"
#include "AMORESystem/AMORETCBConf.hh"
#include "AMOREDAQ/AMOREDAQManager.hh"

bool AMOREDAQManager::HasRunningTrigger() const
{
  for (const auto & st : fTrigStatus) {
    if (st == READY || st == RUNNING) return true;
  }
  return false;
}

void AMOREDAQManager::TF_WriteEvent_AMORE()
{
  if (!WaitRunState(fRunStatus, RUNSTATE::kRUNNING, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  std::string first_file = fOutputFilename + ".00000";
  if (OpenNewHDF5File(first_file.c_str()) < 0) {
    ERROR("can't create hdf5 output file %s", first_file.c_str());
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }

  auto * adc0 = fADCList[0].get();
  auto * conf0 = static_cast<AMOREADCConf *>(adc0->GetConfig());
  const int ndp = conf0->RL();

  if (ndp <= 0 || ndp > kH5AMORENDPMAX) {
    ERROR("invalid ndp: %d (max %d)", ndp, kH5AMORENDPMAX);
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }

  // Read coincidence window from TCB config (CW is in samples, convert to ns)
  auto * tcbconf = static_cast<AMORETCBConf *>(fConfigList->FindConfig(ADC::AMORETCB, 0));
  const unsigned long cw = tcbconf ? static_cast<unsigned long>(tcbconf->CW()) * fTimeDelta : 0;
  INFO("Event building with coincidence window: %lu samples = %lu ns",
       tcbconf ? static_cast<unsigned long>(tcbconf->CW()) : 0, cw);

  auto * h5event = new H5AMOREEvent;
  h5event->SetNDP(ndp);
  fH5Event = h5event;

  fHDF5File->SetData(h5event);
  if (!fHDF5File->Open()) {
    delete h5event;
    fH5Event = nullptr;
    ERROR("can't open hdf5 output file %s", fOutputFilename.c_str());
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }

  // One event builder per output group: signal -> /events, pedestal -> /pedestal.
  struct Builder {
    int                         group;
    ConcurrentDeque<Crystal_t> * queue;
    std::vector<Crystal_t>       data;
    unsigned long                start{0};
    bool                         has{false};
    std::uint64_t                nbuilt{0};
  };
  Builder builders[2] = {{H5AMOREEvent::kSignal, &fTriggeredCrystals, {}},
                         {H5AMOREEvent::kPedestal, &fPedestalCrystals, {}}};
  for (auto & b : builders) b.data.reserve(8);

  double perror = 0.0;
  double integral = 0.0;

  fWriteStatus = RUNNING;

  auto flushEvent = [&](Builder & b) -> bool {
    if (!b.has) return true;
    EventInfo_t info{};
    info.tnum  = static_cast<unsigned int>(b.nbuilt);
    info.ttime = b.start;
    info.ttype = (b.group == H5AMOREEvent::kPedestal) ? 1 : 0;  // CUPDAQ: 1 = pedestal
    info.nhit  = static_cast<decltype(info.nhit)>(b.data.size());

    herr_t status = h5event->AppendEvent(info, b.data, b.group);
    if (status < 0) {
      ERROR("H5AMOREEvent::AppendEvent failed (%s tnum=%u, nhit=%u)",
            H5AMOREEvent::GroupPath(b.group), info.tnum, info.nhit);
      RUNSTATE::SetError(fRunStatus);
      fWriteStatus = PROCSTATE::ERROR;
      return false;
    }

    ++b.nbuilt;
    if (b.group == H5AMOREEvent::kSignal) ++fNBuiltEvent;
    b.data.clear();
    b.has = false;
    return true;
  };

  // Add one crystal to its builder; returns false on write error.
  auto addCrystal = [&](Builder & b, Crystal_t && crystal) -> bool {
    if (b.has) {
      // Crystals from different ADC threads need not arrive in time order:
      // compare the absolute time difference with the coincidence window.
      const unsigned long dt = crystal.ttime > b.start ? crystal.ttime - b.start
                                                       : b.start - crystal.ttime;
      if (dt <= cw) {
        // Same crystal and time from another (legacy) path: merge trigger bits
        for (auto & existing : b.data) {
          if (existing.id == crystal.id && existing.ttime == crystal.ttime) {
            existing.trgbit |= crystal.trgbit;
            return true;
          }
        }
        b.start = std::min(b.start, static_cast<unsigned long>(crystal.ttime));
        b.data.push_back(std::move(crystal));
        return true;
      }
      if (!flushEvent(b)) return false;
    }
    b.start = crystal.ttime;
    b.has = true;
    b.data.push_back(std::move(crystal));
    return true;
  };

  bool failed = false;
  while (!failed) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    if (fDoSplitOutputFile) {
      for (auto & b : builders)
        if (!flushEvent(b)) failed = true;
      if (failed) break;
      h5event->Flush();
      fSubRunNumber += 1;
      char newname[512];
      std::snprintf(newname, sizeof(newname), "%s.%05d", fOutputFilename.c_str(), fSubRunNumber.load());
      if (OpenNewHDF5File(newname) < 0) {
        ERROR("can't create split hdf5 output file %s", newname);
        RUNSTATE::SetError(fRunStatus);
        fWriteStatus = PROCSTATE::ERROR;
        break;
      }
      fDoSplitOutputFile = false;
    }

    bool gotAny = false;
    for (auto & b : builders) {
      auto popped = b.queue->pop_front();
      if (!popped) continue;
      gotAny = true;
      if (!addCrystal(b, std::move(popped.value()))) { failed = true; break; }
    }
    if (failed) break;

    if (!gotAny) {
      // Queues empty — check if all triggers are done
      if (!HasRunningTrigger() && fTriggeredCrystals.size() == 0 && fPedestalCrystals.size() == 0) {
        for (auto & b : builders) flushEvent(b);  // last in-progress events
        break;
      }
      h5event->Flush();
      ThreadSleep(fWriteSleep, perror, integral, 0);
      continue;
    }

    ThreadSleep(fWriteSleep, perror, integral,
                fTriggeredCrystals.size() + fPedestalCrystals.size());
  }

  if (fHDF5File) {
    fHDF5File->Close();
  }

  delete h5event;
  fH5Event = nullptr;

  if (fWriteStatus != PROCSTATE::ERROR) {
    fWriteStatus = ENDED;
  }

  INFO("Writing events ended. Signal events: %lld, pedestal events: %llu", fNBuiltEvent,
       static_cast<unsigned long long>(builders[1].nbuilt));
}

long AMOREDAQManager::OpenNewHDF5File(const char * filename)
{
  long retval = 0;

  std::string filepath(filename);

  std::size_t slash_pos = filepath.find_last_of("/\\");
  std::string bname = (slash_pos == std::string::npos) ? filepath : filepath.substr(slash_pos + 1);

  int subnum = 0;
  std::size_t dot_pos = bname.find_last_of('.');
  if (dot_pos != std::string::npos && dot_pos + 1 < bname.length()) {
    subnum = std::atoi(bname.substr(dot_pos + 1).c_str());
  }

  if (subnum == 0) {
    namespace fs = std::filesystem;
    fs::path parent = fs::path(filename).parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
      fs::create_directories(parent);
      INFO("created output directory %s", parent.c_str());
    }
    fHDF5File = new H5DataWriter(filename, fCompressionLevel);
    fHDF5File->SetSubrun(0);
  }
  else {
    retval = fHDF5File->GetFileSize();
    fHDF5File->Close();
    delete fHDF5File;

    fHDF5File = new H5DataWriter(filename, fCompressionLevel);
    fHDF5File->SetSubrun(subnum);
    fHDF5File->SetData(fH5Event);

    if (!fHDF5File->Open()) {
      ERROR("can't open output file %s", filename);
      return -1;
    }
  }

  INFO("%s opened", filename);
  return retval;
}
