#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>

#include "DAQUtils/ELog.hh"
#include "DAQConfig/DAQConf.hh"

#include "AMOREDAQ/AMOREDAQManager.hh"

// ============================================================
//  Run() — dispatch on DAQ type
// ============================================================
void AMOREDAQManager::Run()
{
  if (std::filesystem::exists(kFORCEDENDRUNFILE)) { std::filesystem::remove(kFORCEDENDRUNFILE); }

  if (!ReadConfig()) return;

  if (fDAQType == DAQ::AMORETCB) {
    RC_AMORETCB();
  }
  else {
    RC_AMOREDAQ();
  }
}

// ============================================================
//  LaunchDAQServers
//  SSH into each daqserver and start amoredaq in the background.
//  Requires passwordless SSH and amoredaq in PATH on remote hosts.
//  The config file path must be accessible on all hosts (e.g. NFS).
// ============================================================
void AMOREDAQManager::LaunchDAQServers()
{
  auto * daqconf = static_cast<DAQConf *>(fConfigList->GetDAQConfig());
  if (!daqconf) return;

  for (int i = 0; i < daqconf->GetN(); i++) {
    int id = daqconf->GetID(i);
    if (id == fDAQID) continue; // skip master itself

    std::string ip       = daqconf->GetIPAddr(id);
    std::string daq_name = daqconf->GetDAQName(id);
    std::string output   = fDAQOutputs.count(id) ? fDAQOutputs.at(id) : "/data/run";
    std::string logfile  = Form("/data/testData/LOG/%s.log", daq_name.c_str());

    // Kill any leftover amoredaq process from a previous run so the port is free.
    std::system(Form("ssh %s 'pkill -x amoredaq; true'", ip.c_str()));

    // Derive absolute paths so the remote shell can locate both the binary and the config
    // regardless of its working directory. RPATH in the binary covers all shared libraries.
    std::filesystem::path bin_dir =
      std::filesystem::read_symlink("/proc/self/exe").parent_path();
    std::string amoredaq_bin = (bin_dir / "amoredaq").string();
    std::string abs_config   = std::filesystem::absolute(fConfigFilename).string();

    // Build the remote command:
    //   nohup <abs>/amoredaq -c <abs_config> -r <run> -d <id> -o <output> > <log> 2>&1 &
    std::string remote_cmd = Form(
      "nohup %s -c %s -r %d -d %d -o %s > %s 2>&1 &",
      amoredaq_bin.c_str(), abs_config.c_str(), fRunNumber, id,
      output.c_str(), logfile.c_str()
    );

    std::string ssh_cmd = Form("ssh %s '%s'", ip.c_str(), remote_cmd.c_str());

    INFO("[%s] launching: %s", daq_name.c_str(), ssh_cmd.c_str());
    int ret = std::system(ssh_cmd.c_str());
    if (ret != 0) {
      WARNING("[%s] SSH launch returned %d (may be non-fatal)", daq_name.c_str(), ret);
    }
  }

  // Give daqservers a moment to start their ZMQ servers
  INFO("waiting 3 s for daqservers to initialize...");
  std::this_thread::sleep_for(std::chrono::seconds(3));
}

// ============================================================
//  RC_AMORETCB — master controller
//  Runs on the master server (has its own TCB for muon system).
//  Coordinates amoredaq instances on remote daqservers via ZMQ.
// ============================================================
void AMOREDAQManager::RC_AMORETCB()
{
  INFO("amoretcbdaq (master) starting [run=%d]", fRunNumber);

  auto * daqconf = static_cast<DAQConf *>(fConfigList->GetDAQConfig());
  if (!daqconf) {
    ERROR("RC_AMORETCB: no DAQ section in config — cannot run as master");
    return;
  }

  fDAQName = daqconf->GetDAQName(fDAQID);
  fDAQPort = daqconf->GetPort(fDAQID);

  // ----------------------------------------------------------
  // Launch amoredaq on each remote daqserver via SSH
  // ----------------------------------------------------------
  LaunchDAQServers();

  // ----------------------------------------------------------
  // Connect ZMQ REQ sockets to each daqserver
  // ----------------------------------------------------------
  bool socketerror = false;
  for (int i = 0; i < daqconf->GetN(); i++) {
    int id = daqconf->GetID(i);
    if (id == fDAQID) continue; // skip self

    std::string ip       = daqconf->GetIPAddr(id);
    int         port     = daqconf->GetPort(id);
    std::string daq_name = daqconf->GetDAQName(id);
    std::string endpoint = "tcp://" + ip + ":" + std::to_string(port);

    auto socket = std::make_unique<zmq::socket_t>(fZMQContext, zmq::socket_type::req);
    socket->set(zmq::sockopt::req_relaxed, 1);
    socket->set(zmq::sockopt::rcvtimeo, 5000);
    socket->set(zmq::sockopt::sndtimeo, 5000);
    socket->connect(endpoint);

    // verify connection
    nlohmann::json ping;
    ping["command"] = "kQUERYDAQSTATUS";
    std::string ping_str = ping.dump();
    zmq::message_t req(ping_str.size());
    std::memcpy(req.data(), ping_str.c_str(), ping_str.size());

    INFO("[%s] connecting to %s ...", daq_name.c_str(), endpoint.c_str());
    auto send_res = socket->send(req, zmq::send_flags::none);
    if (!send_res) {
      ERROR("[%s] send failed at %s", daq_name.c_str(), endpoint.c_str());
      socketerror = true;
      break;
    }

    zmq::message_t reply;
    auto recv_res = socket->recv(reply, zmq::recv_flags::none);
    if (!recv_res) {
      ERROR("[%s] connection timeout at %s", daq_name.c_str(), endpoint.c_str());
      socketerror = true;
      break;
    }

    fDAQSocket.push_back(std::move(socket));
    INFO("[%s] connected at %s", daq_name.c_str(), endpoint.c_str());
  }

  if (socketerror) {
    ERROR("RC_AMORETCB: failed to connect to all daqservers");
    fDAQSocket.clear();
    return;
  }

  // ----------------------------------------------------------
  // Wait for all daqservers to reach kBOOTED
  // ----------------------------------------------------------
  INFO("waiting for all daqservers to boot...");
  if (!WaitDAQStatus(RUNSTATE::kBOOTED)) {
    ERROR("RC_AMORETCB: daqserver boot failed");
    fDAQSocket.clear();
    return;
  }
  INFO("all daqservers booted");

  // ----------------------------------------------------------
  // kCONFIGRUN
  // ----------------------------------------------------------
  SendCommandToDAQs("kCONFIGRUN");
  INFO("waiting for all daqservers to configure...");
  if (!WaitDAQStatus(RUNSTATE::kCONFIGURED)) {
    ERROR("RC_AMORETCB: daqserver configure failed");
    SendCommandToDAQs("kEXIT");
    fDAQSocket.clear();
    return;
  }
  INFO("all daqservers configured");

  // ----------------------------------------------------------
  // kSTARTRUN
  // ----------------------------------------------------------
  SendCommandToDAQs("kSTARTRUN");
  INFO("waiting for all daqservers to start running...");
  if (!WaitDAQStatus(RUNSTATE::kRUNNING)) {
    ERROR("RC_AMORETCB: daqserver start failed");
    SendCommandToDAQs("kEXIT");
    fDAQSocket.clear();
    return;
  }
  time(&fStartDatime);
  INFO("run=%d started — all daqservers running", fRunNumber);

  // ----------------------------------------------------------
  // Monitor loop: print per-DAQ trigger rate every 10 s
  // ----------------------------------------------------------
  const int kStatusInterval = 10;
  int elapsed = 0;

  // previous trigger count per daqserver (indexed by socket position)
  const int ndaqsockets = static_cast<int>(fDAQSocket.size());
  std::vector<unsigned long> ntrg_prev(ndaqsockets, 0);
  auto tprev = std::chrono::steady_clock::now();

  while (elapsed < fSetDAQTime) {
    if (IsForcedEndRunFile()) {
      INFO("forced end-run file detected");
      break;
    }

    int remaining = fSetDAQTime - elapsed;
    int sleepSec  = (remaining < kStatusInterval) ? remaining : kStatusInterval;
    std::this_thread::sleep_for(std::chrono::seconds(sleepSec));
    elapsed += sleepSec;

    auto tnow = std::chrono::steady_clock::now();
    double dt  = std::chrono::duration<double>(tnow - tprev).count();
    tprev = tnow;

    char tbuf[32];
    std::time_t now = std::time(nullptr);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&now));

    std::string line = Form("[%s] elapsed: %3d s ", tbuf, elapsed);

    for (int idx = 0; idx < ndaqsockets; ++idx) {
      auto & socket = fDAQSocket[idx];
      if (!socket) continue;

      std::string daq_name;
      nlohmann::json reply = SendCommandToDAQ(socket, "kQUERYTRGINFO", daq_name);

      if (reply.value("status", "error") == "ok") {
        unsigned long nevent = reply.value("nevent", 0ul);
        double rate = dt > 0.0 ? static_cast<double>(nevent - ntrg_prev[idx]) / dt : 0.0;
        ntrg_prev[idx] = nevent;
        line += Form(" | %s: %.2f Hz", daq_name.c_str(), rate);
      }
      else {
        line += Form(" | %s: ---", daq_name.c_str());
      }
    }
    std::cout << line << std::endl;
  }

  // ----------------------------------------------------------
  // kENDRUN
  // ----------------------------------------------------------
  SendCommandToDAQs("kENDRUN");
  INFO("waiting for all daqservers to end run...");
  if (!WaitDAQStatus(RUNSTATE::kRUNENDED)) {
    ERROR("RC_AMORETCB: daqserver end-run failed");
  }
  INFO("waiting for all daqserver processes to finish...");
  if (!WaitDAQStatus(RUNSTATE::kPROCENDED)) {
    ERROR("RC_AMORETCB: daqserver process-end failed");
  }
  time(&fEndDatime);
  INFO("run=%d ended", fRunNumber);

  // ----------------------------------------------------------
  // kEXIT
  // ----------------------------------------------------------
  SendCommandToDAQs("kEXIT");
  fDAQSocket.clear();

  INFO("amoretcbdaq (master) ended");
}

// ============================================================
//  RC_AMOREDAQ — daqserver
//
//  Slave mode  : DAQConf present in config
//                → start ZMQ MsgServer, wait for commands from master
//  Standalone  : no DAQConf (existing YAML without DAQ: section)
//                → run independently as before (backward compatible)
// ============================================================
void AMOREDAQManager::RC_AMOREDAQ()
{
  auto * daqconf = static_cast<DAQConf *>(fConfigList->GetDAQConfig());
  const bool isSlave = (daqconf != nullptr);

  // ---- threads ----
  std::thread th_msg;
  std::thread th1, th2, th3;
  std::thread th_swt[8];

  if (isSlave) {
    // slave mode: set DAQ name/port for ZMQ MsgServer
    fDAQName = daqconf->GetDAQName(fDAQID);
    fDAQPort = daqconf->GetPort(fDAQID);
    INFO("amoredaq [%s] starting in slave mode (port=%d) [run=%d]",
         fDAQName.c_str(), fDAQPort, fRunNumber);

    th_msg = std::thread(&AMOREDAQManager::TF_MsgServer, this);
    RUNSTATE::SetState(fRunStatus, RUNSTATE::kBOOTED);

    // wait for kCONFIGRUN from master
    if (WaitCommand(fDoConfigRun, fDoExit) != 0) {
      INFO("amoredaq [%s] exited before CONFIGRUN", fDAQName.c_str());
      fDoExit = true;
      th_msg.join();
      return;
    }
  }
  else {
    INFO("amoredaq starting in standalone mode [run=%d]", fRunNumber);
  }

  // ----------------------------------------------------------
  // Open TCB + ADC
  // ----------------------------------------------------------
  if (fTCB.Open() != 0) {
    RUNSTATE::SetError(fRunStatus);
    ERROR("TCB open failed");
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }
  fTCB.SetConfig(fConfigList);
  if (!fTCB.Config()) {
    RUNSTATE::SetError(fRunStatus);
    ERROR("TCB config failed");
    fTCB.Close();
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }
  if (!AddADC(fConfigList)) {
    RUNSTATE::SetError(fRunStatus);
    fTCB.Close();
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }
  if (!PrepareDAQ()) {
    RUNSTATE::SetError(fRunStatus);
    fTCB.Close();
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }
  if (!OpenDAQ()) {
    RUNSTATE::SetError(fRunStatus);
    fTCB.Close();
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }

  if (isSlave) {
    RUNSTATE::SetState(fRunStatus, RUNSTATE::kCONFIGURED);
    INFO("amoredaq [%s] configured", fDAQName.c_str());

    // wait for kSTARTRUN from master
    if (WaitCommand(fDoStartRun, fDoExit) != 0) {
      INFO("amoredaq [%s] exited before STARTRUN", fDAQName.c_str());
      fDoExit = true;
      CloseDAQ(); fTCB.Close();
      th_msg.join();
      return;
    }
  }

  // ----------------------------------------------------------
  // Start data threads (wait on kRUNNING via ThreadWait)
  // ----------------------------------------------------------
  const int nadc = GetEntries();
  th1 = std::thread(&AMOREDAQManager::TF_ReadData_AMORE, this);
  th2 = std::thread(&AMOREDAQManager::TF_StreamData, this);
  th3 = std::thread(&AMOREDAQManager::TF_WriteEvent_AMORE, this);
  for (int i = 0; i < nadc; ++i) {
    th_swt[i] = std::thread(&AMOREDAQManager::TF_SWTrigger, this, i);
  }

  // ----------------------------------------------------------
  // Start hardware + measure pedestal → set kRUNNING
  // ----------------------------------------------------------
  fTCB.TriggerStart();
  if (!MeasurePedestal()) {
    RUNSTATE::SetError(fRunStatus);
    fTCB.TriggerStop();
    th1.join(); th2.join();
    for (int i = 0; i < nadc; ++i) th_swt[i].join();
    th3.join();
    CloseDAQ(); fTCB.Close();
    if (isSlave) { fDoExit = true; th_msg.join(); }
    return;
  }
  RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNNING);
  time(&fStartDatime);
  INFO("amoredaq running");

  // ----------------------------------------------------------
  // Wait for end condition
  // ----------------------------------------------------------
  if (isSlave) {
    // slave: wait for kENDRUN command from master
    WaitCommand(fDoEndRun, fRunStatus);
  }
  else {
    // standalone: time-based loop, print instantaneous trigger rate every 10 s
    const int kStatusInterval = 10;
    int elapsed = 0;
    unsigned int ntrg_prev = 0;
    auto tprev = std::chrono::steady_clock::now();

    while (elapsed < fSetDAQTime) {
      int remaining = fSetDAQTime - elapsed;
      int sleepSec  = (remaining < kStatusInterval) ? remaining : kStatusInterval;
      std::this_thread::sleep_for(std::chrono::seconds(sleepSec));
      elapsed += sleepSec;

      unsigned int ntrg;
      {
        std::lock_guard<std::mutex> lock(fMonitorMutex);
        ntrg = fTriggerNumber;
      }
      auto tnow = std::chrono::steady_clock::now();
      double dt  = std::chrono::duration<double>(tnow - tprev).count();
      double rate = dt > 0.0 ? (ntrg - ntrg_prev) / dt : 0.0;
      ntrg_prev = ntrg;
      tprev = tnow;

      char tbuf[32];
      std::time_t now = std::time(nullptr);
      std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", std::localtime(&now));
      std::cout << Form("[%s] elapsed: %3d s  trigger rate: %.2f Hz", tbuf, elapsed, rate)
                << std::endl;
    }
  }

  // ----------------------------------------------------------
  // Stop hardware and drain threads
  // ----------------------------------------------------------
  fTCB.TriggerStop();
  RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNENDED);
  time(&fEndDatime);

  th1.join(); th2.join();
  for (int i = 0; i < nadc; ++i) th_swt[i].join();
  th3.join();

  CloseDAQ();
  fTCB.Close();

  if (isSlave) {
    RUNSTATE::SetState(fRunStatus, RUNSTATE::kPROCENDED);
    INFO("amoredaq [%s] process ended, waiting for EXIT command", fDAQName.c_str());
    WaitCommand(fDoExit);
    th_msg.join();
  }

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

  double recvDataSize   = totalReadDataSize / kDGIGABYTES;
  double outputDataSize = fTotalWrittenDataSize / kDGIGABYTES;

  double trate = liveTime > 0.0 ? fTriggerNumber / liveTime : 0.0;
  double drate = liveTime > 0.0 ? recvDataSize   * 1024.0   / liveTime : 0.0;
  double orate = liveTime > 0.0 ? outputDataSize * 1024.0   / liveTime : 0.0;

  std::cout << std::endl;
  std::cout << "************************* DAQ Summary *************************" << std::endl;
  std::cout << Form("%32s", "Run number : ")             << fRunNumber << std::endl;
  std::cout << Form("%32s", "Start Time : ")             << TDatime(fStartDatime).AsSQLString() << std::endl;
  std::cout << Form("%32s", "End Time : ")               << TDatime(fEndDatime).AsSQLString()   << std::endl;
  std::cout << std::endl;
  std::cout << Form("%32s", "Live time : ")              << Form("%.1f", liveTime) << " [s]" << std::endl;
  std::cout << Form("%32s", "Number of ADC : ")          << nadc << std::endl;
  std::cout << Form("%32s", "Total number of trigger : ") << Form("%d", fTriggerNumber) << std::endl;
  std::cout << Form("%32s", "Trigger rate : ")           << Form("%.2f", trate) << " [Hz]" << std::endl;
  std::cout << Form("%32s", "Total number of event : ")  << Form("%d", fNBuiltEvent) << std::endl;
  std::cout << std::endl;
  std::cout << Form("%32s", "Received data size : ")
            << Form("%.3f GBytes (%.3f MB/sec)", recvDataSize, drate) << std::endl;
  std::cout << Form("%32s", "Written data size : ")
            << Form("%.3f GBytes (%.3f MB/sec)", outputDataSize, orate) << std::endl;
  std::cout << "***************************************************************" << std::endl;
  std::cout << std::endl;
}
