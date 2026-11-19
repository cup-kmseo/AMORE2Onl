#include <iostream>

#include "AMORESystem/AMOREADCConf.hh"

ClassImp(AMOREADCConf)

AMOREADCConf::AMOREADCConf(int sid)
  : AbsConf(sid, ADC::AMOREADC)
{
  SetNameTitle("AMOREADC", "ADC for AMoRE Experiment");

  for (int i = 0; i < fNCH; i++) {
    fCID[i] = i + 1;
    fPID[i] = 0;
  }

  fMID = sid + 128;
}

void AMOREADCConf::PrintConf() const
{
  std::cout << Form(" ++ AMOREADC config: SID(%d) MID(%d) NCH(%d) SR(%d) RL(%d) DLY(%d) ZSU(%d) PTRG(%d)",
                    fSID, fMID, fNCH, fSR, fRL, fDLY, fZSU, fPTRG)
            << std::endl;
  if (fIsEnabled) {
    std::cout << " ++ This AMOREADC is enabled" << std::endl;
    std::cout << " ++ Trigger Mode(s):";
    for (const auto & m : fTRGMODEs) std::cout << " " << m;
    std::cout << std::endl;
  }
  else std::cout << " ++ This AMOREADC is disabled" << std::endl;

  std::cout << " -----------------------------------------------" << std::endl;
  for (int j = 0; j < 2; j++) {
    if (j > 0) std::cout << std::endl;
    std::cout << "        CID : ";
    for (int i = 8 * j; i < 8 * (j + 1); i++)
      std::cout << Form("%6d", fCID[i]);
    std::cout << std::endl;
    std::cout << "        PID : ";
    for (int i = 8 * j; i < 8 * (j + 1); i++)
      std::cout << Form("%6d", fPID[i]);
    std::cout << std::endl;
    std::cout << "      TRGON : ";
    for (int i = 8 * j; i < 8 * (j + 1); i++)
      std::cout << Form("%6d", fTRGON[i]);
    std::cout << std::endl;
    std::cout << "         DT : ";
    for (int i = 8 * j; i < 8 * (j + 1); i++)
      std::cout << Form("%6d", fDT[i]);
    std::cout << std::endl;
    std::cout << "        THR : ";
    for (int i = 8 * j; i < 8 * (j + 1); i++)
      std::cout << Form("%6d", fTHR[i]);
    std::cout << std::endl;
  }
  std::cout << " -----------------------------------------------" << std::endl;
  for (const auto & cc : fTrgConds) {
    std::cout << Form(" ++ TRGCOND %s: type=%s bit=0x%x target=%s", cc.name.c_str(),
                      cc.type.c_str(), cc.bit, cc.target == TrgTarget::kHeat ? "heat" : "light");
    if (cc.type == "RV")
      std::cout << Form(" BWF=(%d, %g, %g) NWIN=%d DS=%d TMPLT=%s FR50=%s", cc.bwfOrder,
                        cc.bwfLC, cc.bwfUC, cc.nwin, cc.ds, cc.tmpltPfx.c_str(),
                        cc.fr50Pfx.empty() ? cc.tmpltPfx.c_str() : cc.fr50Pfx.c_str());
    std::cout << std::endl << "        ON  : ";
    for (int x = 0; x < AMORE::kNXTALPERADC; x++) std::cout << Form("%6d", cc.on[x]);
    std::cout << std::endl << "        THR : ";
    for (int x = 0; x < AMORE::kNXTALPERADC; x++) std::cout << Form("%6d", cc.thr[x]);
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
