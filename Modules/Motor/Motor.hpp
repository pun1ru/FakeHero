#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 统一电机控制接口的抽象基类库 / Library of the abstract base class that unifies the motor control interface
standalone: false
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstdint>

#include "cycle_value.hpp"
#include "libxr_def.hpp"

/**
 * @brief 电机抽象接口：统一描述电机控制命令、反馈格式与基础控制能力。
 *        Abstract motor interface that unifies the control command, the feedback
 *        format and the basic control capabilities of a motor.
 */
class Motor
{
 public:
  /**
   * @brief 控制模式。
   *        Control modes.
   */
  enum ControlMode : uint8_t
  {
    MODE_POSITION,  ///< 位置控制模式 Position control mode
    MODE_VELOCITY,  ///< 速度控制模式 Velocity control mode
    MODE_TORQUE,    ///< 力矩控制模式 Torque control mode
    MODE_CURRENT,   ///< 电流控制模式 Current control mode
    MODE_MIT,       ///< MIT 控制模式 MIT control mode
  };

  /**
   * @brief 电机控制命令，各驱动实现其中一部分模式并决定字段的解释。
   *        Motor control command; each driver implements a subset of the modes and
   *        defines how the fields are interpreted.
   */
  struct MotorCmd
  {
    ControlMode mode;              ///< 电机控制模式 Motor control mode
    float reduction_ratio = 1.0f;  ///< 减速比 Reduction ratio
    float torque = 0;              ///< 力矩 Torque
    float position = 0;            ///< 目标位置 Target position
    float velocity = 0;            ///< 目标速度 Target velocity
    float kp = 0;                  ///< 刚度 Stiffness
    float kd = 0;                  ///< 阻尼 Damping
  };

  /**
   * @brief 电机反馈。
   *        Motor feedback.
   */
  struct Feedback
  {
    uint8_t error_id;                    ///< 电机错误码 Motor error code
    uint8_t state = 0;                   ///< 电机错误状态 Motor error state
    float position;                      ///< 电机原始角度 Raw motor angle
    LibXR::CycleValue<float> abs_angle;  ///< 归一化到单圈的角度 Single-turn angle
    float multi_turn_angle = 0.0f;       ///< 从首次反馈累计的多圈角度 (rad) Accumulated angle
    float velocity;                      ///< 转速 Speed
    float omega;                         ///< 角速度 Angular velocity
    float torque;                        ///< 扭矩 Torque
    float temp;                          ///< 温度 Temperature
  };

  /**
   * @brief 虚析构函数。
   *        Virtual destructor.
   */
  virtual ~Motor() = default;

  /**
   * @brief 使能电机输出。
   *        Enable the motor output.
   */
  virtual void Enable() = 0;

  /**
   * @brief 失能电机输出。
   *        Disable the motor output.
   */
  virtual void Disable() = 0;

  /**
   * @brief 松开电机，输出零力矩或零电流。
   *        Relax the motor with zero torque or zero current output.
   */
  virtual void Relax() = 0;

  /**
   * @brief 更新电机反馈。
   *        Update the motor feedback.
   *
   * @return 更新结果。
   *         Result of the update.
   */
  virtual LibXR::ErrorCode Update() = 0;

  /**
   * @brief 获取当前反馈。
   *        Get the current feedback.
   *
   * @return 反馈数据的引用。
   *         Reference to the feedback data.
   */
  virtual const Feedback& GetFeedback() = 0;

  /**
   * @brief 下发控制命令。
   *        Send a control command.
   *
   * @param cmd 控制命令。
   *            Control command.
   */
  virtual void Control(const MotorCmd& cmd) = 0;

  /**
   * @brief 清除电机错误状态。
   *        Clear the motor error state.
   */
  virtual void ClearError() = 0;

  /**
   * @brief 保存当前位置为零点。
   *        Save the current position as the zero point.
   */
  virtual void SaveZeroPoint() = 0;

 private:
};
