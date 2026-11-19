#include <cstdint>
#include <memory>
#include <thread>

#include "ELog.hh"

#include "AMOREHDF5/AMOREEDM.hh"
#include "AMOREDAQ/AMOREDAQManager.hh"

void AMOREDAQManager::TF_ReadData_AMORE()
{
  fReadStatus = READY;

  if (!WaitRunState(fRunStatus, RUNSTATE::kRUNNING, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  const int nadc = GetNADC();

  INFO("Reading data from ADCs started.");

  auto * adc0 = fADCList[0].get();
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

  if (!WaitRunState(fRunStatus, RUNSTATE::kRUNNING, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  const int nadc = GetNADC();

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
        auto * adc = fADCList[i].get();
        remain += adc->Bsize();
      }
      if (remain == 0) { break; }
    }

    int nTotalChunkinADC = 0;
    for (int i = 0; i < nadc; ++i) {
      auto * adc = fADCList[i].get();
      auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());

      nTotalChunkinADC += adc->Bsize();

      auto chunkdata = adc->Bpop_front();
      if (!chunkdata) { continue; }

      unsigned char * data = chunkdata->data;
      int ndp = kKILOBYTES * chunkdata->size / 64;

      for (auto & trig : fTriggerManager.GetTriggers(i))
        trig->PushChunkData(data, ndp);

      nTotalChunkinADC -= 1;
    }

    int nAvailableChunk = nTotalChunkinADC / nadc;
    ThreadSleep(fReadSleep, sleepError, sleepIntegral, nAvailableChunk, kTargetChunk);
  }

  for (int i = 0; i < nadc; ++i) {
    for (auto & trig : fTriggerManager.GetTriggers(i))
      trig->Stop();
  }

  fStreamStatus = ENDED;
  INFO("Streaming data ended");
}

void AMOREDAQManager::TF_SWTrigger(int n)
{
  fTrigStatus[n] = READY;

  if (!WaitRunState(fRunStatus, RUNSTATE::kRUNNING, fDoExit)) {
    WARNING("Exited by exit command before starting");
    return;
  }

  auto * adc  = fADCList[n].get();
  auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());
  const int ndp = conf->RL();

  INFO("software trigger for AMOREADC[sid=%d] started.", conf->SID());
  fTrigStatus[n] = RUNNING;

  const auto & triggers = fTriggerManager.GetTriggers(n);
  if (triggers.empty()) {
    ERROR("No trigger assigned for ADC index %d", n);
    fTrigStatus[n] = PROCSTATE::ERROR;
    return;
  }
  const int ntrg = static_cast<int>(triggers.size());

  std::vector<XtalTrigger> recs;
  recs.reserve(AMORE::kNXTALPERADC);

  // Crystal_t carries 2 x kH5AMORENDPMAX waveforms (~2 MB): keep it off the stack.
  auto xtal = std::make_unique<Crystal_t>();

  // Convert finalised crystal triggers of path t to Crystal_t and queue them.
  auto queueRecords = [&](int t) {
    for (auto & rec : recs) {
      const int ch = 2 * rec.xtal;  // phonon channel of this crystal

      xtal->ndp   = static_cast<std::uint16_t>(ndp);
      xtal->id    = static_cast<std::uint16_t>(conf->PID(ch) / 2);
      xtal->ttime = rec.ttime;
      // Signal path: trgbit from its conditions. Legacy per-channel paths (removed
      // later) keep the old "bit t = path t" marking.
      xtal->trgbit = rec.trgbit ? rec.trgbit : (1u << t);
      xtal->SetWaveforms(rec.phonon.data(), rec.photon.data(), ndp);
      xtal->npulse = static_cast<std::uint16_t>(triggers[t]->CountPulses(ch, rec.phonon.data(), ndp));
      fTriggeredCrystals.push_back(*xtal);

      std::lock_guard<std::mutex> lock(fMonitorMutex);
      ++fTriggerNumber;
    }
    recs.clear();
  };

  while (true) {
    if (fDoExit || RUNSTATE::CheckError(fRunStatus)) break;

    bool anyData = false;
    bool allEmpty = true;

    for (int t = 0; t < ntrg; ++t) {
      const int ret = triggers[t]->DoTriggerXtal(recs);

      if (ret > 0) {
        anyData = true;
        allEmpty = false;
        queueRecords(t);
      }
      else if (ret == 0) {
        if (!(fStreamStatus == ENDED && triggers[t]->IsFIFOEmpty())) allEmpty = false;
      }
      else {
        // ret < 0: FIFO error or stopped — treat this path as done
      }
    }

    if (!anyData) {
      if (allEmpty) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // Crystals still inside their dead time at the end of the run.
  for (int t = 0; t < ntrg; ++t) {
    triggers[t]->FlushPending(recs);
    queueRecords(t);
  }

  INFO("software trigger for AMOREADC[sid=%d] ended.", conf->SID());
  fTrigStatus[n] = ENDED;
}

