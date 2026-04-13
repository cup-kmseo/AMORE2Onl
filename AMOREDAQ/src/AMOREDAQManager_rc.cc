#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>

#include "DAQUtils/ELog.hh"

#include "AMOREDAQ/AMOREDAQManager.hh"

void AMOREDAQManager::Run()
{
  if (std::filesystem::exists(kFORCEDENDRUNFILE)) { std::filesystem::remove(kFORCEDENDRUNFILE); }

  if (!ReadConfig()) return;

  RC_AMOREDAQ();
}

void AMOREDAQManager::RC_AMORETCB()
{
  int state = 0;
  INFO("AMORETCB controller now starting [run=%d]", fRunNumber);
}

void AMOREDAQManager::RC_AMOREDAQ()
{
  INFO("amoredaq now starting [run=%d]", fRunNumber);

  std::thread th1;
  std::thread th2;
  std::thread th_swt[8];
  std::thread th3;

  fTCB.SetConfig(fConfigList);

  if (fTCB.Open() != 0) return;
  if (!fTCB.Config()) return;
  if (!AddADC(fConfigList)) return;
  if (!PrepareDAQ()) return;
  if (!OpenDAQ()) return;

  th1 = std::thread(&AMOREDAQManager::TF_ReadData_AMORE, this);
  th2 = std::thread(&AMOREDAQManager::TF_StreamData, this);
  th3 = std::thread(&AMOREDAQManager::TF_WriteEvent_AMORE, this);

  int nadc = GetEntries();
  INFO("RC_AMOREDAQ: nadc=%d", nadc);
  for (int i = 0; i < nadc; ++i) {
    th_swt[i] = std::thread(&AMOREDAQManager::TF_SWTrigger, this, i);
  }

  // sleep for 1 secs
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  fTCB.TriggerStart();
  RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNNING);

  // sleep for set DAQ time, printing status every 10 seconds
  const int kStatusInterval = 10;
  int elapsed = 0;
  auto runStart = std::chrono::steady_clock::now();
  while (elapsed < fSetDAQTime) {
    int remaining = fSetDAQTime - elapsed;
    int sleepSec = (remaining < kStatusInterval) ? remaining : kStatusInterval;
    std::this_thread::sleep_for(std::chrono::seconds(sleepSec));
    elapsed += sleepSec;

    unsigned int ntrg;
    {
      std::lock_guard<std::mutex> lock(fMonitorMutex);
      ntrg = fTriggerNumber;
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
    double rate = dt > 0.0 ? ntrg / dt : 0.0;

    char tbuf[32];
    std::time_t now = std::time(nullptr);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&now));
    std::cout << Form("[%s] elapsed: %3d s  trigger rate: %.2f Hz", tbuf, elapsed, rate)
              << std::endl;
  }
  fTCB.TriggerStop();
  RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNENDED);

  th1.join();
  th2.join();
  for (int i = 0; i < nadc; ++i) {
    th_swt[i].join();
  }
  th3.join();

  CloseDAQ();
  fTCB.Close();

  PrintDAQSummary();
  INFO("amoredaq ended");
}

void AMOREDAQManager::PrintDAQSummary()
{
  unsigned long totalReadDataSize;
  double liveTime;

  const std::size_t nadc = GetEntries();
  if (nadc > 0) {
    auto * theADC = static_cast<AbsADC *>(fCont[0]);
    totalReadDataSize = static_cast<unsigned long>(nadc * theADC->GetTotalBCount() * kKILOBYTES);
    liveTime = theADC->GetCurrentTime() / kDONESECOND;
  }
  else {
    totalReadDataSize = fTotalRawDataSize;
    liveTime = std::difftime(fEndDatime, fStartDatime);
  }

  double recvDataSize = totalReadDataSize / kDGIGABYTES;
  double outputDataSize = fTotalWrittenDataSize / kDGIGABYTES;

  double trate = liveTime > 0.0 ? fTriggerNumber / liveTime : 0.0;
  double drate = liveTime > 0.0 ? recvDataSize * 1024.0 / liveTime : 0.0;
  double orate = liveTime > 0.0 ? outputDataSize * 1024.0 / liveTime : 0.0;

  std::cout << std::endl;
  std::cout << "************************* DAQ Summary *************************" << std::endl;
  std::cout << Form("%32s", "Run number : ") << fRunNumber << std::endl;
  std::cout << Form("%32s", "Start Time : ") << TDatime(fStartDatime).AsSQLString() << std::endl;
  std::cout << Form("%32s", "End Time : ") << TDatime(fEndDatime).AsSQLString() << std::endl;
  std::cout << std::endl;
  std::cout << Form("%32s", "Live time : ") << Form("%.1f", liveTime) << " [s]" << std::endl;
  std::cout << Form("%32s", "Number of ADC : ") << nadc << std::endl;
  std::cout << Form("%32s", "Total number of trigger : ") << Form("%d", fTriggerNumber)
            << std::endl;
  std::cout << Form("%32s", "Trigger rate : ") << Form("%.2f", trate) << " [Hz]" << std::endl;
  std::cout << Form("%32s", "Total number of event : ") << Form("%d", fNBuiltEvent) << std::endl;
  std::cout << std::endl;
  std::cout << Form("%32s", "Received data size : ")
            << Form("%.3f GBytes (%.3f MB/sec)", recvDataSize, drate) << std::endl;
  std::cout << Form("%32s", "Written data size : ")
            << Form("%.3f GBytes (%.3f MB/sec)", outputDataSize, orate) << std::endl;
  std::cout << "***************************************************************" << std::endl;
  std::cout << std::endl;
}
