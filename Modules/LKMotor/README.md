# LKMotor

LK CAN motor driver using the shared `Motor` interface. One motor uses standard
CAN ID `0x140 + motor_id`; the default `motor_id = 1` matches the Hero pitch
motor at `0x141`. The driver queues feedback and decodes it in `Update()`.

`MODE_CURRENT` uses normalized current in `MotorCmd.velocity` (matching
`RMMotor`); `CurrentControlRaw()` accepts the legacy signed command directly.
`MODE_VELOCITY` uses rpm. `MODE_POSITION` uses radians and emits command `0xA4`.
`MODE_TORQUE` requires a calibrated `torque_nm_per_raw`, otherwise it sends no
command. `SaveZeroPoint()` writes motor nonvolatile memory (`0x19`).

The old Hero pitch command used `LK_PITCH_HORIZON_ENCODE + pitch_deg * 800`.
To reproduce that mapping, configure `position_zero_count` with the original
constant (-43838 in the old upper board) and `position_counts_per_degree = 800`.
Set `max_speed_dps = 150` to match that board. Set `zero_encoder` to the
measured encoder count at horizontal (55791 in the old upper board). The
default 100 counts/degree is the un-geared LK protocol unit.

This module is a library component. Construct it with a `LibXR::CAN&` from
the hardware container; it does not self-register as an XRobot application.

Source protocol reference: `27_3SE_hero/hero_up/PrivateDrivers/LK/LK_driver.c`.
