#include <iostream>

#include "AMOREDAQ/AMOREDAQManager.hh"
#include "AMOREDAQ/daqopt.hh"
#include "OnlConsts/onlconsts.hh"

int main(int argc, char ** argv)
{
  if (argc < 2) {
    printusage(argv[0]);
    return 0;
  }

  daqopt option;
  option.init();
  optparse(option, argc, argv);

  AMOREDAQManager manager;
  manager.SetDAQType(DAQ::AMORETCB);
  manager.SetConfigFilename(option.config);
  manager.SetRunNumber(option.runnum);
  manager.SetDAQID(option.daqid);
  manager.SetDAQTime(option.daqtime);
  manager.SetNEvent(option.daqevent);
  manager.SetVerboseLevel(option.vlevel);

  manager.Run();

  delete &manager;
  return 0;
}
