#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: test_module
constructor_args: 
  - period_ms: 1000
template_args: []
required_hardware:
  - Buzzer
  - Light
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "timer.hpp"
#include "spi.hpp"
#include "pwm.hpp"
#include <set>
class TestModule : public LibXR::Application {
public:
  TestModule(LibXR::HardwareContainer &hw, LibXR::ApplicationManager &app,uint32_t period_ms)
  : buzzer_(hw.template FindOrExit<LibXR::PWM>({"Buzzer"})),
  light_(hw.template FindOrExit<LibXR::SPI>({"Light"})),
  period_ms_(period_ms)
  {
    // Hardware initialization example:
    // auto dev = hw.template Find<LibXR::GPIO>("led");
    buzzer_->SetConfig({2700});    // ① 2.7kHz，会重算 PSC/ARR
    buzzer_->SetDutyCycle(0.5f);   // ② 用新的 Period 算 CCR
    buzzer_->Disable();            // ③ 先别响

    beep_task_=(LibXR::Timer::CreateTask(BuzzerTick,this,kTickMs));
    light_task_=(LibXR::Timer::CreateTask(LightTick,this,period_ms));
    LibXR::Timer::Add(beep_task_);
    LibXR::Timer::Add(light_task_);
    LibXR::Timer::Start(beep_task_);
    LibXR::Timer::Start(light_task_);
  }
    static constexpr uint32_t kTickMs = 10;    // 节拍
    static constexpr uint32_t kBeepMs = 200;   // 每次响多久

    static void BuzzerTick(TestModule* self) 
    {
      // self->elapsed_ms_ += kTickMs;                       // ① 累计
      // if (self->elapsed_ms_ >= self->period_ms_) {        // ② 到一个周期就归零
      //   self->elapsed_ms_ = 0;
      // }

      // const bool on = self->elapsed_ms_ < kBeepMs;        // ③ 当前该响吗
      // if (on == self->buzzer_on_) {
      //   return;                                           // ④ 状态没变就不动硬件
      // }
      // self->buzzer_on_ = on;
      // if (on) self->buzzer_->Enable();
      // else    self->buzzer_->Disable();
    }
    static void LightTick(TestModule* self) 
    {
      self->led_on_ = !self->led_on_;                     // 每 period_ms 翻转
      if (self->led_on_) self->Ws2812Write(255, 0, 0);     // 绿
      else               self->Ws2812Write(0, 0, 0);      // 灭
    }

    void Ws2812Write(uint8_t r, uint8_t g, uint8_t b) 
    {
      const uint8_t grb[3] = {g, r, b};                   // WS2812 是 GRB 顺序！
      for (uint8_t byte = 0; byte < 3U; byte++) {
        for (uint8_t bit = 0; bit < 8U; bit++) {
          const uint16_t idx = (byte * 8U + bit) * 2U;
          if (((grb[byte] >> (7U - bit)) & 0x1U) != 0U) { frame_[idx] = 0xFF; frame_[idx+1] = 0xE0; }
          else                                         { frame_[idx] = 0xF8; frame_[idx+1] = 0x00; }
        }
      } 
      light_->Write({frame_, 48}, op_);                   // 阻塞发送
    }
  

  void OnMonitor() override {}

private:

  LibXR::PWM* buzzer_;
  LibXR::SPI* light_;
  uint32_t period_ms_;

  LibXR::Timer::TimerHandle beep_task_;
  LibXR::Timer::TimerHandle light_task_;

  bool led_on_ = false;
  bool buzzer_on_ = false;
  uint32_t elapsed_ms_ = 0;
  uint8_t frame_[48];

  LibXR::Semaphore sem_;
  LibXR::SPI::OperationRW op_{sem_};
};