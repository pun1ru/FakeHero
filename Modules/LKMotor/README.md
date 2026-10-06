# LKMotor

LK classic CAN motor driver using the shared `Motor` interface. Based on
`LK_CAN_协议.pdf`, CAN protocol V2.36, pages 5-16. Commands use standard ID
`0x140 + motor_id`, while replies use **`0x180 + motor_id`** (`motor_id` 1-32);
all frames are 8 bytes. The default `motor_id = 1` sends to `0x141` and listens
on `0x181`. `Update()` decodes queued replies outside the CAN interrupt.

`0x9C` and control replies `0xA0`-`0xA8` contain temperature (signed 1 C/LSB),
signed output current or power, speed (signed 1 degree/s per LSB) and the raw
encoder position. `Feedback.velocity` is rpm (degree/s divided by 6), and
`Feedback.omega` is rad/s. Configure `encoder_counts_per_turn` for the motor's
14/15/16-bit encoder (16384/32768/65536; default 65536). `zero_encoder` is a
software origin in encoder counts; `Feedback.position` is the shortest signed
encoder angle from that origin. `multi_turn_angle` accumulates successive
single-turn differences. A `0x92` reply replaces the accumulated angle with the
motor-reported multi-turn angle (0.01 degree/LSB); the next encoder frame resets
the difference baseline. Samples must be less than half an encoder turn apart.
`0x92` does not overwrite `position`, which continues to represent the encoder.
`RequestStatus1()` and `ClearError()` decode `0x9A`/`0x9B` replies into
`Feedback.state` (motor state) and `Feedback.error_id` (error bitmask).
`GetStatus1()` also returns bus voltage and current (0.01 V/A per LSB), motor
state and error flags. These fields update only when a status-1 reply arrives.

For MF/MH/MG, `OutputType::IQ` is the default. `MODE_CURRENT` uses normalized
current in `MotorCmd.velocity` (matching `RMMotor`), while `CurrentControlRaw()`
sends `0xA1` with a signed value limited to `max_current_raw` (default 2000,
protocol maximum 2048). `MODE_TORQUE` requires a calibrated
`torque_nm_per_raw`; otherwise it sends nothing. For MS, select
`OutputType::POWER` and call `PowerControlRaw()` for the `0xA0` open-loop power
command (range -850..850); current and torque modes do nothing in this mode.
Status replies expose the raw value through `GetCurrentRaw()` or `GetPowerRaw()`.
`Feedback.torque` is zero until an IQ torque calibration is supplied; raw current
is not itself torque.

`MODE_VELOCITY` takes rpm and sends `0xA2`: signed 0.01 degree/s units in bytes
4-7, with the IQ limit in bytes 2-3 for IQ motors. `MODE_POSITION` takes radians
and sends `0xA4`: signed 0.01 degree units in bytes 4-7 and a maximum speed in
degree/s in bytes 2-3. `MotorCmd.velocity` is the optional max speed in rpm;
zero uses `max_speed_dps`. `RequestStatus2()` sends `0x9C` and
`RequestMultiTurnAngle()` sends `0x92`. `SaveZeroPoint()` sends `0x19`, writing
the current encoder origin into motor ROM; it takes effect after a power cycle,
and frequent writes wear the device.

The old Hero pitch command used `LK_PITCH_HORIZON_ENCODE + pitch_deg * 800`.
To reproduce that mapping, configure `position_zero_count` with the original
constant (-43838 in the old upper board) and `position_counts_per_degree = 800`.
Set `max_speed_dps = 150` to match that board. Set `zero_encoder` to the
measured encoder count at horizontal (55791 in the old upper board). The
default 100 counts/degree is the protocol unit. The old scaling reflects the
gear train or board calibration and must be checked against the actual motor.

This module is a library component. Construct it with a `LibXR::CAN&` from
the hardware container; it does not self-register as an XRobot application.

Legacy integration reference: `27_3SE_hero/hero_up/PrivateDrivers/LK/LK_driver.c`.
