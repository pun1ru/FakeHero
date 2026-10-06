#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: LK CAN motor driver for Hero pitch axis
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
  struct Param {
    uint8_t motor_id = 1;
    bool reverse = false;
    uint16_t zero_encoder = 0;
    float position_counts_per_degree = 100.0f;
    int32_t position_zero_count = 0;
    uint16_t max_speed_dps = 360;
    int16_t max_current_raw = 2000;
    float torque_nm_per_raw = 0.0f;
  };

  explicit LKMotor(LibXR::CAN& can) : LKMotor(can, Param{}) {}

  LKMotor(LibXR::CAN& can, const Param& param) : can_(&can), param_(param) {
    const uint32_t id = 0x140u + param_.motor_id;
    auto callback = LibXR::CAN::Callback::Create(
        [](bool in_isr, LKMotor* self, const LibXR::CAN::ClassicPack& pack) {
          self->OnReceive(in_isr, pack);
        }, this);
    can_->Register(callback, LibXR::CAN::Type::STANDARD,
                   LibXR::CAN::FilterMode::ID_RANGE, id, id);
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
  int16_t GetCurrentRaw() const { return current_raw_; }

  void Control(const MotorCmd& cmd) override {
    switch (cmd.mode) {
      case MODE_CURRENT:
        CurrentControlNormalized(cmd.velocity);
        break;
      case MODE_TORQUE:
        if (param_.torque_nm_per_raw > 0.0f) {
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

  void SpeedControlRpm(float rpm) {
    const int32_t speed = ClampI32((param_.reverse ? -rpm : rpm) * 600.0f);
    uint8_t data[8]{};
    data[0] = 0xA2;
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
    if (command == 0xA1 || command == 0xA2 || command == 0xA4 || command == 0xA6) {
      constexpr float kDegToRad = 0.017453292519943295f;
      const int16_t current = static_cast<int16_t>(
          static_cast<uint16_t>(pack.data[2] | (pack.data[3] << 8)));
      const int16_t rpm = static_cast<int16_t>(
          static_cast<uint16_t>(pack.data[4] | (pack.data[5] << 8)));
      const uint16_t encoder = static_cast<uint16_t>(pack.data[6] | (pack.data[7] << 8));
      const int16_t relative = static_cast<int16_t>(encoder - param_.zero_encoder);
      const float sign = param_.reverse ? -1.0f : 1.0f;
      current_raw_ = current;
      feedback_.position = sign * relative * (360.0f / 65536.0f) * kDegToRad;
      feedback_.abs_angle = feedback_.position;
      feedback_.velocity = sign * rpm;
      feedback_.omega = feedback_.velocity * 0.10471975511965977f;
      feedback_.torque = sign * current * param_.torque_nm_per_raw;
      feedback_.temp = pack.data[1];
      feedback_.state = 1;
    } else if (command == 0x92) {
      uint64_t value = 0;
      for (uint8_t i = 1; i < 8; ++i) value |= uint64_t(pack.data[i]) << (8 * (i - 1));
      if (value & (uint64_t(1) << 55)) value |= 0xFF00000000000000ULL;
      const auto signed_value = static_cast<int64_t>(value);
      feedback_.position = (param_.reverse ? -1.0f : 1.0f) *
                           static_cast<float>(signed_value) * 0.0001745329252f;
    }
  }

  LibXR::CAN* can_;
  Param param_;
  Feedback feedback_{};
  int16_t current_raw_ = 0;
  LibXR::MPMCQueue<LibXR::CAN::ClassicPack> recv_queue_{4};
};
