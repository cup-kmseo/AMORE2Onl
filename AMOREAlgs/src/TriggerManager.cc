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

  // Clear previous pool if re-building
  fActivePool.clear();

  int nADC = confList->GetNADC(ADC::AMOREADC);
  std::cout << "TriggerManager: Building trigger pool for " << nADC << " ADCs."
            << std::endl;

  for (int i = 0; i < nADC; ++i) {
    AbsConf * absConf = confList->GetConfig(ADC::AMOREADC, i);
    AMOREADCConf * adcConf = dynamic_cast<AMOREADCConf *>(absConf);

    if (!adcConf) {
      std::cerr << "TriggerManager::BuildTriggers - Invalid config or not an AMOREADCConf at index "
                << i << std::endl;
      fActivePool.push_back(nullptr); // Push nullptr to maintain ADC index alignment
      continue;
    }

    std::string trgMode = adcConf->TRGMODE();
    std::string trgName = trgMode + "_ADC" + std::to_string(i);

    // Look up the requested TRGMODE in the registry
    auto it = fRegistry.find(trgMode);

    if (it != fRegistry.end()) {
      // Instantiate the specific trigger
      auto trigger = it->second(trgName.c_str());

      // Inject the configuration directly into the trigger
      trigger->SetConfig(adcConf);

      fActivePool.push_back(std::move(trigger));
      std::cout << " -> ADC " << i << " assigned: " << trgMode << std::endl;
    }
    else {
      std::cerr << "TriggerManager::BuildTriggers - Unknown TRGMODE: '" << trgMode << "' for ADC "
                << i << ". No trigger assigned." << std::endl;
      fActivePool.push_back(nullptr);
    }
  }

  return true;
}

AbsSWTrigger * TriggerManager::GetTrigger(int adcIndex) const
{
  if (adcIndex < 0 || adcIndex >= static_cast<int>(fActivePool.size())) { return nullptr; }
  return fActivePool[adcIndex].get();
}

bool TriggerManager::PrepareAll()
{
  if (fActivePool.empty()) {
    std::cerr << "TriggerManager::PrepareAll - Pool is empty. Call BuildTriggers first."
              << std::endl;
    return false;
  }

  bool allSuccess = true;
  for (size_t i = 0; i < fActivePool.size(); ++i) {
    if (!fActivePool[i]) continue; // Skip if this ADC had no valid trigger assigned

    if (!fActivePool[i]->Prepare()) {
      std::cerr << "TriggerManager::PrepareAll - Failed to prepare trigger for ADC " << i
                << std::endl;
      allSuccess = false;
    }
  }

  return allSuccess;
}