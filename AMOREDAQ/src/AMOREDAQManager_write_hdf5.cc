#include <cstdlib>
#include <string>

#include "DAQUtils/ELog.hh"
#include "HDF5Utils/H5DataWriter.hh"

#include "AMOREHDF5/AMOREEDM.hh"
#include "AMOREHDF5/H5AMOREEvent.hh"
#include "AMORESystem/AMORETCBConf.hh"
#include "AMOREDAQ/AMOREDAQManager.hh"

void AMOREDAQManager::WriteAMORE_HDF5()
{
  auto * adc0 = static_cast<AbsADC *>(fCont[0]);
  auto * conf0 = static_cast<AMOREADCConf *>(adc0->GetConfig());
  const int ndp = conf0->RL();

  if (ndp <= 0 || ndp > kH5AMORENDPMAX) {
    ERROR("invalid ndp: %d (max %d)", ndp, kH5AMORENDPMAX);
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }

  // Read coincidence window from TCB config (CW is in samples, convert to ns)
  auto * tcbconf = static_cast<AMORETCBConf *>(fConfigList->GetTCBConfig());
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

  EventInfo_t eventinfo{};
  std::vector<Crystal_t> eventdata;
  eventdata.reserve(8);

  double perror = 0.0;
  double integral = 0.0;

  bool hasEvent = false;
  unsigned long eventStartTime = 0;

  fWriteStatus = RUNNING;

  auto flushEvent = [&]() -> bool {
    eventinfo.tnum  = static_cast<unsigned int>(fNBuiltEvent);
    eventinfo.ttime = eventStartTime;
    eventinfo.ttype = 0;
    eventinfo.nhit  = static_cast<decltype(eventinfo.nhit)>(eventdata.size());

    herr_t status = h5event->AppendEvent(eventinfo, eventdata);
    if (status < 0) {
      ERROR("H5AMOREEvent::AppendEvent failed (tnum=%u, nhit=%u)",
            eventinfo.tnum, eventinfo.nhit);
      RUNSTATE::SetError(fRunStatus);
      fWriteStatus = PROCSTATE::ERROR;
      return false;
    }

    ++fNBuiltEvent;
    eventdata.clear();
    hasEvent = false;
    return true;
  };

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    auto popped = fTriggeredCrystals.pop_front();

    if (!popped) {
      // Queue empty — check if all triggers are done
      if (!HasRunningTrigger()) {
        // Flush last in-progress event
        if (hasEvent && !flushEvent()) break;
        break;
      }
      ThreadSleep(fWriteSleep, perror, integral, 0);
      continue;
    }

    Crystal_t crystal = std::move(popped.value());

    if (!hasEvent) {
      // Start new event
      eventStartTime = crystal.ttime;
      hasEvent = true;
      eventdata.push_back(std::move(crystal));
    }
    else if (crystal.ttime - eventStartTime <= cw) {
      // Within coincidence window — add to current event
      eventdata.push_back(std::move(crystal));
    }
    else {
      // Outside CW — flush current event and start a new one
      if (!flushEvent()) break;

      eventStartTime = crystal.ttime;
      hasEvent = true;
      eventdata.push_back(std::move(crystal));
    }

    ThreadSleep(fWriteSleep, perror, integral, fTriggeredCrystals.size());
  }

  if (fHDF5File) {
    fHDF5File->Close();
  }

  delete h5event;
  fH5Event = nullptr;

  if (fWriteStatus != PROCSTATE::ERROR) {
    fWriteStatus = ENDED;
  }

  INFO("Writing events ended. Total events: %lld", fNBuiltEvent);
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
