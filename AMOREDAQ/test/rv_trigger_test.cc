// rv_trigger_test.cc
// Usage: rv_trigger_test <config_file> <template_root_file> <output_h5file> <h5file1> [h5file2 ...]
//
// Multi-file sequential mode: h5files are processed in order with all filter/trigger state
// maintained across file boundaries — matching read_amod.C behaviour.
// Baseline and warmup (fWarmupLeft) are applied only once at the start of the first file.
//
// config_file: AMoRE-I offline trigger config (e.g. 000664.config or 000657.config).
//   TRGON bit 0 = RVH (heat cross-correlation)
//   TRGON bit 4 = RVL (light cross-correlation)
//
// output_h5file: HDF5 waveform output.
//   Structure per triggered XID:
//     /xid{NN}/trgtime  [N]       float64  trigger time (ms, aligned with Ofl trgtime)
//     /xid{NN}/trgpoint scalar    int32    trigger position within waveform window (= DELAY)
//     /xid{NN}/trgbit   [N]       int32    1=RVH, 4=RVL, 5=both
//     /xid{NN}/waveform [N, RL]   uint16   heat channel ADC/4, trigger at sample DELAY

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "hdf5.h"

#include "AMORE/amoreconsts.hh"
#include "AMOREAlgs/RVCore.hh"
#include "AMOREAlgs/RVTemplate.hh"
#include "AMOREAlgs/SignalTrigger.hh"
#include "AMORESystem/AMOREADCConf.hh"

// ---------------------------------------------------------------------------
// Legacy-mode driver: one RVCore per enabled channel of a board, set up from an
// AMOREADCConf (TRGON, THR, RVTHRY/X, CID = template xid, BWF, RV_NWIN, RV_DS,
// SR, DLY, template prefixes). Same engine RVCondition runs, but fed with float
// input (raw * 0.25 - float baseline) to match read_amod.C bit for bit.
// ---------------------------------------------------------------------------
class RVTriggerDriver {
public:
  explicit RVTriggerDriver(const char * name) : fName(name) {}
  void SetConfig(AMOREADCConf * c) { fConf = c; }

  bool Prepare()
  {
    RVTemplateFile tf;
    if (!tf.Open(fConf->RVTmpltFile())) { std::cerr << fName << ": " << tf.Error() << "\n"; return false; }
    const std::string & pfx  = fConf->RVTmpltHistPfx();
    const std::string & pfx50 = fConf->RVTmpltHistPfxFR50();
    const bool useFR50 = pfx50 != pfx;

    RVParams par;
    par.bwfOrder = fConf->BWFOrder(); par.bwfLC = fConf->BWFLC(); par.bwfUC = fConf->BWFUC();
    par.nwin = fConf->RVNWin(); par.ds = fConf->RVDS();
    par.sr = fConf->SamplingFreqHz(); par.warmup = fConf->DLY();

    for (int ch = 0; ch < AMORE::kNCHPERADC; ++ch) {
      if (!fConf->TRGON(ch)) continue;
      std::vector<double> tmpl, fr50;
      const int xid = fConf->CID(ch);
      if (!tf.Load(pfx, xid, tmpl) || (useFR50 && !tf.Load(pfx50, xid, fr50))) {
        std::cerr << fName << " ch" << ch << ": " << tf.Error() << "\n"; return false;
      }
      par.thr = fConf->THR(ch) * 1e-3; par.thry = fConf->RVTHRY(ch); par.thrx = fConf->RVTHRX(ch);
      std::string err;
      if (!fCore[ch].Prepare(par, tmpl, useFR50 ? &fr50 : nullptr, err)) {
        std::cerr << fName << " ch" << ch << ": " << err << "\n"; return false;
      }
    }
    return true;
  }

  void SetBaselinesD(const double * v, int nch)
  {
    for (int i = 0; i < nch && i < AMORE::kNCHPERADC; ++i) fBase[i] = v[i];
  }
  bool   EvalF(int ch, double v)     { return fCore[ch].Eval(v - fBase[ch]); }
  double GetLastRV(int ch)  const    { return fCore[ch].LastRV(); }
  double GetLastARV(int ch) const    { return fCore[ch].LastARV(); }

private:
  std::string    fName;
  AMOREADCConf * fConf{nullptr};
  RVCore         fCore[AMORE::kNCHPERADC];
  double         fBase[AMORE::kNCHPERADC]{};
};

// ---------------------------------------------------------------------------
// Config parsing
// ---------------------------------------------------------------------------
struct CrystalInfo {
  int  xid;
  int  phonon_ch;
  int  photon_ch;
  bool trgon;
  bool trgon_rvl;
  int  thr;
  int  thr_rvl;
  int  thry{0};
  int  thrx{0};
};

struct ParsedConfig {
  int    bwforder{2};
  double bwflc{150.0};
  double bwfuc{2000.0};
  int    bwfl_order{2};
  double bwfl_lc{800.0};
  double bwfl_uc{2400.0};
  int    nh{1600};
  int    njh{8};
  int    nl{240};
  int    njl{4};
  int    rl{30000};
  int    delay{13000};
  int    dt{1000};
  int    namoreadc{3};
  int    tmprun{-1};
  std::string tmplfile{};
  std::vector<CrystalInfo> boards[3];
};

static bool parse_config(const std::string & path, ParsedConfig & cfg)
{
  std::ifstream f(path);
  if (!f) { std::cerr << "Cannot open config: " << path << "\n"; return false; }

  std::string line, tok;
  bool past_stop = false;
  int  cur_board = -1;

  std::vector<int> xids[3], cidh[3], cidl[3], trgon[3];
  std::vector<int> thr_rvh[3], thr_rvy[3], thr_rvx[3], thr_rvl[3];

  auto strip_comment = [](std::string & s) {
    auto p = s.find('#');
    if (p != std::string::npos) s.resize(p);
  };

  while (std::getline(f, line)) {
    strip_comment(line);
    std::istringstream iss(line);
    if (!(iss >> tok) || tok.empty()) continue;

    if (!past_stop) {
      if (tok == "STOP") { past_stop = true; continue; }
      if      (tok == "BWFH")     { iss >> cfg.bwforder >> cfg.bwflc >> cfg.bwfuc; }
      else if (tok == "BWFL")     { iss >> cfg.bwfl_order >> cfg.bwfl_lc >> cfg.bwfl_uc; }
      else if (tok == "FORDER")   { iss >> cfg.bwforder; }
      else if (tok == "FCUTOFFH") { iss >> cfg.bwflc >> cfg.bwfuc; }
      else if (tok == "FCUTOFFL") { iss >> cfg.bwfl_lc >> cfg.bwfl_uc; }
      else if (tok == "NH")       { iss >> cfg.nh; int t; if (iss >> t) cfg.njh = t; }
      else if (tok == "NJH")      { iss >> cfg.njh; }
      else if (tok == "NL")       { iss >> cfg.nl; int t; if (iss >> t) cfg.njl = t; }
      else if (tok == "NJL")      { iss >> cfg.njl; }
      else if (tok == "RL")       { iss >> cfg.rl; }
      else if (tok == "DELAY")    { iss >> cfg.delay; }
      else if (tok == "DT")       { iss >> cfg.dt; }
      else if (tok == "NAMOREADC"){ iss >> cfg.namoreadc; }
      else if (tok == "TMPRUN")   { iss >> cfg.tmprun; }
      continue;
    }

    if (tok == "AMOREADC") {
      int mid, nch; iss >> mid >> nch;
      if (mid >= 0 && mid < 3) cur_board = mid;
      continue;
    }
    if (cur_board < 0) continue;

    if      (tok == "XID")                         { int v; while (iss >> v) xids[cur_board].push_back(v - 1); }
    else if (tok == "CID")                         { int v; while (iss >> v) { if (cidh[cur_board].size()==cidl[cur_board].size()) cidh[cur_board].push_back(v-1); else cidl[cur_board].push_back(v-1); } }
    else if (tok == "TRGON")                       { int v; while (iss >> v) trgon[cur_board].push_back(v); }
    else if (tok == "THRH_RV" || tok == "THR_RVH") { int v; while (iss >> v) thr_rvh[cur_board].push_back(v); }
    else if (tok == "THR_RVY")                     { int v; while (iss >> v) thr_rvy[cur_board].push_back(v); }
    else if (tok == "THR_RVX")                     { int v; while (iss >> v) thr_rvx[cur_board].push_back(v); }
    else if (tok == "THRL_RV" || tok == "THR_RVL") { int v; while (iss >> v) thr_rvl[cur_board].push_back(v); }
  }

  for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
    const int nxtal = static_cast<int>(xids[b].size());
    for (int j = 0; j < nxtal; ++j) {
      CrystalInfo ci;
      ci.xid       = xids[b][j];
      ci.phonon_ch = (j < (int)cidh[b].size()) ? cidh[b][j] : -1;
      ci.photon_ch = (j < (int)cidl[b].size()) ? cidl[b][j] : -1;
      ci.trgon     = (j < (int)trgon[b].size()) ? (trgon[b][j] & 1) != 0 : false;
      ci.trgon_rvl = (j < (int)trgon[b].size()) ? (trgon[b][j] & 4) != 0 : false;
      ci.thr       = (j < (int)thr_rvh[b].size()) ? thr_rvh[b][j] : 900;
      ci.thr_rvl   = (j < (int)thr_rvl[b].size()) ? thr_rvl[b][j] : 900;
      ci.thry      = (j < (int)thr_rvy[b].size()) ? thr_rvy[b][j] : 0;
      ci.thrx      = (j < (int)thr_rvx[b].size()) ? thr_rvx[b][j] : 0;
      bool active_h = ci.trgon     && ci.phonon_ch >= 0 && ci.xid >= 0;
      bool active_l = ci.trgon_rvl && ci.photon_ch >= 0 && ci.xid >= 0;
      if (!active_h && !active_l) continue;
      cfg.boards[b].push_back(ci);
    }
  }
  return true;
}

static void print_config(const ParsedConfig & cfg)
{
  std::cerr << "--- Config ---\n";
  std::cerr << "  BWF heat:  order=" << cfg.bwforder
            << " lc=" << cfg.bwflc << " uc=" << cfg.bwfuc << " Hz\n";
  std::cerr << "  BWF light: order=" << cfg.bwfl_order
            << " lc=" << cfg.bwfl_lc << " uc=" << cfg.bwfl_uc << " Hz\n";
  std::cerr << "  NH=" << cfg.nh << " NJH=" << cfg.njh
            << "  -> NWIN_H=" << cfg.nh/cfg.njh << " DS_H=" << cfg.njh << "\n";
  std::cerr << "  NL=" << cfg.nl << " NJL=" << cfg.njl
            << "  -> NWIN_L=" << cfg.nl/cfg.njl << " DS_L=" << cfg.njl << "\n";
  std::cerr << "  RL=" << cfg.rl << " DELAY=" << cfg.delay << " DT=" << cfg.dt << "\n";
  std::cerr << "  Template: " << cfg.tmplfile << "\n";
  for (int b = 0; b < 3; ++b) {
    if (cfg.boards[b].empty()) continue;
    std::cerr << "  Board " << b << ": " << cfg.boards[b].size() << " crystals\n";
    for (const auto & ci : cfg.boards[b])
      std::cerr << "    xid=" << ci.xid
                << " ph=" << ci.phonon_ch << "(rvh=" << ci.trgon << ",thr=" << ci.thr << ")"
                << " pl=" << ci.photon_ch << "(rvl=" << ci.trgon_rvl << ",thr=" << ci.thr_rvl << ")"
                << "\n";
  }
  std::cerr << "--------------\n";
}

// ---------------------------------------------------------------------------
// HDF5 input
// ---------------------------------------------------------------------------
struct ContData {
  int      chunkno;
  uint64_t time[3];
  uint32_t adc[3][16][32768];
};

static hid_t make_h5_dtype()
{
  hsize_t dim_t[1] = {3};
  hid_t atime = H5Tarray_create(H5T_NATIVE_UINT64, 1, dim_t);
  hsize_t dim_a[3] = {3, 16, 32768};
  hid_t aadc = H5Tarray_create(H5T_NATIVE_UINT, 3, dim_a);
  hid_t dtype = H5Tcreate(H5T_COMPOUND, sizeof(ContData));
  H5Tinsert(dtype, "chunkno", HOFFSET(ContData, chunkno), H5T_NATIVE_INT);
  H5Tinsert(dtype, "time",    HOFFSET(ContData, time),    atime);
  H5Tinsert(dtype, "adc",     HOFFSET(ContData, adc),     aadc);
  H5Tclose(atime);
  H5Tclose(aadc);
  return dtype;
}

// ---------------------------------------------------------------------------
// Trigger event and per-file descriptor
// ---------------------------------------------------------------------------
struct TrigEvent {
  double    trgtime_ms;
  int       board, xid, ch;
  long long global_sample;  // absolute raw-sample index from run start
  int       trgbit;
};

// Describes one input HDF5 file in the sequential run
struct FileDesc {
  std::string path;
  long long   first_sample;  // global raw-sample index of this file's first sample
  hsize_t     nchunks;
};

// ---------------------------------------------------------------------------
// Per-crystal dead-time combining state
// ---------------------------------------------------------------------------
struct XIDState {
  int       dtcnt{0};
  int       trgbit{0};
  double    trgtime{0.0};
  int       board{-1};
  int       ch{-1};
  long long global_sample{0};
};

// ---------------------------------------------------------------------------
// HDF5 waveform output (pass 2) — multi-file version
// ---------------------------------------------------------------------------
static void write_waveforms(
    const std::string            & outpath,
    const std::vector<TrigEvent> & events,
    const ParsedConfig           & cfg,
    const std::vector<FileDesc>  & files,
    hid_t                          in_dtype)
{
  std::cerr << "Writing waveforms -> " << outpath << "\n";

  if (files.empty() || events.empty()) {
    std::cerr << "  (nothing to write)\n";
    return;
  }

  // Open all input files and their dataspaces up front
  const int NF = static_cast<int>(files.size());
  std::vector<hid_t> fids(NF, -1), dsets(NF, -1), fspaces(NF, -1);
  for (int k = 0; k < NF; ++k) {
    fids[k]    = H5Fopen(files[k].path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    dsets[k]   = H5Dopen2(fids[k], "rawdata", H5P_DEFAULT);
    fspaces[k] = H5Dget_space(dsets[k]);
  }

  // 4-slot chunk cache: (file_idx, local_chunk) → ContData
  struct CSlot { int fi{-1}; hsize_t ci{~0ULL}; std::unique_ptr<ContData> data; };
  const int NCACHE = 4;
  std::vector<CSlot> cache(NCACHE);
  for (auto & s : cache) s.data = std::make_unique<ContData>();
  int cache_ptr = 0;

  hsize_t memdims[1] = {1};
  hid_t msp = H5Screate_simple(1, memdims, nullptr);

  // Returns the ADC value at a given global raw-sample position.
  auto get_adc = [&](long long gsi, int board, int ch) -> uint16_t {
    if (gsi < 0) return 0;
    // Find file (files are sorted by first_sample ascending)
    int fi = -1;
    for (int k = NF - 1; k >= 0; --k)
      if (gsi >= files[k].first_sample) { fi = k; break; }
    if (fi < 0) return 0;
    long long local = gsi - files[fi].first_sample;
    hsize_t ci = (hsize_t)(local / 32768);
    int     si = (int)(local % 32768);
    if (ci >= files[fi].nchunks) return 0;
    // Check cache
    for (auto & s : cache)
      if (s.fi == fi && s.ci == ci)
        return (uint16_t)(s.data->adc[board][ch][si] / 4);
    // Cache miss
    auto & slot = cache[cache_ptr++ % NCACHE];
    slot.fi = fi; slot.ci = ci;
    hsize_t off[1] = {ci}, cnt[1] = {1};
    H5Sselect_hyperslab(fspaces[fi], H5S_SELECT_SET, off, nullptr, cnt, nullptr);
    H5Dread(dsets[fi], in_dtype, msp, fspaces[fi], H5P_DEFAULT, slot.data.get());
    return (uint16_t)(slot.data->adc[board][ch][si] / 4);
  };

  // Group events by xid
  std::map<int, std::vector<const TrigEvent *>> by_xid;
  for (const auto & ev : events)
    by_xid[ev.xid].push_back(&ev);

  hid_t fout = H5Fcreate(outpath.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

  const int RL = cfg.rl;
  hsize_t wvf_memdims[1] = {(hsize_t)RL};
  hid_t   wvf_msp        = H5Screate_simple(1, wvf_memdims, nullptr);
  std::vector<uint16_t> wvf(RL);

  int nxid_written = 0;
  long long nwvf_total = 0;

  for (auto & [xid, evs] : by_xid) {
    const int N = static_cast<int>(evs.size());
    char gname[16];
    std::snprintf(gname, sizeof(gname), "xid%02d", xid);
    hid_t grp = H5Gcreate2(fout, gname, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    // trgtime [N]
    {
      hsize_t dims[1] = {(hsize_t)N};
      hid_t sp = H5Screate_simple(1, dims, nullptr);
      hid_t ds = H5Dcreate2(grp, "trgtime", H5T_NATIVE_DOUBLE,
                             sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      std::vector<double> t(N);
      for (int i = 0; i < N; ++i) t[i] = evs[i]->trgtime_ms;
      H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, t.data());
      H5Dclose(ds); H5Sclose(sp);
    }
    // trgpoint scalar
    {
      hsize_t dims[1] = {1};
      hid_t sp = H5Screate_simple(1, dims, nullptr);
      hid_t ds = H5Dcreate2(grp, "trgpoint", H5T_NATIVE_INT,
                             sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      int tp = cfg.delay;
      H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, &tp);
      H5Dclose(ds); H5Sclose(sp);
    }
    // trgbit [N]
    {
      hsize_t dims[1] = {(hsize_t)N};
      hid_t sp = H5Screate_simple(1, dims, nullptr);
      hid_t ds = H5Dcreate2(grp, "trgbit", H5T_NATIVE_INT,
                             sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      std::vector<int> tb(N);
      for (int i = 0; i < N; ++i) tb[i] = evs[i]->trgbit;
      H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, tb.data());
      H5Dclose(ds); H5Sclose(sp);
    }
    // waveform [N, RL]
    hid_t wvf_ds, wvf_fsp;
    {
      hsize_t dims[2]  = {(hsize_t)N, (hsize_t)RL};
      hid_t sp         = H5Screate_simple(2, dims, nullptr);
      hid_t plist      = H5Pcreate(H5P_DATASET_CREATE);
      hsize_t cdims[2] = {1, (hsize_t)RL};
      H5Pset_chunk(plist, 2, cdims);
      H5Pset_deflate(plist, 1);
      wvf_ds  = H5Dcreate2(grp, "waveform", H5T_NATIVE_UINT16,
                            sp, H5P_DEFAULT, plist, H5P_DEFAULT);
      wvf_fsp = H5Dget_space(wvf_ds);
      H5Pclose(plist); H5Sclose(sp);
    }

    for (int i = 0; i < N; ++i) {
      const TrigEvent & ev = *evs[i];
      long long wstart = ev.global_sample - cfg.delay;
      for (int s = 0; s < RL; ++s)
        wvf[s] = get_adc(wstart + s, ev.board, ev.ch);

      hsize_t start[2] = {(hsize_t)i, 0};
      hsize_t count[2] = {1, (hsize_t)RL};
      H5Sselect_hyperslab(wvf_fsp, H5S_SELECT_SET, start, nullptr, count, nullptr);
      H5Dwrite(wvf_ds, H5T_NATIVE_UINT16, wvf_msp, wvf_fsp, H5P_DEFAULT, wvf.data());
    }

    H5Sclose(wvf_fsp); H5Dclose(wvf_ds);
    H5Gclose(grp);
    nwvf_total += N;
    ++nxid_written;
    std::cerr << "  " << gname << ": " << N << " waveforms\n";
  }

  H5Sclose(wvf_msp);
  H5Sclose(msp);
  H5Fclose(fout);

  for (int k = 0; k < NF; ++k) {
    H5Sclose(fspaces[k]); H5Dclose(dsets[k]); H5Fclose(fids[k]);
  }

  std::cerr << "Done: " << nxid_written << " XIDs, "
            << nwvf_total << " waveforms -> " << outpath << "\n";
}

// ---------------------------------------------------------------------------
// Input conversion shared by the legacy (--quantize) and --signal paths
// ---------------------------------------------------------------------------
// --quantize: feed the legacy drivers what the DAQ FIFO holds — integer ADC
// (raw / 4, unsigned short) and an integer baseline — instead of raw * 0.25.
static bool g_quantize = false;

static double float_baseline(const ContData * row, int b, int ch)
{
  double sum = 0;
  for (int j = 0; j < 100; ++j) sum += row->adc[b][ch][j] * 0.25;
  return sum / 100.0;
}

// Integer baseline as SignalTrigger gets it (AbsSWTrigger::fBaseline is int).
static int int_baseline(const ContData * row, int b, int ch)
{
  long sum = 0;
  for (int j = 0; j < 100; ++j) sum += row->adc[b][ch][j] / 4;
  return static_cast<int>(sum / 100);
}

// ---------------------------------------------------------------------------
// --signal: replay through ChunkDataFIFO -> SignalTrigger (the DAQ path)
// ---------------------------------------------------------------------------
// One SignalTrigger per board with an RVH and an RVL condition, fed by the main
// thread and consumed by one thread per board (like TF_StreamData / TF_SWTrigger).
// Synthetic time tags: sample gs -> gs * 10000 ns, so global_sample = ttime / 10000.
// Output: same stdout columns as the legacy mode (trgbit = fired bits 0-7) plus
// the opening bits (trgbit >> 8); waveform file has the same /xidNN layout.
static int run_signal_mode(const ParsedConfig & cfg, const std::vector<std::string> & h5paths,
                           const std::string & outpath)
{
  const int NWIN_H = cfg.nh / cfg.njh;
  const int NWIN_L = cfg.nl / cfg.njl;
  const int NTRG4  = NWIN_H - NWIN_L;
  const double ms_per_sample = 0.01;
  const int deq_H  = (cfg.rl - cfg.delay) / cfg.njh;
  const int deq_L  = (cfg.rl - cfg.delay) / cfg.njl;
  const double t0_corr_h_ms = ((deq_H - 3) * cfg.njh - 1) * ms_per_sample;
  const double t0_corr_l_ms = ((deq_L - NTRG4 - 1) * cfg.njl - 1) * ms_per_sample;

  const int nb = std::min(cfg.namoreadc, 3);
  std::unique_ptr<AMOREADCConf>  conf[3];
  std::unique_ptr<SignalTrigger> st[3];
  int slot2xid[3][AMORE::kNXTALPERADC];

  for (int b = 0; b < nb; ++b) {
    for (auto & x : slot2xid[b]) x = -1;
    if (cfg.boards[b].empty()) continue;

    conf[b] = std::make_unique<AMOREADCConf>();
    auto & c = *conf[b];
    c.SetNCH(16);
    c.SetSR(10);  // [us / sample]
    c.SetRL(cfg.rl);
    c.SetDLY(cfg.delay);
    c.SetRVTmpltFile(cfg.tmplfile);

    TrgCondConf rvh, rvl;
    rvh.name = "RVH"; rvh.type = "RV"; rvh.bit = TrgBit::kRVH; rvh.target = TrgTarget::kHeat;
    rvh.bwfOrder = cfg.bwforder; rvh.bwfLC = cfg.bwflc; rvh.bwfUC = cfg.bwfuc;
    rvh.nwin = NWIN_H; rvh.ds = cfg.njh; rvh.tmpltPfx = "htmp";
    rvl.name = "RVL"; rvl.type = "RV"; rvl.bit = TrgBit::kRVL; rvl.target = TrgTarget::kLight;
    rvl.bwfOrder = cfg.bwfl_order; rvl.bwfLC = cfg.bwfl_lc; rvl.bwfUC = cfg.bwfl_uc;
    rvl.nwin = NWIN_L; rvl.ds = cfg.njl; rvl.tmpltPfx = "ltmp"; rvl.fr50Pfx = "htmp";

    bool anyL = false;
    for (const auto & ci : cfg.boards[b]) {
      if (ci.phonon_ch < 0 || ci.phonon_ch % 2 != 0 || ci.photon_ch != ci.phonon_ch + 1) {
        std::cerr << "board " << b << " xid " << ci.xid
                  << ": channel pair is not (2x, 2x+1), not supported\n";
        return 1;
      }
      const int x = ci.phonon_ch / 2;
      slot2xid[b][x] = ci.xid;
      c.SetPID(2 * x,     2 * ci.xid + 1);  // crystal id = PID / 2 = xid
      c.SetPID(2 * x + 1, 2 * ci.xid + 2);
      c.SetDT(2 * x, cfg.dt);               // crystal dead time = phonon channel DT
      rvh.on[x] = ci.trgon;     rvh.thr[x] = ci.thr;  rvh.thry[x] = ci.thry; rvh.thrx[x] = ci.thrx;
      rvl.on[x] = ci.trgon_rvl; rvl.thr[x] = ci.thr_rvl;
      anyL |= ci.trgon_rvl;
    }

    std::vector<TrgCondConf> conds{rvh};
    if (anyL) conds.push_back(rvl);

    st[b] = std::make_unique<SignalTrigger>(("Signal_B" + std::to_string(b)).c_str());
    st[b]->SetConfig(conf[b].get());
    st[b]->SetConditions(conds);
    if (!st[b]->Prepare()) { std::cerr << "board " << b << ": Prepare failed\n"; return 1; }
    std::cerr << "Board " << b << " signal path: " << conds.size() << " condition(s)\n";
  }

  // --- consumers
  struct Out { XtalTrigger rec; double rv, arv; };
  std::vector<Out> outs[3];
  std::atomic<bool> producerDone{false};
  std::vector<std::thread> workers;
  for (int b = 0; b < nb; ++b) {
    if (!st[b]) continue;
    workers.emplace_back([&, b]() {
      std::vector<XtalTrigger> recs;
      auto take = [&]() {
        for (auto & r : recs) {
          double rv = 0, arv = 0;
          const std::uint32_t open = r.trgbit >> TrgBit::kOpenShift;
          for (const auto & cond : st[b]->GetConditions()) {
            if (!(open & cond->Bit())) continue;
            auto * rc = dynamic_cast<RVCondition *>(cond.get());
            if (rc) { rv = rc->GetLastRV(r.xtal); arv = rc->GetLastARV(r.xtal); }
            break;
          }
          outs[b].push_back({std::move(r), rv, arv});
        }
        recs.clear();
      };
      while (true) {
        const int n = st[b]->DoTriggerXtal(recs);
        if (n > 0) { take(); continue; }
        if (n < 0 || (producerDone && st[b]->IsFIFOEmpty())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      st[b]->FlushPending(recs);
      take();
    });
  }

  // --- producer
  hid_t dtype = make_h5_dtype();
  auto row = std::make_unique<ContData>();
  std::vector<std::vector<unsigned short>> adc(16, std::vector<unsigned short>(32768));
  std::vector<unsigned short *> adcp(16);
  for (int ch = 0; ch < 16; ++ch) adcp[ch] = adc[ch].data();
  std::vector<unsigned long> tt(32768);
  double t0_per_board[3] = {};
  long long global_offset = 0;
  bool first = true;

  for (const auto & h5path : h5paths) {
    hid_t fid = H5Fopen(h5path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (fid < 0) { std::cerr << "Cannot open: " << h5path << "\n"; return 1; }
    hid_t dset = H5Dopen2(fid, "rawdata", H5P_DEFAULT);
    hid_t fsp  = H5Dget_space(dset);
    hsize_t nchunks = 0;
    H5Sget_simple_extent_dims(fsp, &nchunks, nullptr);
    hsize_t memdims[1] = {1};
    hid_t msp = H5Screate_simple(1, memdims, nullptr);

    for (hsize_t ic = 0; ic < nchunks; ++ic) {
      hsize_t off[1] = {ic}, cnt[1] = {1};
      H5Sselect_hyperslab(fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
      H5Dread(dset, dtype, msp, fsp, H5P_DEFAULT, row.get());

      if (first) {
        for (int b = 0; b < 3; ++b) t0_per_board[b] = row->time[b] * 1e-6;
        for (int b = 0; b < nb; ++b) {
          if (!st[b]) continue;
          int base[AMORE::kNCHPERADC] = {};
          for (int ch = 0; ch < 16; ++ch) base[ch] = int_baseline(row.get(), b, ch);
          st[b]->SetBaselines(base, AMORE::kNCHPERADC);
        }
        first = false;
      }
      if (ic % 200 == 0)
        std::cerr << "  [" << h5path.substr(h5path.rfind('/') + 1) << "] chunk " << ic << "/"
                  << nchunks << "  \r" << std::flush;

      const long long gs0 = global_offset + (long long)ic * 32768;
      for (int j = 0; j < 32768; ++j) tt[j] = (unsigned long)(gs0 + j) * 10000UL;

      for (int b = 0; b < nb; ++b) {
        if (!st[b]) continue;
        // keep the FIFO a few chunks ahead of the consumer, bounded in memory
        while (st[b]->GetFIFOQueueSize() > 3)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        for (int ch = 0; ch < 16; ++ch)
          for (int j = 0; j < 32768; ++j)
            adc[ch][j] = static_cast<unsigned short>(row->adc[b][ch][j] / 4);
        st[b]->PushChunkData(adcp.data(), tt.data(), 32768);
      }
    }
    std::cerr << "\n";
    global_offset += (long long)nchunks * 32768;
    H5Sclose(msp); H5Sclose(fsp); H5Dclose(dset); H5Fclose(fid);
  }

  producerDone = true;
  for (int b = 0; b < nb; ++b) if (st[b]) st[b]->Stop();
  for (auto & w : workers) w.join();
  H5Tclose(dtype);

  // --- report (same columns as legacy mode + opening bits)
  struct Ev { double trgtime; int board, xid, ch; long long gs; std::uint32_t trgbit; double rv, arv;
              const std::vector<std::uint16_t> * wvf; };
  std::vector<Ev> evs;
  for (int b = 0; b < nb; ++b)
    for (const auto & o : outs[b]) {
      const long long gs = static_cast<long long>(o.rec.ttime / 10000UL);
      const bool openH = (o.rec.trgbit >> TrgBit::kOpenShift) & TrgBit::kRVH;
      const double tt_ms = t0_per_board[b] + gs * ms_per_sample + (openH ? t0_corr_h_ms : t0_corr_l_ms);
      evs.push_back({tt_ms, b, slot2xid[b][o.rec.xtal], 2 * o.rec.xtal, gs, o.rec.trgbit, o.rv, o.arv,
                     &o.rec.phonon});
    }
  std::sort(evs.begin(), evs.end(), [](const Ev & a, const Ev & b) { return a.trgtime < b.trgtime; });

  std::cout << "# board xid ch global_sample trgtime_ms trgbit rv arv openbits\n";
  for (const auto & e : evs)
    std::cout << e.board << ' ' << e.xid << ' ' << e.ch << ' ' << e.gs << ' ' << std::fixed
              << std::setprecision(5) << e.trgtime << ' ' << (e.trgbit & TrgBit::kFiredMask) << ' '
              << std::setprecision(4) << e.rv << ' ' << e.arv << ' '
              << (e.trgbit >> TrgBit::kOpenShift) << '\n';
  std::cerr << "Done (signal mode). Total triggers: " << evs.size() << "\n";

  // --- waveforms: heat channel snapshot taken by SignalTrigger at open (ADC/4)
  std::map<int, std::vector<const Ev *>> by_xid;
  for (const auto & e : evs) by_xid[e.xid].push_back(&e);
  hid_t fout = H5Fcreate(outpath.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  for (auto & [xid, v] : by_xid) {
    char gname[16];
    std::snprintf(gname, sizeof(gname), "xid%02d", xid);
    hid_t grp = H5Gcreate2(fout, gname, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    const hsize_t N = v.size(), RL = cfg.rl;
    std::vector<double> t(N); std::vector<int> tb(N); std::vector<std::uint16_t> w(N * RL);
    for (hsize_t i = 0; i < N; ++i) {
      t[i] = v[i]->trgtime; tb[i] = static_cast<int>(v[i]->trgbit & TrgBit::kFiredMask);
      std::copy(v[i]->wvf->begin(), v[i]->wvf->end(), w.begin() + i * RL);
    }
    hsize_t d1[1] = {N}, d2[2] = {N, RL};
    hid_t s1 = H5Screate_simple(1, d1, nullptr), s2 = H5Screate_simple(2, d2, nullptr);
    hid_t ds;
    ds = H5Dcreate2(grp, "trgtime", H5T_NATIVE_DOUBLE, s1, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, t.data()); H5Dclose(ds);
    ds = H5Dcreate2(grp, "trgbit", H5T_NATIVE_INT, s1, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, tb.data()); H5Dclose(ds);
    ds = H5Dcreate2(grp, "waveform", H5T_NATIVE_UINT16, s2, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, H5T_NATIVE_UINT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, w.data()); H5Dclose(ds);
    H5Sclose(s1); H5Sclose(s2); H5Gclose(grp);
  }
  H5Fclose(fout);
  return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char * argv[])
{
  bool signal_mode = false;
  int  a0 = 1;
  for (; a0 < argc && argv[a0][0] == '-' && argv[a0][1] == '-'; ++a0) {
    const std::string opt = argv[a0];
    if (opt == "--signal")        signal_mode = true;
    else if (opt == "--quantize") g_quantize  = true;
    else { std::cerr << "Unknown option " << opt << "\n"; return 1; }
  }
  if (argc - a0 < 4) {
    std::cerr << "Usage: " << argv[0]
              << " [--signal | --quantize] <config_file> <template_root_file> <output_h5file>"
                 " <h5file1> [h5file2 ...]\n"
                 "  --quantize  legacy drivers get integer ADC (raw/4) and integer baseline\n"
                 "  --signal    replay through ChunkDataFIFO -> SignalTrigger (DAQ path)\n";
    return 1;
  }

  const std::string confpath = argv[a0];
  const std::string tmplpath = argv[a0 + 1];
  const std::string outpath  = argv[a0 + 2];
  std::vector<std::string> h5paths;
  for (int i = a0 + 3; i < argc; ++i) h5paths.push_back(argv[i]);

  ParsedConfig cfg;
  if (!parse_config(confpath, cfg)) return 1;
  cfg.tmplfile = tmplpath;

  if (signal_mode) {
    print_config(cfg);
    return run_signal_mode(cfg, h5paths, outpath);
  }

  print_config(cfg);
  std::cerr << "Output: " << outpath << "\n";
  std::cerr << "Input files (" << h5paths.size() << "):\n";
  for (const auto & p : h5paths) std::cerr << "  " << p << "\n";

  const int NWIN_H = cfg.nh / cfg.njh;
  const int NWIN_L = cfg.nl / cfg.njl;

  const double ms_per_sample = 1000.0 / 100000.0;
  const int NTRG4  = NWIN_H - NWIN_L;
  const int deq_H  = (cfg.rl - cfg.delay) / cfg.njh;
  const int deq_L  = (cfg.rl - cfg.delay) / cfg.njl;
  // Subtract 1 raw sample to match read_amod.C's dead-time counter offset:
  // read_amod emits at nprocessed = detection + fDT - 1, so trgtime = t0 + (detection-1)*ms.
  const double t0_corr_h_ms = ((deq_H - 3) * cfg.njh - 1) * ms_per_sample;
  const double t0_corr_l_ms = ((deq_L - NTRG4 - 1) * cfg.njl - 1) * ms_per_sample;
  std::cerr << "  t0_corr heat=" << t0_corr_h_ms
            << " ms  light=" << t0_corr_l_ms << " ms\n";

  // --- Heat trigger drivers ---
  RVTriggerDriver * drv[3]  = {};
  AMOREADCConf    * conf[3] = {};

  for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
    conf[b] = new AMOREADCConf();
    conf[b]->SetSR(10);  // [us / sample] = 100 kHz
    conf[b]->SetRL(cfg.rl);
    conf[b]->SetDLY(cfg.delay);
    conf[b]->SetBWFOrder(cfg.bwforder);
    conf[b]->SetBWFLC(cfg.bwflc);
    conf[b]->SetBWFUC(cfg.bwfuc);
    conf[b]->SetRVDS(cfg.njh);
    conf[b]->SetRVNWin(NWIN_H);
    conf[b]->SetRVTmpltFile(cfg.tmplfile);

    for (const auto & ci : cfg.boards[b]) {
      if (!ci.trgon || ci.phonon_ch < 0) continue;
      conf[b]->SetTRGON(ci.phonon_ch, 1);
      conf[b]->SetTHR(ci.phonon_ch, ci.thr);
      conf[b]->SetDT(ci.phonon_ch, cfg.dt);
      conf[b]->SetCID(ci.phonon_ch, ci.xid);
      conf[b]->SetRVTHRY(ci.phonon_ch, ci.thry);
      conf[b]->SetRVTHRX(ci.phonon_ch, ci.thrx);
    }

    drv[b] = new RVTriggerDriver(("RVTrig_H_B" + std::to_string(b)).c_str());
    drv[b]->SetConfig(conf[b]);
    if (!drv[b]->Prepare()) {
      std::cerr << "Board " << b << ": heat Prepare() failed.\n"; return 1;
    }
    std::cerr << "Board " << b << " heat: ready.\n";
  }

  // --- Light trigger drivers ---
  RVTriggerDriver * drv_l[3]  = {};
  AMOREADCConf    * conf_l[3] = {};
  bool any_rvl = false;

  for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
    bool board_has_rvl = false;
    for (const auto & ci : cfg.boards[b])
      if (ci.trgon_rvl && ci.photon_ch >= 0) { board_has_rvl = true; break; }
    if (!board_has_rvl) continue;

    conf_l[b] = new AMOREADCConf();
    conf_l[b]->SetSR(10);  // [us / sample] = 100 kHz
    conf_l[b]->SetRL(cfg.rl);
    conf_l[b]->SetDLY(cfg.delay);
    conf_l[b]->SetBWFOrder(cfg.bwfl_order);
    conf_l[b]->SetBWFLC(cfg.bwfl_lc);
    conf_l[b]->SetBWFUC(cfg.bwfl_uc);
    conf_l[b]->SetRVDS(cfg.njl);
    conf_l[b]->SetRVNWin(NWIN_L);
    conf_l[b]->SetRVTmpltFile(cfg.tmplfile);
    conf_l[b]->SetRVTmpltHistPfx("ltmp");
    conf_l[b]->SetRVTmpltHistPfxFR50("htmp");  // use heat fr50 for light template — matches read_amod.C

    for (const auto & ci : cfg.boards[b]) {
      if (!ci.trgon_rvl || ci.photon_ch < 0) continue;
      conf_l[b]->SetTRGON(ci.photon_ch, 1);
      conf_l[b]->SetTHR(ci.photon_ch, ci.thr_rvl);
      conf_l[b]->SetDT(ci.photon_ch, cfg.dt);
      conf_l[b]->SetCID(ci.photon_ch, ci.xid);
    }

    drv_l[b] = new RVTriggerDriver(("RVTrig_L_B" + std::to_string(b)).c_str());
    drv_l[b]->SetConfig(conf_l[b]);
    if (!drv_l[b]->Prepare()) {
      std::cerr << "Board " << b << ": light Prepare() failed.\n"; return 1;
    }
    std::cerr << "Board " << b << " light: ready.\n";
    any_rvl = true;
  }
  if (!any_rvl) std::cerr << "  (no RVL channels configured)\n";

  hid_t dtype = make_h5_dtype();

  // --- Per-XID dead-time state (persists across files) ---
  std::map<int, XIDState> xid_states;
  for (int b = 0; b < cfg.namoreadc && b < 3; ++b)
    for (const auto & ci : cfg.boards[b])
      xid_states[ci.xid] = {};

  std::cout << "# board xid ch global_sample trgtime_ms trgbit rv arv\n";

  long long ntrg = 0;
  std::vector<TrigEvent> events;
  std::vector<FileDesc>  file_descs;

  double    t0_per_board[3] = {};  // per-board start time [ms], mirrors t0[] in read_amod.C
  double    t0_ms          = 0.0; // = t0_per_board[0], used as elapsed reference
  long long global_offset  = 0;   // raw samples processed before current file
  bool      first_file     = true;
  auto * row = new ContData;

  // --- Pass 1: iterate over all input files ---
  for (const auto & h5path : h5paths) {
    hid_t fid = H5Fopen(h5path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (fid < 0) { std::cerr << "Cannot open: " << h5path << "\n"; return 1; }

    hid_t dset = H5Dopen2(fid, "rawdata", H5P_DEFAULT);
    hid_t fsp  = H5Dget_space(dset);
    hsize_t nchunks = 0;
    H5Sget_simple_extent_dims(fsp, &nchunks, nullptr);
    std::cerr << "File: " << h5path << "  chunks=" << nchunks
              << "  global_offset=" << global_offset << "\n";

    // Register this file in the descriptor list
    file_descs.push_back({h5path, global_offset, nchunks});

    hsize_t memdims[1] = {1};
    hid_t msp = H5Screate_simple(1, memdims, nullptr);

    if (first_file) {
      // Baseline and t0 from first file's first chunk only
      hsize_t off[1] = {0}, cnt[1] = {1};
      H5Sselect_hyperslab(fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
      H5Dread(dset, dtype, msp, fsp, H5P_DEFAULT, row);
      for (int b = 0; b < 3; ++b)
        t0_per_board[b] = row->time[b] * 1e-6;
      t0_ms = t0_per_board[0];

      for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
        double baseline_hd[AMORE::kNCHPERADC] = {};
        double baseline_ld[AMORE::kNCHPERADC] = {};
        for (const auto & ci : cfg.boards[b]) {
          if (ci.trgon && ci.phonon_ch >= 0)
            baseline_hd[ci.phonon_ch] = g_quantize ? int_baseline(row, b, ci.phonon_ch)
                                                   : float_baseline(row, b, ci.phonon_ch);
          if (ci.trgon_rvl && ci.photon_ch >= 0)
            baseline_ld[ci.photon_ch] = g_quantize ? int_baseline(row, b, ci.photon_ch)
                                                   : float_baseline(row, b, ci.photon_ch);
        }
        if (drv[b])   drv[b]->SetBaselinesD(baseline_hd, AMORE::kNCHPERADC);
        if (drv_l[b]) drv_l[b]->SetBaselinesD(baseline_ld, AMORE::kNCHPERADC);
      }
      first_file = false;
    }

    // Process all chunks in this file
    for (hsize_t ic = 0; ic < nchunks; ++ic) {
      hsize_t off[1] = {ic}, cnt[1] = {1};
      H5Sselect_hyperslab(fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
      H5Dread(dset, dtype, msp, fsp, H5P_DEFAULT, row);

      if (ic % 200 == 0)
        std::cerr << "  [" << h5path.substr(h5path.rfind('/')+1) << "] chunk "
                  << ic << "/" << nchunks << "  total_trg=" << ntrg << "  \r" << std::flush;

      for (int jbin = 0; jbin < 32768; ++jbin) {
        const long long gs          = global_offset + (long long)ic * 32768 + jbin;
        const double    elapsed_raw = gs * ms_per_sample;  // raw sample offset from run start

        // Step 1: eval all channels
        for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
          for (const auto & ci : cfg.boards[b]) {

            if (ci.trgon && ci.phonon_ch >= 0 && drv[b]) {
              double val_h  = g_quantize ? double(row->adc[b][ci.phonon_ch][jbin] / 4)
                                         : row->adc[b][ci.phonon_ch][jbin] * 0.25;
              bool fired_h  = drv[b]->EvalF(ci.phonon_ch, val_h);
              if (fired_h) {
                double trgtime = t0_per_board[b] + elapsed_raw + t0_corr_h_ms;
                auto & xs = xid_states[ci.xid];
                if (xs.dtcnt == 0)
                  xs = {cfg.dt, 1, trgtime, b, ci.phonon_ch, gs};
                else
                  xs.trgbit |= 1;
              }
            }

            if (ci.trgon_rvl && ci.photon_ch >= 0 && drv_l[b]) {
              double val_l  = g_quantize ? double(row->adc[b][ci.photon_ch][jbin] / 4)
                                         : row->adc[b][ci.photon_ch][jbin] * 0.25;
              bool fired_l  = drv_l[b]->EvalF(ci.photon_ch, val_l);
              if (fired_l) {
                double trgtime = t0_per_board[b] + elapsed_raw + t0_corr_l_ms;
                auto & xs = xid_states[ci.xid];
                if (xs.dtcnt == 0)
                  xs = {cfg.dt, 4, trgtime, b, ci.phonon_ch, gs};
                else
                  xs.trgbit |= 4;
              }
            }
          }
        }

        // Step 2: decrement dead-time; emit completed events
        for (auto & [xid, xs] : xid_states) {
          if (xs.dtcnt > 0) {
            --xs.dtcnt;
            if (xs.dtcnt == 0) {
              ++ntrg;
              double rv = 0, arv = 0;
              for (int b2 = 0; b2 < cfg.namoreadc && b2 < 3; ++b2)
                for (const auto & ci2 : cfg.boards[b2]) {
                  if (ci2.xid != xid) continue;
                  if ((xs.trgbit & 1) && ci2.trgon && ci2.phonon_ch >= 0 && drv[b2]) {
                    rv  = drv[b2]->GetLastRV(ci2.phonon_ch);
                    arv = drv[b2]->GetLastARV(ci2.phonon_ch);
                  } else if ((xs.trgbit & 4) && ci2.trgon_rvl && ci2.photon_ch >= 0 && drv_l[b2]) {
                    rv  = drv_l[b2]->GetLastRV(ci2.photon_ch);
                    arv = drv_l[b2]->GetLastARV(ci2.photon_ch);
                  }
                }

              std::cout << xs.board << ' ' << xid << ' ' << xs.ch
                        << ' ' << xs.global_sample
                        // 5 decimals (= 0.01 ns) so trgtime is not quantized below the
                        // intrinsic μs grid; setprecision(2) here rounded to 10 μs and
                        // produced spurious ±5/+1/+2 μs residuals vs Ofl full precision.
                        << ' ' << std::fixed << std::setprecision(5) << xs.trgtime
                        << ' ' << xs.trgbit
                        << ' ' << std::setprecision(4) << rv
                        << ' ' << arv
                        << '\n';
              events.push_back({xs.trgtime, xs.board, xid, xs.ch,
                                xs.global_sample, xs.trgbit});
              xs = XIDState{};
            }
          }
        }
      }
    }

    std::cerr << "\n";
    global_offset += (long long)nchunks * 32768;

    H5Sclose(msp);
    H5Sclose(fsp);
    H5Dclose(dset);
    H5Fclose(fid);
  }

  std::cerr << "Done. Total triggers: " << ntrg
            << " across " << h5paths.size() << " file(s).\n";

  std::sort(events.begin(), events.end(),
            [](const TrigEvent & a, const TrigEvent & b){
              return a.trgtime_ms < b.trgtime_ms;
            });

  // --- Pass 2: waveforms ---
  write_waveforms(outpath, events, cfg, file_descs, dtype);

  delete row;
  H5Tclose(dtype);
  for (int b = 0; b < 3; ++b) {
    delete drv[b];   delete conf[b];
    delete drv_l[b]; delete conf_l[b];
  }
  return 0;
}
