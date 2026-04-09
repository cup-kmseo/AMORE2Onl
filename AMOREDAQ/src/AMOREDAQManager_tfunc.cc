#include <cstdint>
#include <thread>

#include "DAQUtils/ELog.hh"

#include "AMOREHDF5/AMOREEDM.hh"
#include "AMOREDAQ/AMOREDAQManager.hh"

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
      fReadStatus = PROCSTATE::ERROR;
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
          fReadStatus = PROCSTATE::ERROR;
          break;
        }
      }

      if (fReadStatus == PROCSTATE::ERROR || RUNSTATE::CheckError(fRunStatus)) { break; }

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

  if (fReadStatus != PROCSTATE::ERROR) { fReadStatus = ENDED; }
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

      fTriggers[i]->PushChunkData(data, ndp);

      nTotalChunkinADC -= 1;
    }

    int nAvailableChunk = nTotalChunkinADC / nadc;
    ThreadSleep(fReadSleep, sleepError, sleepIntegral, nAvailableChunk, kTargetChunk);
  }

  for (int i = 0; i < nadc; ++i) {
    fTriggers[i]->Stop();
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

  auto * adc  = static_cast<AbsADC *>(fCont[n]);
  auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());
  const int ndp = conf->RL();
  const int nch = kNCHAMOREADC;

  INFO("software trigger for AMOREADC[sid=%d] started.", conf->SID());
  fTrigStatus[n] = RUNNING;

  auto & trigger = fTriggers[n];

  // Dump buffers: one row per channel, ndp samples per row
  std::vector<std::vector<unsigned short>> dumpStorage(nch, std::vector<unsigned short>(ndp));
  std::vector<unsigned short *> dumpADC(nch);
  for (int i = 0; i < nch; ++i) dumpADC[i] = dumpStorage[i].data();

  std::vector<unsigned long> dumpTime(ndp);
  bool trgbit[kNCHAMOREADC]{};
  unsigned long trgtime = 0;

  std::vector<std::uint16_t> phonon(ndp);
  std::vector<std::uint16_t> photon(ndp);

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    int ret = trigger->DoTrigger(trgtime, trgbit, dumpADC.data(), dumpTime.data());

    if (ret == 1) {
      // Package Crystal_t for each triggered phonon channel (even index)
      for (int i = 0; i < nch; i += 2) {
        if (!trgbit[i]) continue;
        if (i + 1 >= nch) {
          WARNING("Channel %02d [sid=%d] has no paired photon channel", i, adc->GetSID());
          continue;
        }

        Crystal_t xtal;
        xtal.ndp   = static_cast<std::uint16_t>(ndp);
        xtal.id    = static_cast<std::uint16_t>(conf->PID(i) / 2);
        xtal.ttime = trgtime;

        for (int j = 0; j < ndp; ++j) {
          phonon[j] = dumpADC[i][j];
          photon[j] = dumpADC[i + 1][j];
        }

        xtal.SetWaveforms(phonon.data(), photon.data(), ndp);
        fTriggeredCrystals.push_back(xtal);

        INFO("Crystal id=%d [sid=%d] triggered at t=%lu", xtal.id, adc->GetSID(), trgtime);
      }
    }
    else if (ret == 0) {
      // FIFO empty — check if streaming is done
      if (fStreamStatus == ENDED && trigger->IsFIFOEmpty()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    else {
      // ret < 0: FIFO error or stopped
      break;
    }
  }

  INFO("software trigger for AMOREADC[sid=%d] ended.", conf->SID());
  fTrigStatus[n] = ENDED;
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

  if (OpenNewHDF5File(fOutputFilename.c_str()) < 0) {
    ERROR("can't create hdf5 output file %s", fOutputFilename.c_str());
    RUNSTATE::SetError(fRunStatus);
    fWriteStatus = PROCSTATE::ERROR;
    return;
  }

  WriteAMORE_HDF5();
}