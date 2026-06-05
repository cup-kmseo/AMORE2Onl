// rv_trigger_test.cc
// Usage: rv_trigger_test <h5file> <config_file> [template_root_file [output_h5file]]
//
// Replays AMoRE-I continuous HDF5 data (AMORECONT_XXXXXX.h5.NNNNN) through
// RValueTrigger (phonon/heat channels only), printing trigger events to stdout
// and writing triggered waveforms to an HDF5 output file.
//
// config_file: AMoRE-I offline trigger config (e.g. 000664.config or 000644.config).
//   Both keyword formats are supported:
//     old: BWFH order lc uc  /  NH nwin njh  /  THR_RVH ...
//     new: FORDER n + FCUTOFFH lc uc  /  NH nwin + NJH njh  /  THRH_RV ...
//
// template_root_file: overrides TMPRUN in config (optional; uses rv_trig/T<run>.root
//   convention if omitted but TMPRUN is set — specify explicitly for portability).
//
// output_h5file: HDF5 waveform output (default: <h5file>.wvf.h5).
//   Structure per triggered XID:
//     /xid{NN}/trgtime  [N]       float64  trigger time (ms from file start)
//     /xid{NN}/waveform [N, RL]   uint16   ADC/4, trigger at sample DELAY

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "hdf5.h"

#include "AMORE/amoreconsts.hh"
#include "AMOREAlgs/RValueTrigger.hh"
#include "AMORESystem/AMOREADCConf.hh"

// ---------------------------------------------------------------------------
// Thin subclass: exposes protected EvalChannel for direct per-sample calls.
// ---------------------------------------------------------------------------
class RVTriggerDriver : public RValueTrigger {
public:
  explicit RVTriggerDriver(const char * name) : RValueTrigger(name) {}
  bool Eval(int ch, unsigned short v) { return EvalChannel(ch, v); }
  double PeakRV(int ch)  const { return GetRVPeak(ch); }
  double PeakARV(int ch) const { return GetARVPeak(ch); }
};

// ---------------------------------------------------------------------------
// Config parsing
// ---------------------------------------------------------------------------
struct CrystalInfo {
  int  xid;       // 0-indexed crystal ID  (for htmpXX template)
  int  phonon_ch; // 0-indexed channel within board
  bool trgon;     // heat trigger enabled
  int  thr;       // heat RV threshold (integer; trigger fires when RV > thr*1e-3)
  int  thry{0};   // ellipse RV²  axis (THR_RVY / THRH_RVY); 0 = disabled
  int  thrx{0};   // ellipse ARV² axis (THR_RVX / THRH_RVX); 0 = disabled
};

struct ParsedConfig {
  // Global params
  int    bwforder{2};
  double bwflc{150.0};
  double bwfuc{2000.0};
  int    nh{800};    // template window [original samples]
  int    njh{8};     // heat downsampling factor  => NWIN = nh/njh, DS = njh
  int    rl{30000};
  int    delay{13000};
  int    dt{1000};
  int    namoreadc{3};
  int    tmprun{-1};
  std::string tmplfile{};  // resolved template path

  std::vector<CrystalInfo> boards[3];
};

static bool parse_config(const std::string & path, ParsedConfig & cfg)
{
  std::ifstream f(path);
  if (!f) { std::cerr << "Cannot open config: " << path << "\n"; return false; }

  std::string line, tok;
  bool past_stop = false;
  int  cur_board = -1;

  // temporary per-board accumulators (filled as keywords arrive)
  std::vector<int> xids[3], cidh[3], cidl[3], trgon[3], thr_rvh[3], thr_rvy[3], thr_rvx[3];

  auto strip_comment = [](std::string & s) {
    auto p = s.find('#');
    if (p != std::string::npos) s.resize(p);
  };

  while (std::getline(f, line)) {
    strip_comment(line);
    std::istringstream iss(line);
    if (!(iss >> tok) || tok.empty()) continue;

    if (!past_stop) {
      // --- header section (before STOP) ---
      if (tok == "STOP") { past_stop = true; continue; }

      if (tok == "BWFH") {          // old: BWFH order lc uc
        iss >> cfg.bwforder >> cfg.bwflc >> cfg.bwfuc;
      } else if (tok == "FORDER") { // new: FORDER order
        iss >> cfg.bwforder;
      } else if (tok == "FCUTOFFH") { // new: FCUTOFFH lc uc
        iss >> cfg.bwflc >> cfg.bwfuc;
      } else if (tok == "NH") {
        // old: NH nwin njh   new: NH nwin (NJH on separate line)
        iss >> cfg.nh;
        int tmp;
        if (iss >> tmp) cfg.njh = tmp;  // old style has njh on same line
      } else if (tok == "NJH") {
        iss >> cfg.njh;
      } else if (tok == "RL")         { iss >> cfg.rl;
      } else if (tok == "DELAY")      { iss >> cfg.delay;
      } else if (tok == "DT")         { iss >> cfg.dt;
      } else if (tok == "NAMOREADC")  { iss >> cfg.namoreadc;
      } else if (tok == "TMPRUN")     { iss >> cfg.tmprun;
      }
      continue;
    }

    // --- per-ADC section (after STOP) ---
    if (tok == "AMOREADC") {
      int mid, nch; iss >> mid >> nch;
      if (mid >= 0 && mid < 3) cur_board = mid;
      continue;
    }

    if (cur_board < 0) continue;

    if (tok == "XID") {
      int v; while (iss >> v) xids[cur_board].push_back(v - 1);
    } else if (tok == "CID") {
      // CID pairs: phonon photon phonon photon ...
      int v;
      while (iss >> v) {
        // push alternating into cidh / cidl
        if (cidh[cur_board].size() == cidl[cur_board].size())
          cidh[cur_board].push_back(v - 1);
        else
          cidl[cur_board].push_back(v - 1);
      }
    } else if (tok == "TRGON") {
      int v; while (iss >> v) trgon[cur_board].push_back(v);
    } else if (tok == "THRH_RV" || tok == "THR_RVH") {
      int v; while (iss >> v) thr_rvh[cur_board].push_back(v);
    } else if (tok == "THR_RVY") {
      int v; while (iss >> v) thr_rvy[cur_board].push_back(v);
    } else if (tok == "THR_RVX") {
      int v; while (iss >> v) thr_rvx[cur_board].push_back(v);
    }
    // THRL_RV / THR_RVL / THRH_AM / NOISE60 etc. ignored (phonon-only test)
  }

  // Build CrystalInfo per board
  for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
    const int nxtal = static_cast<int>(xids[b].size());
    for (int j = 0; j < nxtal; ++j) {
      CrystalInfo ci;
      ci.xid       = xids[b][j];
      ci.phonon_ch = (j < static_cast<int>(cidh[b].size())) ? cidh[b][j] : -1;
      ci.trgon     = (j < static_cast<int>(trgon[b].size())) ? (trgon[b][j] & 1) : false;
      ci.thr       = (j < static_cast<int>(thr_rvh[b].size())) ? thr_rvh[b][j] : 900;
      ci.thry      = (j < static_cast<int>(thr_rvy[b].size())) ? thr_rvy[b][j] : 0;
      ci.thrx      = (j < static_cast<int>(thr_rvx[b].size())) ? thr_rvx[b][j] : 0;
      if (ci.phonon_ch < 0 || ci.xid < 0 || !ci.trgon) continue;
      cfg.boards[b].push_back(ci);
    }
  }

  return true;
}

static void print_config(const ParsedConfig & cfg)
{
  std::cerr << "--- Config ---\n";
  std::cerr << "  BWF heat: order=" << cfg.bwforder
            << " lc=" << cfg.bwflc << " uc=" << cfg.bwfuc << " Hz\n";
  std::cerr << "  NH=" << cfg.nh << " NJH=" << cfg.njh
            << "  -> NWIN=" << cfg.nh/cfg.njh << " DS=" << cfg.njh << "\n";
  std::cerr << "  RL=" << cfg.rl << " DELAY=" << cfg.delay << " DT=" << cfg.dt << "\n";
  std::cerr << "  Template: " << cfg.tmplfile << "\n";
  for (int b = 0; b < 3; ++b) {
    std::cerr << "  Board " << b << ": " << cfg.boards[b].size() << " phonon channels\n";
    for (const auto & ci : cfg.boards[b])
      std::cerr << "    ch=" << ci.phonon_ch << " xid=" << ci.xid
                << " thr=" << ci.thr
                << " thry=" << ci.thry << " thrx=" << ci.thrx << "\n";
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
// Trigger event (collected during pass 1)
// ---------------------------------------------------------------------------
struct TrigEvent {
  double  trgtime_ms;
  int     board, xid, ch;
  hsize_t ic;   // chunk index of trigger sample
  int     jbin; // sample index within chunk
};

// ---------------------------------------------------------------------------
// HDF5 waveform output (pass 2)
// ---------------------------------------------------------------------------
static void write_waveforms(
    const std::string         & outpath,
    const std::vector<TrigEvent> & events,
    const ParsedConfig        & cfg,
    hid_t                       in_dset,
    hid_t                       in_fsp,
    hid_t                       in_dtype,
    hsize_t                     nchunks)
{
  std::cerr << "Writing waveforms -> " << outpath << "\n";

  // --- 2-slot chunk cache (heap-allocated: ContData is ~6 MB each) ---
  struct Slot { hsize_t idx{UINT64_MAX}; ContData * data{nullptr}; };
  Slot cache[2];
  cache[0].data = new ContData;
  cache[1].data = new ContData;
  int next_slot = 0;

  hsize_t cache_memdims[1] = {1};
  hid_t   cache_msp = H5Screate_simple(1, cache_memdims, nullptr);

  auto get_chunk = [&](hsize_t ci) -> const ContData * {
    for (auto & s : cache)
      if (s.idx == ci) return s.data;
    auto & s  = cache[next_slot];
    s.idx     = ci;
    hsize_t off[1] = {ci}, cnt[1] = {1};
    H5Sselect_hyperslab(in_fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
    H5Dread(in_dset, in_dtype, cache_msp, in_fsp, H5P_DEFAULT, s.data);
    next_slot = 1 - next_slot;
    return s.data;
  };

  // --- Group events by xid (events are already sorted by trgtime) ---
  std::map<int, std::vector<const TrigEvent *>> by_xid;
  for (const auto & ev : events)
    by_xid[ev.xid].push_back(&ev);

  // --- Create output HDF5 ---
  hid_t fout = H5Fcreate(outpath.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

  const int RL = cfg.rl;

  // mem space for writing one waveform row
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

    // trgtime dataset [N] float64
    {
      hsize_t dims[1] = {(hsize_t)N};
      hid_t sp = H5Screate_simple(1, dims, nullptr);
      hid_t ds = H5Dcreate2(grp, "trgtime", H5T_NATIVE_DOUBLE,
                             sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      std::vector<double> t(N);
      for (int i = 0; i < N; ++i) t[i] = evs[i]->trgtime_ms;
      H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, t.data());
      H5Dclose(ds);
      H5Sclose(sp);
    }

    // waveform dataset [N, RL] uint16 (deflate compressed, chunk = 1 waveform)
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
      H5Pclose(plist);
      H5Sclose(sp);
    }

    for (int i = 0; i < N; ++i) {
      const TrigEvent & ev = *evs[i];
      // global sample index of trigger
      long long gs = (long long)ev.ic * 32768 + ev.jbin;

      // Waveform window: pre-trigger = DELAY, post-trigger = RL - DELAY.
      // Trigger sits at sample DELAY, giving enough post-trigger to see the full decay.
      long long wstart = gs - cfg.delay;

      for (int s = 0; s < RL; ++s) {
        long long gsi = wstart + s;
        if (gsi < 0) {
          wvf[s] = 0;
          continue;
        }
        hsize_t ci = (hsize_t)(gsi / 32768);
        int     si = (int)(gsi % 32768);
        if (ci >= nchunks) {
          wvf[s] = 0;
          continue;
        }
        wvf[s] = (uint16_t)(get_chunk(ci)->adc[ev.board][ev.ch][si] / 4);
      }

      hsize_t start[2] = {(hsize_t)i, 0};
      hsize_t count[2] = {1, (hsize_t)RL};
      H5Sselect_hyperslab(wvf_fsp, H5S_SELECT_SET, start, nullptr, count, nullptr);
      H5Dwrite(wvf_ds, H5T_NATIVE_UINT16, wvf_msp, wvf_fsp, H5P_DEFAULT, wvf.data());
    }

    H5Sclose(wvf_fsp);
    H5Dclose(wvf_ds);
    H5Gclose(grp);

    nwvf_total += N;
    ++nxid_written;
    std::cerr << "  " << gname << ": " << N << " waveforms\n";
  }

  H5Sclose(wvf_msp);
  H5Sclose(cache_msp);
  delete cache[0].data;
  delete cache[1].data;
  H5Fclose(fout);

  std::cerr << "Done: " << nxid_written << " XIDs, "
            << nwvf_total << " waveforms -> " << outpath << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char * argv[])
{
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0]
              << " <h5file> <config_file> [template_root_file [output_h5file]]\n";
    return 1;
  }

  const std::string h5path   = argv[1];
  const std::string confpath = argv[2];

  ParsedConfig cfg;
  if (!parse_config(confpath, cfg)) return 1;

  // Template file: prefer explicit 3rd arg, then fall back to TMPRUN
  if (argc >= 4) {
    cfg.tmplfile = argv[3];
  } else if (cfg.tmprun > 0) {
    // try rv_trig/ convention (T<run>.root in same dir as config)
    std::string dir = confpath.substr(0, confpath.rfind('/') + 1);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "T%06d.root", cfg.tmprun);
    cfg.tmplfile = dir + buf;
  } else {
    std::cerr << "No template file: specify as 3rd arg or set TMPRUN in config.\n";
    return 1;
  }

  // Output HDF5: optional 5th arg, otherwise derive from input filename
  std::string outpath;
  if (argc >= 5) {
    outpath = argv[4];
  } else {
    outpath = h5path + ".wvf.h5";
  }

  print_config(cfg);
  std::cerr << "Output: " << outpath << "\n";

  const int NWIN = cfg.nh / cfg.njh;

  // --- One trigger driver per ADC board ---
  RVTriggerDriver * drv[3]  = {};
  AMOREADCConf    * conf[3] = {};

  for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
    conf[b] = new AMOREADCConf();
    conf[b]->SetSR(100000);   // AMoRE-I: 100 kHz
    conf[b]->SetRL(cfg.rl);
    conf[b]->SetDLY(cfg.delay);
    conf[b]->SetBWFOrder(cfg.bwforder);
    conf[b]->SetBWFLC(cfg.bwflc);
    conf[b]->SetBWFUC(cfg.bwfuc);
    conf[b]->SetRVDS(cfg.njh);
    conf[b]->SetRVNWin(NWIN);
    conf[b]->SetRVTmpltFile(cfg.tmplfile);

    for (const auto & ci : cfg.boards[b]) {
      conf[b]->SetTRGON(ci.phonon_ch, 1);
      conf[b]->SetTHR(ci.phonon_ch, ci.thr);
      conf[b]->SetDT(ci.phonon_ch, cfg.dt);
      conf[b]->SetCID(ci.phonon_ch, ci.xid);
      conf[b]->SetRVTHRY(ci.phonon_ch, ci.thry);
      conf[b]->SetRVTHRX(ci.phonon_ch, ci.thrx);
    }

    const std::string name = "RVTrig_B" + std::to_string(b);
    drv[b] = new RVTriggerDriver(name.c_str());
    drv[b]->SetConfig(conf[b]);

    if (!drv[b]->Prepare()) {
      std::cerr << "Board " << b << ": Prepare() failed.\n";
      return 1;
    }
    std::cerr << "Board " << b << ": ready.\n";
  }

  // --- Open HDF5 ---
  hid_t fid = H5Fopen(h5path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  if (fid < 0) { std::cerr << "Cannot open: " << h5path << "\n"; return 1; }

  hid_t dset  = H5Dopen2(fid, "rawdata", H5P_DEFAULT);
  hid_t fsp   = H5Dget_space(dset);
  hid_t dtype = make_h5_dtype();

  hsize_t nchunks = 0;
  H5Sget_simple_extent_dims(fsp, &nchunks, nullptr);
  std::cerr << "File: " << h5path << "  chunks=" << nchunks << "\n";

  hsize_t memdims[1] = {1};
  hid_t msp = H5Screate_simple(1, memdims, nullptr);
  auto * row = new ContData;

  // --- Baseline from first 100 samples of chunk 0 ---
  {
    hsize_t off[1] = {0}, cnt[1] = {1};
    H5Sselect_hyperslab(fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
    H5Dread(dset, dtype, msp, fsp, H5P_DEFAULT, row);

    for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
      int baseline[AMORE::kNCHPERADC] = {};
      for (const auto & ci : cfg.boards[b]) {
        double sum = 0;
        for (int j = 0; j < 100; ++j) sum += row->adc[b][ci.phonon_ch][j] / 4;
        baseline[ci.phonon_ch] = static_cast<int>(sum / 100.0);
      }
      drv[b]->SetBaselines(baseline, AMORE::kNCHPERADC);
    }
  }

  std::cout << "# board xid ch chunk sample elapsed_ms rv arv\n";

  long long ntrg = 0;
  int       dtcnt[3][AMORE::kNCHPERADC] = {};
  const double ms_per_sample = 1000.0 / 100000.0;  // 0.01 ms at 100 kHz

  std::vector<TrigEvent> events;

  // --- Pass 1: trigger algorithm ---
  for (hsize_t ic = 0; ic < nchunks; ++ic) {
    hsize_t off[1] = {ic}, cnt[1] = {1};
    H5Sselect_hyperslab(fsp, H5S_SELECT_SET, off, nullptr, cnt, nullptr);
    H5Dread(dset, dtype, msp, fsp, H5P_DEFAULT, row);

    if (ic % 100 == 0)
      std::cerr << "  chunk " << ic << "/" << nchunks
                << "  triggers=" << ntrg << "  \r" << std::flush;

    // Every 500 chunks (~160 s): print peak RV per channel for diagnostics
    if (ic > 0 && ic % 500 == 0) {
      std::cerr << "\n[diag chunk " << ic << "] peak RV since last trigger:\n";
      for (int b = 0; b < cfg.namoreadc && b < 3; ++b)
        for (const auto & ci : cfg.boards[b])
          std::cerr << "  B" << b << " XID" << std::setw(2) << ci.xid
                    << " ch" << std::setw(2) << ci.phonon_ch
                    << "  rvpeak=" << std::fixed << std::setprecision(4)
                    << drv[b]->PeakRV(ci.phonon_ch)
                    << "  arvpeak=" << drv[b]->PeakARV(ci.phonon_ch)
                    << "  thr=" << ci.thr * 1e-3 << "\n";
    }

    for (int jbin = 0; jbin < 32768; ++jbin) {
      const double elapsed =
          (static_cast<long long>(ic) * 32768 + jbin) * ms_per_sample;

      for (int b = 0; b < cfg.namoreadc && b < 3; ++b) {
        for (const auto & ci : cfg.boards[b]) {
          // Always call EvalChannel to keep the IIR filter state current.
          // read_amod.C processes every sample through the filter; only the
          // trigger decision is gated by the deadtime flag (tbit check).
          auto val   = static_cast<unsigned short>(row->adc[b][ci.phonon_ch][jbin] / 4);
          bool fired = drv[b]->Eval(ci.phonon_ch, val);

          // RV trace dump: board 0 ch 0 & ch 2, every DS-th sample, chunks 50-65
          if (b == 0 && (ci.phonon_ch == 0 || ci.phonon_ch == 2) &&
              ic >= 50 && ic < 65 && jbin % 8 == 0) {
            std::cerr << "RV b" << b << " ch" << ci.phonon_ch
                      << " t=" << std::fixed << std::setprecision(2) << elapsed
                      << " rv=" << std::setprecision(5) << drv[b]->GetCurrentRV(ci.phonon_ch)
                      << " arv=" << drv[b]->GetCurrentARV(ci.phonon_ch)
                      << " adc=" << (int)val << "\n";
          }

          if (dtcnt[b][ci.phonon_ch] > 0) {
            --dtcnt[b][ci.phonon_ch];
          } else if (fired) {
            dtcnt[b][ci.phonon_ch] = cfg.dt;
            ++ntrg;
            std::cout << b << ' ' << ci.xid << ' ' << ci.phonon_ch
                      << ' ' << ic << ' ' << jbin
                      << ' ' << std::fixed << std::setprecision(2) << elapsed
                      << ' ' << std::setprecision(4) << drv[b]->GetLastRV(ci.phonon_ch)
                      << ' ' << drv[b]->GetLastARV(ci.phonon_ch)
                      << '\n';
            events.push_back({elapsed, b, ci.xid, ci.phonon_ch, ic, jbin});
          }
        }
      }
    }
  }

  std::cerr << "\nDone. Total triggers: " << ntrg
            << " in " << nchunks << " chunks.\n";

  // --- Sort by trgtime ---
  std::sort(events.begin(), events.end(),
            [](const TrigEvent & a, const TrigEvent & b){
              return a.trgtime_ms < b.trgtime_ms;
            });

  // --- Pass 2: extract waveforms and write output HDF5 ---
  write_waveforms(outpath, events, cfg, dset, fsp, dtype, nchunks);

  delete row;
  H5Tclose(dtype);
  H5Sclose(msp);
  H5Sclose(fsp);
  H5Dclose(dset);
  H5Fclose(fid);
  for (int b = 0; b < 3; ++b) { delete drv[b]; delete conf[b]; }
  return 0;
}
