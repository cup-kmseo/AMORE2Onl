#include <iostream>
#include <string>

// Include AMORE framework headers
#include "AMOREAlgs/AbsSWTrigger.hh"
#include "AMOREAlgs/TriggerManager.hh"
#include "AMORESystem/AMOREADCConf.hh"
#include "DAQConfig/AbsConfList.hh"

int main()
{
  std::cout << "=========================================" << std::endl;
  std::cout << "  TriggerManager (Factory) Test Program  " << std::endl;
  std::cout << "=========================================\n" << std::endl;

  TriggerManager trgManager;

  // ---------------------------------------------------------
  // [Test 1] Setup Mock Configuration
  // ---------------------------------------------------------
  std::cout << "[Test 1] Setting up mock configurations..." << std::endl;

  AbsConfList confList;

  AMOREADCConf conf0(1);
  conf0.SetTRGMODE("RandomTrigger");
  conf0.SetEnable();
  conf0.SetLink();

  AMOREADCConf conf1(2);
  conf1.SetTRGMODE("ButterworthTrigger");
  conf1.SetEnable();
  conf1.SetLink();

  AMOREADCConf conf2(3);
  conf2.SetTRGMODE("UnknownTriggerType"); // To test error handling
  conf2.SetEnable();
  conf2.SetLink();

  // Add configurations to the list.
  // (Assuming TObjArray::Add() is sufficient for AbsConfList::GetNADC and GetConfig to work.
  // If your framework requires a specific parser to populate AbsConfList, use that here instead.)
  confList.Add(&conf0);
  confList.Add(&conf1);
  confList.Add(&conf2);

  confList.Dump();

  std::cout << confList.GetNADC(ADC::AMOREADC) << std::endl;

  std::cout << "-> Mock configuration list created.\n" << std::endl;

  // ---------------------------------------------------------
  // [Test 2] Build Triggers from Configuration
  // ---------------------------------------------------------
  std::cout << "[Test 2] Building triggers from configuration list..." << std::endl;

  bool buildResult = trgManager.BuildTriggers(&confList);

  if (buildResult) { std::cout << "-> SUCCESS: BuildTriggers returned true.\n" << std::endl; }
  else {
    std::cerr << "-> FAIL: BuildTriggers returned false.\n" << std::endl;
  }

  // ---------------------------------------------------------
  // [Test 3] Verify Assigned Triggers
  // ---------------------------------------------------------
  std::cout << "[Test 3] Verifying assigned triggers in the pool..." << std::endl;

  // Note: This relies on GetNADC() correctly identifying the 3 added configs.
  const auto & trgs0 = trgManager.GetTriggers(0);
  if (!trgs0.empty()) std::cout << "-> ADC 0: Successfully got " << trgs0[0]->GetName() << std::endl;
  else std::cerr << "-> ADC 0: Trigger is null (Check if GetConfig() works with Add())" << std::endl;

  const auto & trgs1 = trgManager.GetTriggers(1);
  if (!trgs1.empty()) std::cout << "-> ADC 1: Successfully got " << trgs1[0]->GetName() << std::endl;
  else std::cerr << "-> ADC 1: Trigger is null" << std::endl;

  const auto & trgs2 = trgManager.GetTriggers(2);
  if (trgs2.empty())
    std::cout << "-> ADC 2: Empty as expected (Unknown trigger type handled properly)." << std::endl;
  else std::cerr << "-> ADC 2: FAIL - Should be empty, but got " << trgs2[0]->GetName() << std::endl;

  std::cout << std::endl;

  // ---------------------------------------------------------
  // [Test 4] Prepare All
  // ---------------------------------------------------------
  std::cout << "[Test 4] Testing PrepareAll()..." << std::endl;

  // We expect this to fail gracefully because we haven't attached an actual ChunkDataFIFO
  // and the AbsSWTrigger::Prepare() checks for fFIFO.
  bool prepResult = trgManager.PrepareAll();

  if (!prepResult) {
    std::cout
        << "-> SUCCESS: PrepareAll returned false as expected (No FIFO attached in this unit test)."
        << std::endl;
  }
  else {
    std::cerr << "-> FAIL: PrepareAll returned true unexpectedly." << std::endl;
  }

  std::cout << "\n=========================================" << std::endl;
  std::cout << "           Test Program Finished         " << std::endl;
  std::cout << "=========================================" << std::endl;

  return 0;
}