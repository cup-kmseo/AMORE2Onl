#include <iostream>

#include "AMOREAlgs/ButterworthTrigger.hh"
#include "AMOREAlgs/RValueTrigger.hh"
#include "AMOREAlgs/RandomTrigger.hh"
#include "AMOREAlgs/TriggerManager.hh"

TriggerManager::TriggerManager()
{
  RegisterTriggerType("RandomTrigger",
                      [](const char * name) { return std::make_unique<RandomTrigger>(name); });

  RegisterTriggerType("ButterworthTrigger",
                      [](const char * name) { return std::make_unique<ButterworthTrigger>(name); });

  RegisterTriggerType("RValueTrigger",
                      [](const char * name) { return std::make_unique<RValueTrigger>(name); });
}

void TriggerManager::RegisterTriggerType(const std::string & typeName, TriggerCreator creator)
{
  fRegistry[typeName] = creator;
}

bool TriggerManager::BuildTriggers(const AbsConfList * confList)
{
  if (!confList) {
    std::cerr << "TriggerManager::BuildTriggers - Configuration list is null!" << std::endl;
    return false;
  }

  fActivePool.clear();

  int nADC = confList->GetNADC(ADC::AMOREADC);
  std::cout << "TriggerManager: Building trigger pool for " << nADC << " ADCs." << std::endl;

  for (int i = 0; i < nADC; ++i) {
    AbsConf * absConf = confList->GetConfig(ADC::AMOREADC, i);
    AMOREADCConf * adcConf = dynamic_cast<AMOREADCConf *>(absConf);

    fActivePool.emplace_back(); // add empty inner vector for this ADC

    if (!adcConf) {
      std::cerr << "TriggerManager::BuildTriggers - Invalid config at ADC index " << i << std::endl;
      continue;
    }

    const auto & modes = adcConf->TRGMODEs();
    for (int j = 0; j < static_cast<int>(modes.size()); ++j) {
      const std::string & trgMode = modes[j];
      std::string trgName = trgMode + "_ADC" + std::to_string(i) + "_path" + std::to_string(j);

      auto it = fRegistry.find(trgMode);
      if (it == fRegistry.end()) {
        std::cerr << "TriggerManager::BuildTriggers - Unknown TRGMODE: '" << trgMode
                  << "' for ADC " << i << std::endl;
        continue;
      }

      auto trigger = it->second(trgName.c_str());
      trigger->SetConfig(adcConf);
      fActivePool[i].push_back(std::move(trigger));
      std::cout << " -> ADC " << i << " path " << j << ": " << trgMode << std::endl;
    }
  }

  return true;
}

const std::vector<std::unique_ptr<AbsSWTrigger>> & TriggerManager::GetTriggers(int adcIndex) const
{
  if (adcIndex < 0 || adcIndex >= static_cast<int>(fActivePool.size())) { return fEmpty; }
  return fActivePool[adcIndex];
}

bool TriggerManager::PrepareAll()
{
  if (fActivePool.empty()) {
    std::cerr << "TriggerManager::PrepareAll - Pool is empty. Call BuildTriggers first."
              << std::endl;
    return false;
  }

  bool allSuccess = true;
  for (int i = 0; i < static_cast<int>(fActivePool.size()); ++i) {
    for (int j = 0; j < static_cast<int>(fActivePool[i].size()); ++j) {
      if (!fActivePool[i][j]->Prepare()) {
        std::cerr << "TriggerManager::PrepareAll - Failed to prepare trigger ADC " << i
                  << " path " << j << std::endl;
        allSuccess = false;
      }
    }
  }

  return allSuccess;
}