#include "TRandom3.h"

#include "AMOREDAQ/AMOREDAQManager.hh"
#include "AMOREHDF5/H5AMOREEvent.hh"

void AMOREDAQManager::TF_ReadData_AMORE()
{
  fReadStatus = READY;

  if (!ThreadWait(fRunStatus, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  const int nadc = GetEntries();

  INFO("Reading data from ADCs started.");

  auto * adc0 = static_cast<AbsADC *>(fCont[0]);
  std::vector<int> currentBCounts(nadc);

  double sleepError = 0.0;
  double sleepIntegral = 0.0;
  const int kTargetBlocks = 4;

  bool isFlushingData = false;

  fReadStatus = RUNNING;

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) { break; }

    if (RUNSTATE::CheckState(fRunStatus, RUNSTATE::kRUNENDED) && !isFlushingData) {
      INFO("Run ended. Waiting 1s for remaining data...");
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      isFlushingData = true;
    }

    int minBCount = ReadBCountMin(currentBCounts.data());

    if (minBCount < 0) {
      ERROR("Failed to read buffer count");
      RUNSTATE::SetError(fRunStatus);
      fReadStatus = ERROR;
      break;
    }

    if (isFlushingData && minBCount < fMinimumBCount) {
      INFO("No more data [minBCount=%d]. Stop.", minBCount);
      break;
    }

    int nBlocksProcessed = 0;
    if (minBCount >= fMinimumBCount) {
      for (int i = 0; i < nadc; ++i) {
        if (ReadADCData(i, fMinimumBCount) < 0) {
          ERROR("Reading failed at ADC #%d", i);
          RUNSTATE::SetError(fRunStatus);
          fReadStatus = ERROR;
          break;
        }
      }

      if (fReadStatus == ERROR || RUNSTATE::CheckError(fRunStatus)) { break; }

      {
        std::lock_guard<std::mutex> lock(fMonitorMutex);
        fTriggerNumber = adc0->GetCurrentTrgNumber();
        fCurrentTime = adc0->GetCurrentTime();
        fTriggerTime = fCurrentTime;
        for (std::size_t i = 0; i < nadc; ++i) {
          fRemainingBCount[i] = currentBCounts[i] - fMinimumBCount;
        }
      }

      fTotalReadDataSize += static_cast<double>(nadc) * fMinimumBCount * kKILOBYTES;
      nBlocksProcessed = 1;
    }

    int nAvailableBlocks = (minBCount / fMinimumBCount) - nBlocksProcessed;
    ThreadSleep(fReadSleep, sleepError, sleepIntegral, nAvailableBlocks, kTargetBlocks);
  }

  if (fReadStatus != ERROR) { fReadStatus = ENDED; }
  INFO("Reading data from ADCs ended");
}

void AMOREDAQManager::TF_StreamData()
{
  fStreamStatus = READY;

  if (!ThreadWait(fRunStatus, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  const int nadc = GetEntries();

  double sleepError = 0.0;
  double sleepIntegral = 0.0;
  const int kTargetChunk = 4;

  INFO("Streaming data started.");
  fStreamStatus = RUNNING;

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) { break; }

    if (fReadStatus == ENDED) {
      int remain = 0;
      for (int i = 0; i < nadc; ++i) {
        auto * adc = static_cast<AbsADC *>(fCont[i]);
        remain += adc->Bsize();
      }
      if (remain == 0) { break; }
    }

    int nTotalChunkinADC = 0;
    for (int i = 0; i < nadc; ++i) {
      auto * adc = static_cast<AbsADC *>(fCont[i]);
      auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());

      nTotalChunkinADC += adc->Bsize();

      auto chunkdata = adc->Bpop_front();
      if (!chunkdata) { continue; }

      unsigned char * data = chunkdata->data;
      int ndp = kKILOBYTES * chunkdata->size / 64;

      fFIFOs[i]->PushChunk(data, ndp, conf);

      nTotalChunkinADC -= 1;
    }

    int nAvailableChunk = nTotalChunkinADC / nadc;
    ThreadSleep(fReadSleep, sleepError, sleepIntegral, nAvailableChunk, kTargetChunk);
  }

  for (int i = 0; i < nadc; ++i) {
    fFIFOs[i]->Stop();
  }

  fStreamStatus = ENDED;
  INFO("Streaming data ended");
}

void AMOREDAQManager::TF_SWTrigger(int n)
{
  fTrigStatus[n] = READY;

  if (!ThreadWait(fRunStatus, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  auto *adc  = static_cast<AbsADC *>(fCont[n]);
  auto *conf = static_cast<AMOREADCConf *>(adc->GetConfig());

  const int ndp  = conf->RL();
  const int tail = ndp - conf->DLY();

  INFO("software trigger for AMOREADC[sid=%d] started.", conf->SID());
  fTrigStatus[n] = RUNNING;

  auto &fifo = fFIFOs[n];

  const int nch = kNCHAMOREADC;
  std::vector<unsigned short> adcval(nch);
  unsigned long currenttime = 0;
  unsigned long lasttime = 0;

  bool isFirstSample = true;

  std::vector<unsigned long> dumpTime(ndp);

  std::vector<std::vector<unsigned short>> dumpStorage(
      nch, std::vector<unsigned short>(ndp));
  std::vector<unsigned short*> dumpADC(nch);
  for (int i = 0; i < nch; ++i)
    dumpADC[i] = dumpStorage[i].data();

  std::vector<std::uint16_t> phonon(ndp);
  std::vector<std::uint16_t> photon(ndp);

  std::vector<int> ndt(nch, 0);
  std::vector<bool> istriggered(nch, false);
  std::vector<bool> pendingTrigger(nch, false);
  std::vector<int> pendingCount(nch, 0);

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;
    if (fStreamStatus == ENDED && fifo->Empty()) break;

    int stat = fifo->PopCurrent(adcval.data(), currenttime);
    if (stat == 0) {
      if (!isFirstSample) {
        unsigned long delta = currenttime - lasttime;
        if (delta != fTimeDelta) {
          if (delta < fTimeDelta) {
            WARNING("[NS ERROR] Overlap/Jitter! Last: %lu Now: %lu Gap: %lu ns",
                    lasttime, currenttime, delta);
          }
          else {
            unsigned long lost = (delta / fTimeDelta) - 1;
            if (lost != 0) {
              WARNING("[NS ERROR] missing samples! Last: %lu Now: %lu Gap: %lu ns | Lost: %lu",
                      lasttime, currenttime, delta, lost);
            }
          }
        }

        for (int i = 0; i < nch; ++i) {
          if (!conf->TRGON(i)) continue;

          if (istriggered[i]) {
            ndt[i] += 1;
            if (ndt[i] > tail + conf->DT(i)) {
              istriggered[i] = false;
              ndt[i] = 0;
            }
            continue;
          }

          if (pendingTrigger[i]) {
            pendingCount[i] += 1;

            if (pendingCount[i] >= tail) {
              if (i + 1 >= nch) {
                WARNING("Channel %02d [sid=%d] has no paired channel for dump",
                        i, adc->GetSID());
                pendingTrigger[i] = false;
                pendingCount[i] = 0;
                continue;
              }

              int copied = fifo->DumpCurrent(dumpADC.data(), dumpTime.data());

              if (copied != ndp) {
                WARNING("Channel %02d [sid=%d] incomplete dump: %d/%d",
                        i, adc->GetSID(), copied, ndp);
              }
              else {
                int pid = conf->PID(i);

                Crystal_t xtal;
                xtal.ndp   = static_cast<unsigned int>(ndp);
                xtal.id    = pid / 2;
                xtal.ttime = currenttime;

                for (int j = 0; j < ndp; ++j) {
                  phonon[j] = static_cast<std::uint16_t>(dumpADC[i][j]);
                  photon[j] = static_cast<std::uint16_t>(dumpADC[i + 1][j]);
                }

                xtal.SetWaveforms(phonon.data(), photon.data(), ndp);
                fTriggeredCrystals.push_back(xtal);

                INFO("Channel %02d [sid=%d] event confirmed after %d samples",
                     i, adc->GetSID(), pendingCount[i]);

                istriggered[i] = true;
                ndt[i] = 0;
              }

              pendingTrigger[i] = false;
              pendingCount[i] = 0;
            }

            continue;
          }

          if (gRandom->Rndm() < 1e-06) {
            INFO("Channel %02d [sid=%d] is triggered", i, adc->GetSID());
            pendingTrigger[i] = true;
            pendingCount[i] = 0;
          }
        }
      }
      else {
        isFirstSample = false;
      }

      lasttime = currenttime;
    }
    else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  INFO("software trigger for AMOREADC[sid=%d] ended.", conf->SID());
  fTrigStatus[n] = ENDED;

  fifo->DumpStat();
}

bool AMOREDAQManager::HasRunningTrigger() const
{
  for (const auto & st : fTrigStatus) {
    if (st == READY || st == RUNNING) return true;
  }
  return false;
}

void AMOREDAQManager::TF_WriteEvent_AMORE()
{
  if (!ThreadWait(fRunStatus, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  auto * adc0 = static_cast<AbsADC *>(fCont[0]);
  auto * conf0 = static_cast<AMOREADCConf *>(adc0->GetConfig());
  const int ndp = conf0->RL();

  if (ndp <= 0 || ndp > kH5AMORENDPMAX) {
    ERROR("invalid ndp: %d (max %d)", ndp, kH5AMORENDPMAX);
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = ERROR;
    return;
  }

  auto * h5event = new H5AMOREEvent;
  h5event->SetNDP(ndp);
  fH5Event = h5event;

  if (OpenNewHDF5File(fOutputFilename.c_str()) < 0) {
    ERROR("can't open hdf5 output file %s", fOutputFilename.c_str());
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = ERROR;
    return;
  }

  EventInfo_t eventinfo{};
  std::vector<Crystal_t> eventdata;
  eventdata.reserve(1);

  double perror = 0.0;
  double integral = 0.0;

  fWriteStatus = RUNNING;

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    if (fTriggeredCrystals.empty()) {
      if (!HasRunningTrigger()) break;
      ThreadSleep(fWriteSleep, perror, integral, fTriggeredCrystals.size());
      continue;
    }

    eventdata.clear();

    auto popped = fTriggeredCrystals.pop_front();
    if (!popped) {
      ThreadSleep(fWriteSleep, perror, integral, fTriggeredCrystals.size());
      continue;
    }

    Crystal_t crystal = std::move(popped.value());

    if (crystal.ndp == 0) {
      crystal.ndp = static_cast<std::uint16_t>(ndp);
    }

    if (crystal.ndp != static_cast<std::uint16_t>(ndp)) {
      ERROR("crystal ndp mismatch: %u != %d", crystal.ndp, ndp);
      RUNSTATE::SetError(fRunStatus);
      fWriteStatus = ERROR;
      break;
    }

    eventdata.push_back(std::move(crystal));

    eventinfo.tnum  = 0;
    eventinfo.ttime = eventdata[0].ttime;
    eventinfo.ttype = 0;
    eventinfo.nhit  = static_cast<decltype(eventinfo.nhit)>(eventdata.size());

    herr_t status = h5event->AppendEvent(eventinfo, eventdata);
    if (status < 0) {
      ERROR("H5AMOREEvent::AppendEvent failed");
      RUNSTATE::SetError(fRunStatus);
      fWriteStatus = ERROR;
      break;
    }

    ++fNBuiltEvent;

    ThreadSleep(fWriteSleep, perror, integral, fTriggeredCrystals.size());
  }

  if (fHDF5File) {
    fHDF5File->Close();
  }

  if (fWriteStatus != ERROR) {
    fWriteStatus = ENDED;
  }

  INFO("Writing events ended");
}