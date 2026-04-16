#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "AMOREDAQ/AMOREDAQManager.hh"
#include "AMORESystem/AMOREADC.hh"
#include "AMORESystem/AMOREADCConf.hh"
#include "AMORESystem/AMORETCBConf.hh"
#include "DAQConfig/DAQConf.hh"
#include "DAQUtils/ELog.hh"
#include "OnlConsts/adcconsts.hh"

ClassImp(AMOREDAQManager)

AMOREDAQManager::AMOREDAQManager()
  : CupDAQManager(),
    fTCB(AMORETCB::Instance())
{
  fADCType = ADC::AMOREADC; // only AMOREADC will be added, this is amoredaq
  fConfigList = new AbsConfList();
}

AMOREDAQManager::~AMOREDAQManager() { delete fConfigList; }

bool AMOREDAQManager::AddADC(AbsConfList * conflist)
{
  int nadc = conflist->GetNADC(fADCType);
  if (nadc == 0) {
    ERROR("there is no AMOREADC");
    return false;
  }

  for (int i = 0; i < nadc; i++) {
    AbsConf * conf = conflist->GetConfig(fADCType, i);
    if (!conf->IsEnabled()) continue;
    if (!conf->IsLinked()) {
      ERROR("AMOREADC[sid=%2d] enabled but not linked", conf->SID());
      return false;
    }
    if (conf->GetDAQID() == fDAQID) {
      auto * adc = new AMOREADC(conf);
      Add(adc);
      INFO("AMOREADC[sid=%2d] added to DAQ manager", adc->GetSID());
    }
  }

  return true;
}

bool AMOREDAQManager::ReadConfig()
{
  if (fConfigFilename.empty()) {
    ERROR("config filename is empty");
    return false;
  }

  std::string filename = fConfigFilename;

  try {
    YAML::Node node = YAML::LoadFile(filename.c_str());
    if (node.IsNull()) {
      ERROR("config file is empty");
      return false;
    }

    if (node["Include"] && node["Include"].IsSequence()) {
      for (const auto & inc : node["Include"]) {
        std::string inc_file = inc.as<std::string>();
        try {
          YAML::Node inc_node = YAML::LoadFile(inc_file.c_str());

          ReadConfigTCB(inc_node);
          ReadConfigADC(inc_node);
          ReadConfigDAQ(inc_node);

          INFO("Included config %s is successfully loaded", inc_file.c_str());
        }
        catch (const std::exception & e) {
          ERROR("Failed to load included file %s: %s", inc_file.c_str(), e.what());
          return false;
        }
      }
    }

    ReadConfigTCB(node);
    ReadConfigADC(node);
    ReadConfigDAQ(node);

    INFO("reading config %s is done", filename.c_str());

    return true;
  }
  catch (const YAML::BadFile & e) {
    ERROR("file not found, %s", filename.c_str());
  }
  catch (const YAML::ParserException & e) {
    ERROR("syntax error (%s) at line %d, col %d of config file", e.msg.c_str(), e.mark.line + 1,
          e.mark.column + 1);
  }
  catch (const std::exception & e) {
    const char * err_msg = e.what();
    ERROR("unknown error(%s) on reading config file", err_msg ? err_msg : "Unknown");
  }

  return false;
}

template <typename T>
void AMOREDAQManager::FillConfigArray(YAML::Node node, int nch, std::function<void(int, T)> setter,
                                      bool inc)
{
  if (!node) return;

  std::vector<T> val;
  if (node.IsScalar()) { val.push_back(node.as<T>()); }
  else {
    try {
      val = node.as<std::vector<T>>();
    }
    catch (...) {
      return;
    }
  }

  int valsize = val.size();
  if (valsize == 0) return;

  for (int i = 0; i < nch; ++i) {
    T target;
    if (i < valsize) { target = val[i]; }
    else {
      if (inc) { target = val[valsize - 1] + (i - (valsize - 1)); }
      else {
        target = val[valsize - 1];
      }
    }
    setter(i, target);
  }
}

void AMOREDAQManager::ReadConfigTCB(YAML::Node ymlnode)
{
  if (!ymlnode["AMORETCB"]) return;

  auto * conf = new AMORETCBConf(0);
  auto tcb = ymlnode["AMORETCB"];

  if (tcb["ID"]) conf->SetDAQID(tcb["ID"].as<int>());
  if (tcb["CW"]) conf->SetCW(tcb["CW"].as<int>());
  if (tcb["DT"]) conf->SetDT(tcb["DT"].as<int>());
  if (tcb["PSC"]) conf->SetPSC(tcb["PSC"].as<int>());

  fConfigList->Add(conf);
}

void AMOREDAQManager::ReadConfigADC(YAML::Node ymlnode)
{
  if (!ymlnode["AMOREADC"]) return;

  std::vector<YAML::Node> nodes;
  if (ymlnode["AMOREADC"].IsSequence()) {
    for (const auto & n : ymlnode["AMOREADC"])
      nodes.push_back(n);
  }
  else {
    nodes.push_back(ymlnode["AMOREADC"]);
  }

  for (auto & node : nodes) {
    int nch = 0;

    auto * conf = new AMOREADCConf();
    conf->SetName("AMOREADC");
    conf->SetADCType(ADC::AMOREADC);

    if (node["ENABLED"] && node["ENABLED"].as<int>()) { conf->SetEnable(); }

    if (node["DAQID"]) conf->SetDAQID(node["DAQID"].as<int>());

    if (node["SID"]) {
      int sid = node["SID"].as<int>();
      conf->SetSID(sid);
      conf->SetMID(sid + 128);
    }
    if (node["NCH"]) {
      nch = node["NCH"].as<int>();
      conf->SetNCH(nch);
    }

    if (node["SR"]) conf->SetSR(node["SR"].as<int>());
    if (node["RL"]) conf->SetRL(node["RL"].as<int>());
    if (node["DLY"]) conf->SetDLY(node["DLY"].as<int>());
    if (node["ZSU"]) conf->SetZSU(node["ZSU"].as<int>());
    if (node["PTRG"]) conf->SetPTRG(node["PTRG"].as<int>());
    if (node["TRGMODE"]) {
      if (node["TRGMODE"].IsSequence()) {
        for (const auto & m : node["TRGMODE"])
          conf->AddTRGMODE(m.as<std::string>().c_str());
      } else {
        conf->SetTRGMODE(node["TRGMODE"].as<std::string>().c_str());
      }
    }

    if (nch > 0) {
      FillConfigArray<int>(node["CID"], nch, [&](int i, int v) { conf->SetCID(i, v); }, true);
      FillConfigArray<int>(node["PID"], nch, [&](int i, int v) { conf->SetPID(i, v); }, true);
      FillConfigArray<int>(node["TRGON"], nch, [&](int i, int v) { conf->SetTRGON(i, v); });
      FillConfigArray<int>(node["DT"], nch, [&](int i, int v) { conf->SetDT(i, v); });
      FillConfigArray<int>(node["THR"], nch, [&](int i, int v) { conf->SetTHR(i, v); });
      FillConfigArray<int>(node["SLOPE_LB"], nch, [&](int i, int v) { conf->SetSlopeLB(i, v); });
      FillConfigArray<int>(node["SLOPE_DT"], nch, [&](int i, int v) { conf->SetSlopeDT(i, v); });
    }

    fConfigList->Add(conf);
  }
}

bool AMOREDAQManager::PrepareDAQ()
{
  const int nadc = GetEntries();

  if (nadc <= 0) {
    ERROR("No ADC module included in the configuration");
    fReadStatus = PROCSTATE::ERROR;
    RUNSTATE::SetError(fRunStatus);
    return false;
  }

  // preparing software triggers via TriggerManager
  if (!fTriggerManager.BuildTriggers(fConfigList)) {
    ERROR("TriggerManager::BuildTriggers failed");
    return false;
  }
  if (!fTriggerManager.PrepareAll()) {
    ERROR("TriggerManager::PrepareAll failed");
    return false;
  }

  int dsr = 0;
  int rl = 0;
  for (int i = 0; i < nadc; ++i) {
    auto * adc = static_cast<AbsADC *>(fCont[i]);
    auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());
    dsr = conf->SR();
    rl = conf->RL();
  }

  // sorting ADCs with SID
  Sort();

  fMinimumBCount = AMORE::kMINIMUMBCOUNT;
  fRecordLength = rl;

  // for time integrity check in swtringger
  fTimeDelta = dsr * 1000;

  std::string report = "\n\n";
  report += "============ AMOREDAQManager Preparation Report ==============\n";
  report += Form("                       type: %s\n", GetADCName(fADCType));
  report += Form("              number of ADC: %d\n", nadc);
  report += Form("      minimum buffer count : %d\n", fMinimumBCount);
  report += Form("             record length : %d\n", fRecordLength);
  report += "==============================================================\n";

  INFO("%s", report.c_str());

  // for Debug Monitoring
  fRemainingBCount = new int[nadc];
  for (int i = 0; i < nadc; i++) {
    fRemainingBCount[i] = 0;
  }

  INFO("prepared to take data from AMOREADC");

  return true;
}

void AMOREDAQManager::ReadConfigDAQ(YAML::Node ymlnode)
{
  if (!ymlnode["DAQ"]) return;

  // Only one DAQConf allowed — skip if already added
  if (fConfigList->GetDAQConfig()) return;

  auto * conf = new DAQConf();

  for (const auto & d : ymlnode["DAQ"]) {
    int id            = d["ID"]   ? d["ID"].as<int>()              : -1;
    std::string name  = d["Name"] ? d["Name"].as<std::string>()    : "UNKNOWN";
    std::string ip    = d["IP"]   ? d["IP"].as<std::string>()      : "127.0.0.1";
    int port          = d["Port"] ? d["Port"].as<int>()            : 9000;

    if (id < 0) {
      ERROR("ReadConfigDAQ: DAQ entry missing ID, skipped");
      continue;
    }
    conf->AddDAQ(id, name, ip, port);

    if (d["Output"]) {
      fDAQOutputs[id] = d["Output"].as<std::string>();
    }

    INFO("ReadConfigDAQ: id=%d name=%s ip=%s port=%d", id, name.c_str(), ip.c_str(), port);
  }

  fConfigList->Add(conf);
}

bool AMOREDAQManager::MeasurePedestal()
{
  const int    nadc       = GetEntries();
  const int    nch        = AMORE::kNCHPERADC;
  const double kDuration  = 1.0;  // seconds of data to collect
  const int    kNIter     = 3;    // sigma-clipping iterations
  const double kSigmaCut  = 5.0;  // rejection threshold

  INFO("Measuring pedestal (%.1f s, %d-iter %.0f-sigma clipping)...",
       kDuration, kNIter, kSigmaCut);

  // ---------------------------------------------------------------
  // Phase 1: collect raw samples per ADC per channel
  // ---------------------------------------------------------------
  // samples[adc][ch] holds all unsigned-short ADC values read
  using SampleVec = std::vector<unsigned short>;
  std::vector<std::vector<SampleVec>> samples(nadc, std::vector<SampleVec>(nch));

  auto tStart = std::chrono::steady_clock::now();

  while (true) {
    double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    if (elapsed >= kDuration) break;

    int minBCount = ReadBCountMin();
    if (minBCount < fMinimumBCount) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    for (int i = 0; i < nadc; ++i) {
      if (ReadADCData(i, fMinimumBCount) < 0) {
        ERROR("MeasurePedestal: ReadADCData failed at ADC #%d", i);
        return false;
      }

      auto * adc  = static_cast<AbsADC *>(fCont[i]);
      auto * conf = static_cast<AMOREADCConf *>(adc->GetConfig());

      while (true) {
        auto chunk = adc->Bpop_front();
        if (!chunk) break;

        int ndp = kKILOBYTES * chunk->size / 64;
        unsigned char * data = chunk->data;

        for (int j = 0; j < ndp; ++j) {
          const int offset = j * 64;
          for (int ch = 0; ch < nch; ++ch) {
            if (conf->ZSU() && conf->PID(ch) == 0) continue;
            unsigned int val = data[offset + ch * 3] & 0xFF;
            val |= (static_cast<unsigned int>(data[offset + ch * 3 + 1] & 0xFF) << 8);
            val |= (static_cast<unsigned int>(data[offset + ch * 3 + 2] & 0xFF) << 16);
            samples[i][ch].push_back(static_cast<unsigned short>(val));
          }
        }
      }
    }
  }

  // ---------------------------------------------------------------
  // Phase 2: iterative sigma clipping → baseline per channel
  // ---------------------------------------------------------------
  std::string report = "\n\n";
  report += "============ Pedestal Measurement ============\n";
  report += Form("  (%.0f-sigma clipping, %d iterations)\n", kSigmaCut, kNIter);

  for (int i = 0; i < nadc; ++i) {
    auto * adc = static_cast<AbsADC *>(fCont[i]);

    int baselines[nch];

    for (int ch = 0; ch < nch; ++ch) {
      const SampleVec & sv = samples[i][ch];

      if (sv.empty()) {
        baselines[ch] = 0;
        continue;
      }

      // valid[k] = true while sample sv[k] survives clipping
      std::vector<bool> valid(sv.size(), true);
      long nvalid = static_cast<long>(sv.size());

      for (int iter = 0; iter < kNIter; ++iter) {
        if (nvalid == 0) break;

        // compute mean of surviving samples
        double sum = 0.0;
        for (size_t k = 0; k < sv.size(); ++k)
          if (valid[k]) sum += sv[k];
        double mean = sum / nvalid;

        // compute std of surviving samples
        double sum2 = 0.0;
        for (size_t k = 0; k < sv.size(); ++k) {
          if (!valid[k]) continue;
          double d = sv[k] - mean;
          sum2 += d * d;
        }
        double sigma = std::sqrt(sum2 / nvalid);

        // clip samples beyond kSigmaCut * sigma
        double lo = mean - kSigmaCut * sigma;
        double hi = mean + kSigmaCut * sigma;
        for (size_t k = 0; k < sv.size(); ++k) {
          if (!valid[k]) continue;
          if (sv[k] < lo || sv[k] > hi) {
            valid[k] = false;
            --nvalid;
          }
        }
      }

      // final mean from surviving samples
      double sum = 0.0;
      for (size_t k = 0; k < sv.size(); ++k)
        if (valid[k]) sum += sv[k];
      baselines[ch] = (nvalid > 0)
        ? static_cast<int>(std::round(sum / nvalid))
        : 0;
    }

    fTriggerManager.SetBaselines(i, baselines, nch);

    // report: grid of 4 channels per row, show baseline and surviving fraction
    long total = static_cast<long>(samples[i][0].size());
    report += Form(" SID %2d  (total samples/ch: %ld)\n", adc->GetSID(), total);
    for (int row = 0; row < nch / 4; ++row) {
      report += "  ";
      for (int ch = row * 4; ch < (row + 1) * 4; ++ch)
        report += Form("ch%02d: %5d  ", ch, baselines[ch]);
      report += "\n";
    }
  }
  report += "==============================================\n";
  INFO("%s", report.c_str());

  // clear ADC buffers so TF_ReadData starts from fresh data
  for (int i = 0; i < nadc; ++i)
    static_cast<AbsADC *>(fCont[i])->Bclear();

  return true;
}
