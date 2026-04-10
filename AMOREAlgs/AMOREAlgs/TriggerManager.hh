#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMORESystem/AMOREADCConf.hh"
#include "DAQConfig/AbsConfList.hh"

// Alias for a function that creates a specific trigger instance
using TriggerCreator = std::function<std::unique_ptr<AbsSWTrigger>(const char *)>;

class TriggerManager {
public:
  TriggerManager();
  ~TriggerManager() = default;

  // Register a new trigger type to the factory registry
  void RegisterTriggerType(const std::string & typeName, TriggerCreator creator);

  // Build the heterogeneous trigger pool based on the configuration list
  // Note: Pass the ADC::TYPE (e.g., ADC::kAMOREADC) to filter correct configs
  bool BuildTriggers(const AbsConfList * confList);

  // Get all triggers assigned to a specific ADC index
  const std::vector<std::unique_ptr<AbsSWTrigger>> & GetTriggers(int adcIndex) const;

  // Prepare all initialized triggers in the pool
  bool PrepareAll();

private:
  // Map of available trigger types (Name -> Creation Function)
  std::map<std::string, TriggerCreator> fRegistry;

  // The active pool: one inner vector per ADC, each containing its trigger path(s)
  std::vector<std::vector<std::unique_ptr<AbsSWTrigger>>> fActivePool;

  // Empty vector returned when adcIndex is out of range
  std::vector<std::unique_ptr<AbsSWTrigger>> fEmpty;
};