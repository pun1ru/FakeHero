#include "app_framework.hpp"
#include "libxr.hpp"

// Module headers
#include "TestModule.hpp"
#include "BMI088.hpp"

static void XRobotMain(LibXR::HardwareContainer &hw) {
  using namespace LibXR;
  ApplicationManager appmgr;

  // Auto-generated module instantiations
  static TestModule TestModule_0(hw, appmgr, 1000);
  static BMI088 bmi088(
      hw,
      appmgr,
      BMI088::GyroFreq::GYRO_2000HZ_BW532HZ,
      BMI088::AcclFreq::ACCL_1600HZ,
      BMI088::GyroRange::DEG_2000DPS,
      BMI088::AcclRange::ACCL_24G,
      {1.0, 0.0, 0.0, 0.0},
      {1.0, 0.35, 0.0, 0.0, 0.0, 0.0, false},
      "bmi088_gyro",
      "bmi088_accl",
      45,
      2048
  );

  while (true) {
    appmgr.MonitorAll();
    Thread::Sleep(1000);
  }
}