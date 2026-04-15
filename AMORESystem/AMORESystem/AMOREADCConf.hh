#pragma once

#include <string>
#include <vector>

#include "AMORE/amoreconsts.hh"
#include "DAQConfig/AbsConf.hh"

class AMOREADCConf : public AbsConf {
public:
  AMOREADCConf() = default;
  explicit AMOREADCConf(int sid);
  ~AMOREADCConf() override = default;

  void SetNCH(int val);
  void SetCID(int ch, int val) override;
  void SetPID(int ch, int val) override;
  void SetTRGON(int ch, int val);
  void SetDT(int ch, int val);
  void SetTHR(int ch, int val);
  void SetSR(int val);
  void SetRL(int val);
  void SetDLY(int val);
  void SetZSU(int val);
  void SetPTRG(int val);
  void SetSlopeLB(int ch, int val);
  void SetSlopeDT(int ch, int val);
  void SetTRGMODE(const char * mode);   // 단일 모드 설정 (기존 목록 초기화)
  void AddTRGMODE(const char * mode);   // 모드 추가

  int NCH() const;
  int CID(int ch) const override;
  int PID(int ch) const override;
  int TRGON(int ch) const;
  int DT(int ch) const;
  int THR(int ch) const;
  int SR() const;
  int RL() const;
  int DLY() const;
  int ZSU() const;
  int PTRG() const;
  int SlopeLookBack(int ch) const;  // returns 200 if not set
  int SlopeDeadtime(int ch) const;  // returns 300 if not set
  const std::vector<std::string> & TRGMODEs() const;

  void PrintConf() const override;

private:
  int fNCH{AMORE::kNCHPERADC};
  int fSR{};
  int fRL{};
  int fDLY{};
  int fZSU{};
  int fPTRG{};
  int fCID[AMORE::kNCHPERADC]{};
  int fPID[AMORE::kNCHPERADC]{};
  int fTRGON[AMORE::kNCHPERADC]{};
  int fDT[AMORE::kNCHPERADC]{};
  int fTHR[AMORE::kNCHPERADC]{};
  int fSlopeLB[AMORE::kNCHPERADC]{};  // pile-up: slope look-back window [samples]
  int fSlopeDT[AMORE::kNCHPERADC]{};  // pile-up: slope deadtime [samples]
  
  std::vector<std::string> fTRGMODEs{};

  ClassDef(AMOREADCConf, 1)
};

inline void AMOREADCConf::SetNCH(int val) { fNCH = val; }

inline void AMOREADCConf::SetSR(int val) { fSR = val; }

inline void AMOREADCConf::SetRL(int val) { fRL = val; }

inline void AMOREADCConf::SetDLY(int val) { fDLY = val; }

inline void AMOREADCConf::SetZSU(int val) { fZSU = val; }

inline void AMOREADCConf::SetPTRG(int val) { fPTRG = val; }

inline void AMOREADCConf::SetTRGMODE(const char * mode) { fTRGMODEs = {mode}; }
inline void AMOREADCConf::AddTRGMODE(const char * mode) { fTRGMODEs.push_back(mode); }
inline const std::vector<std::string> & AMOREADCConf::TRGMODEs() const { return fTRGMODEs; }

inline void AMOREADCConf::SetCID(int ch, int val) { fCID[ch] = val; }

inline void AMOREADCConf::SetPID(int ch, int val) { fPID[ch] = val; }

inline void AMOREADCConf::SetTRGON(int ch, int val) { fTRGON[ch] = val; }

inline void AMOREADCConf::SetDT(int ch, int val) { fDT[ch] = val; }

inline void AMOREADCConf::SetTHR(int ch, int val) { fTHR[ch] = val; }

inline void AMOREADCConf::SetSlopeLB(int ch, int val) { fSlopeLB[ch] = val; }

inline void AMOREADCConf::SetSlopeDT(int ch, int val) { fSlopeDT[ch] = val; }

// default 200/300 samples (~2/3 ms at SR=10) if not configured in yml
inline int AMOREADCConf::SlopeLookBack(int ch) const { return fSlopeLB[ch] > 0 ? fSlopeLB[ch] : 200; }

inline int AMOREADCConf::SlopeDeadtime(int ch) const { return fSlopeDT[ch] > 0 ? fSlopeDT[ch] : 300; }

inline int AMOREADCConf::NCH() const { return fNCH; }

inline int AMOREADCConf::SR() const { return fSR; }

inline int AMOREADCConf::RL() const { return fRL; }

inline int AMOREADCConf::DLY() const { return fDLY; }

inline int AMOREADCConf::ZSU() const { return fZSU; }

inline int AMOREADCConf::PTRG() const { return fPTRG; }


inline int AMOREADCConf::CID(int ch) const { return fCID[ch]; }

inline int AMOREADCConf::PID(int ch) const { return fPID[ch]; }

inline int AMOREADCConf::TRGON(int ch) const { return fTRGON[ch]; }

inline int AMOREADCConf::DT(int ch) const { return fDT[ch]; }

inline int AMOREADCConf::THR(int ch) const { return fTHR[ch]; }
