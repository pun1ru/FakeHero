#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: LK classic CAN motor driver with protocol V2.36 feedback and control
constructor_args: []
template_args: []
required_hardware: []
standalone: false
depends:
- 3SE-xrobot-dev/Motor
=== END MANIFEST === */
// clang-format on

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

#include "Motor.hpp"
#include "can.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

class LKMotor : public Motor {
 public:
  enum class OutputType : uint8_t { IQ, POWER };

  struct Status1 {
    float bus_voltage = 0.0f;
    float bus_current = 0.0f;
    uint8_t motor_state = 0;
    uint8_t error_flags = 0;
  };

  struct Param {
    uint8_t motor_id = 1;
    bool reverse = false;
    uint16_t zero_encoder = 0;
    uint32_t encoder_counts_per_turn = 65536;
    float position_counts_per_degree = 100.0f;
    int32_t position_zero_count = 0;
    uint16_t max_speed_dps = 360;
    int16_t max_current_raw = 2000;
    float torque_nm_per_raw = 0.0f;
    OutputType output_type = OutputType::IQ;
  };

  explicit LKMotor(LibXR::CAN& can) : LKMotor(can, Param{}) {}

  LKMotor(LibXR::CAN& can, const Param& param) : can_(&can), param_(param) {
    ASSERT(param_.motor_id >= 1 && param_.motor_id <= 32);
    ASSERT(param_.encoder_counts_per_turn == 16384 ||
           param_.encoder_counts_per_turn == 32768 ||
           param_.encoder_counts_per_turn == 65536);
    ASSERT(param_.zero_encoder < param_.encoder_counts_per_turn);
    ASSERT(param_.max_current_raw > 0 && param_.max_current_raw <= 2048);
    const uint32_t reply_id = 0x180u + param_.motor_id;
    auto callback = LibXR::CAN::Callback::Create(
        [](bool in_isr, LKMotor* self, const LibXR::CAN::ClassicPack& pack) {
          self->OnReceive(in_isr, pack);
        }, this);
    can_->Register(callback, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE, reply_id, reply_id);
  }

  void Enable() override { Send(0x88); }
  void Disable() override { Send(0x80); }
  void Relax() override { Send(0x81); }
  void ClearError() override { Send(0x9B); }

  // This command writes the encoder zero to motor nonvolatile memory.
  void SaveZeroPoint() override { Send(0x19); }

  LibXR::ErrorCode Update() override {
    LibXR::CAN::ClassicPack pack{};
    bool received = false;
    while (recv_queue_.Pop(pack) == LibXR::ErrorCode::OK) {
      Decode(pack);
      received = true;
    }
    return received ? LibXR::ErrorCode::OK : LibXR::ErrorCode::NO_RESPONSE;
  }

  const Feedback& GetFeedback() override { return feedback_; }
  const Status1& GetStatus1() const { return status1_; }
  int16_t GetCurrentRaw() const { return current_raw_; }
  int16_t GetPowerRaw() const { return power_raw_; }

  void RequestStatus1() { Send(0x9A); }
  void RequestStatus2() { Send(0x9C); }

  void Control(const MotorCmd& cmd) override {
    switch (cmd.mode) {
      case MODE_CURRENT:
        if (param_.output_type == OutputType::IQ)
          CurrentControlNormalized(cmd.velocity);
        break;
      case MODE_TORQUE:
        if (param_.output_type == OutputType::IQ &&
            param_.torque_nm_per_raw > 0.0f) {
          CurrentControlRaw(ClampI16(cmd.torque / param_.torque_nm_per_raw,
                                     param_.max_current_raw));
        }
        break;
      case MODE_VELOCITY:
        SpeedControlRpm(cmd.velocity);
        break;
      case MODE_POSITION:
        PositionControlRad(cmd.position, cmd.velocity);
        break;
      default:
        break;
    }
  }

  void CurrentControlRaw(int16_t current) {
    if (param_.output_type != OutputType::IQ) return;
    const int16_t limited = std::clamp<int16_t>(current, -param_.max_current_raw,
                                                param_.max_current_raw);
    const uint16_t raw = static_cast<uint16_t>(param_.reverse ? -limited : limited);
    uint8_t data[8]{};
    data[0] = 0xA1;
    Put16(data + 4, raw);
    Send(data);
  }

  void CurrentControlNormalized(float current) {
    CurrentControlRaw(ClampI16(current * param_.max_current_raw,
                               param_.max_current_raw));
  }

  // MS-series open-loop power command; the protocol range is [-850, 850].
  void PowerControlRaw(int16_t power) {
    if (param_.output_type != OutputType::POWER) return;
    const int16_t limited = std::clamp<int16_t>(power, -850, 850);
    const uint16_t raw = static_cast<uint16_t>(param_.reverse ? -limited : limited);
    uint8_t data[8]{};
    data[0] = 0xA0;
    Put16(data + 4, raw);
    Send(data);
  }

  void SpeedControlRpm(float rpm) {
    const int32_t speed = ClampI32((param_.reverse ? -rpm : rpm) * 600.0f);
    uint8_t data[8]{};
    data[0] = 0xA2;
    if (param_.output_type == OutputType::IQ)
      Put16(data + 2, static_cast<uint16_t>(param_.max_current_raw));
    Put32(data + 4, static_cast<uint32_t>(speed));
    Send(data);
  }

  void PositionControlRad(float position_rad, float max_speed_rpm = 0.0f) {
    constexpr float kRadToDeg = 57.29577951308232f;
    const float angle_deg = position_rad * kRadToDeg *
                            (param_.reverse ? -1.0f : 1.0f);
    const int32_t target = ClampI32(param_.position_zero_count +
                                   angle_deg * param_.position_counts_per_degree);
    uint16_t speed = param_.max_speed_dps;
    if (max_speed_rpm > 0.0f && std::isfinite(max_speed_rpm)) {
      speed = static_cast<uint16_t>(std::clamp(max_speed_rpm * 6.0f, 1.0f,
                                               static_cast<float>(param_.max_speed_dps)));
    }
    uint8_t data[8]{};
    data[0] = 0xA4;
    Put16(data + 2, speed);
    Put32(data + 4, static_cast<uint32_t>(target));
    Send(data);
  }

  void RequestMultiTurnAngle() { Send(0x92); }

 private:
  static void Put16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
  }

  static uint16_t Read16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
  }

  static void Put32(uint8_t* out, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
  }

  static int32_t ClampI32(float value) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int32_t>(std::clamp(
        value, static_cast<float>(std::numeric_limits<int32_t>::min()),
        static_cast<float>(std::numeric_limits<int32_t>::max() - 128)));
  }

  static int16_t ClampI16(float value, int16_t limit) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int16_t>(std::clamp(value, -static_cast<float>(limit),
                                           static_cast<float>(limit)));
  }

  void Send(uint8_t command) {
    uint8_t data[8]{};
    data[0] = command;
    Send(data);
  }

  void Send(const uint8_t (&data)[8]) {
    LibXR::CAN::ClassicPack pack{};
    pack.id = 0x140u + param_.motor_id;
    pack.type = LibXR::CAN::Type::STANDARD;
    pack.dlc = 8;
    for (uint8_t i = 0; i < 8; ++i) pack.data[i] = data[i];
    can_->AddMessage(pack);
  }

  void OnReceive(bool in_isr, const LibXR::CAN::ClassicPack& pack) {
    UNUSED(in_isr);
    if (pack.dlc != 8) return;
    while (recv_queue_.Push(pack) != LibXR::ErrorCode::OK) recv_queue_.Pop();
  }

  void Decode(const LibXR::CAN::ClassicPack& pack) {
    if (pack.dlc != 8) return;
    const auto command = pack.data[0];
    if (command == 0x9A || command == 0x9B) {
      feedback_.temp = static_cast<int8_t>(pack.data[1]);
      status1_.bus_voltage = static_cast<int16_t>(Read16(pack.data + 2)) * 0.01f;
      status1_.bus_current = static_cast<int16_t>(Read16(pack.data + 4)) * 0.01f;
      status1_.motor_state = pack.data[6];
      status1_.error_flags = pack.data[7];
      feedback_.state = status1_.motor_state;
      feedback_.error_id = status1_.error_flags;
    } else if (command == 0x9C || (command >= 0xA0 && command <= 0xA8)) {
      constexpr float kDegToRad = 0.017453292519943295f;
      const int16_t output = static_cast<int16_t>(Read16(pack.data + 2));
      const int16_t speed_dps = static_cast<int16_t>(Read16(pack.data + 4));
      const uint16_t encoder = Read16(pack.data + 6);
      if (encoder >= param_.encoder_counts_per_turn) return;
      int32_t relative = static_cast<int32_t>(encoder) - param_.zero_encoder;
      const int32_t counts = static_cast<int32_t>(param_.encoder_counts_per_turn);
      if (relative < 0) relative += counts;
      if (relative >= counts / 2) relative -= counts;
      const float sign = param_.reverse ? -1.0f : 1.0f;
      current_raw_ = param_.output_type == OutputType::IQ ? output : 0;
      power_raw_ = param_.output_type == OutputType::POWER ? output : 0;
      feedback_.position = sign * static_cast<float>(relative) *
                           (static_cast<float>(LibXR::TWO_PI) /
                            static_cast<float>(param_.encoder_counts_per_turn));
      const LibXR::CycleValue<float> angle(feedback_.position);
      if (angle_initialized_) {
        feedback_.multi_turn_angle += angle - feedback_.abs_angle;
      } else {
        if (!multi_turn_seeded_) {
          feedback_.multi_turn_angle = feedback_.position;
        }
        angle_initialized_ = true;
      }
      feedback_.abs_angle = angle;
      feedback_.velocity = sign * static_cast<float>(speed_dps) / 6.0f;
      feedback_.omega = sign * static_cast<float>(speed_dps) * kDegToRad;
      feedback_.torque = sign * static_cast<float>(current_raw_) *
                         param_.torque_nm_per_raw;
      feedback_.temp = static_cast<int8_t>(pack.data[1]);
    } else if (command == 0x92) {
      uint64_t value = 0;
      for (uint8_t i = 1; i < 8; ++i) value |= uint64_t(pack.data[i]) << (8 * (i - 1));
      if (value & (uint64_t(1) << 55)) value |= 0xFF00000000000000ULL;
      const auto signed_value = static_cast<int64_t>(value);
      feedback_.multi_turn_angle = (param_.reverse ? -1.0f : 1.0f) *
                                   static_cast<float>(signed_value) * 0.0001745329252f;
      angle_initialized_ = false;
      multi_turn_seeded_ = true;
    }
  }

  LibXR::CAN* can_;
  Param param_;
  Feedback feedback_{};
  Status1 status1_{};
  bool angle_initialized_ = false;
  bool multi_turn_seeded_ = false;
  int16_t current_raw_ = 0;
  int16_t power_raw_ = 0;
  LibXR::MPMCQueue<LibXR::CAN::ClassicPack> recv_queue_{4};
};
