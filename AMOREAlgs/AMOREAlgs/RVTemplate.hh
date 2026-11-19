#pragma once

#include <string>
#include <vector>

class TFile;

// Reads R-value template histograms ("<pfx><xid:02d>", TH1D) from a ROOT file
// and returns their contents as full-rate arrays scaled to ADC units (×0.25,
// 32-bit ADC → 14-bit range, as in read_amod.C).
class RVTemplateFile {
public:
  RVTemplateFile() = default;
  ~RVTemplateFile();
  RVTemplateFile(const RVTemplateFile &) = delete;
  RVTemplateFile & operator=(const RVTemplateFile &) = delete;

  bool Open(const std::string & path);
  bool Load(const std::string & pfx, int xid, std::vector<double> & out);

  const std::string & Error() const { return fError; }

private:
  TFile *     fFile{nullptr};
  std::string fError{};
};
