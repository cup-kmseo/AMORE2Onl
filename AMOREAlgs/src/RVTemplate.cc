#include "TFile.h"
#include "TH1D.h"

#include "AMOREAlgs/RVTemplate.hh"

RVTemplateFile::~RVTemplateFile()
{
  if (fFile) {
    fFile->Close();
    delete fFile;
  }
}

bool RVTemplateFile::Open(const std::string & path)
{
  if (path.empty()) {
    fError = "no template file specified (RV_TMPLT)";
    return false;
  }
  fFile = TFile::Open(path.c_str(), "READ");
  if (!fFile || fFile->IsZombie()) {
    fError = "cannot open template file: " + path;
    return false;
  }
  return true;
}

bool RVTemplateFile::Load(const std::string & pfx, int xid, std::vector<double> & out)
{
  if (!fFile) {
    fError = "template file not open";
    return false;
  }
  const char * name = Form("%s%02d", pfx.c_str(), xid);
  auto * hh = dynamic_cast<TH1D *>(fFile->Get(name));
  if (!hh) {
    fError = std::string("histogram ") + name + " not found";
    return false;
  }
  const int nbins = hh->GetNbinsX();
  out.resize(nbins);
  for (int i = 0; i < nbins; ++i)
    out[i] = 0.25 * hh->GetBinContent(i + 1);
  return true;
}
