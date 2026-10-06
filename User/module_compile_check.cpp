#include <type_traits>
#include <cstddef>

#include "CMD.hpp"
#include "DMMotor.hpp"
#include "DR16.hpp"
#include "LKMotor.hpp"
#include "Motor.hpp"
#include "RMMotor.hpp"
#include "Referee.hpp"
#include "VT13.hpp"

static_assert(std::is_base_of_v<Motor, DMMotor>);
static_assert(std::is_base_of_v<Motor, LKMotor>);
static_assert(std::is_base_of_v<Motor, RMMotor>);
static_assert(std::is_constructible_v<DR16, LibXR::UART&, CMD&>);
static_assert(std::is_constructible_v<VT13, LibXR::UART&, CMD&>);
static_assert(sizeof(RefereeTypes::RobotGameRefereePack) == 92);
static_assert(sizeof(RefereeTypes::RobotStatus) == 13);
static_assert(offsetof(RefereeTypes::RobotGameRefereePack, robot_pos) == 80);

// Intentionally uncalled: type-check constructor and command APIs in the ARM build.
void CompileModuleConstructors(LibXR::UART& remote_uart,
                               LibXR::UART& referee_uart, LibXR::CAN& can) {
  CMD cmd;
  DR16 dr16(remote_uart, cmd);
  VT13 vt13(remote_uart, cmd);
  Referee referee(referee_uart, &cmd);
  DMMotor dm(can, {.model = DMMotor::Model::MOTOR_HERO_DOWN,
                   .reverse = false,
                   .can_id = 0x108,
                   .feedback_id = 0x018});
  RMMotor rm(can, {.model = RMMotor::Model::MOTOR_M3508,
                   .reverse = false,
                   .feedback_id = 0x201});
  LKMotor lk(can, {.motor_id = 1,
                   .zero_encoder = 55791,
                   .position_counts_per_degree = 800.0f,
                   .position_zero_count = -43838,
                   .max_speed_dps = 150});
  Motor::MotorCmd command{};
  command.mode = Motor::MODE_MIT;
  dm.Control(command);
  command.mode = Motor::MODE_CURRENT;
  rm.Control(command);
  lk.Control(command);
  (void)dr16;
  (void)vt13;
  (void)referee;
}
