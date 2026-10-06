# 3SE XRobot modules

This directory contains the first two batches of reusable modules for the old
`27_3SE_hero` robot. Each named directory is self-contained and can become an
independent public XRobot module repository under
`https://github.com/3SE-xrobot-dev/`. The modules use LibXR UART, CAN, Topic,
Event and thread APIs. `CMD`, `Referee`, `DR16`, `VT13` and the motor classes
are library objects: an application owns their instances and supplies the
hardware interfaces. They are not automatically enabled by `User/xrobot.yaml`.

| Batch | Repository | Dependencies | Legacy function covered |
| --- | --- | --- | --- |
| 1 | CMD | LibXR | RC/AI command selection and Topic publication |
| 1 | DR16 | CMD | 18-byte DBUS remote, switches and keyboard |
| 1 | VT13 | CMD | 21-byte CRC16 remote, switches and keyboard |
| 1 | Referee | CMD, LibXR | 2025 frame parsing, UI transmit and game data Topics |
| 2 | Motor | LibXR | Common motor command and feedback interface |
| 2 | DMMotor | Motor | DM MIT CAN commands and feedback |
| 2 | RMMotor | Motor | DJI M2006/M3508/GM6020 grouped CAN commands |
| 2 | LKMotor | Motor | LK pitch motor CAN commands and feedback |

## Hero mapping

- The lower board receives the referee system on USART1 at 115200 8N1. The
  upper board has no physical referee connection; it receives selected data
  through board-to-board CAN. Instantiate `Referee` only on a connected UART.
- The old remote receiver uses UART5 at 100000 8E2. `DR16` and `VT13` now
  default to this setting. They cannot both own UART5 simultaneously; select
  the protocol matching the installed receiver. The frame parsers feed `CMD`.
- The lower board uses DM MIT IDs `0x01`-`0x06` and the stirring motor command
  ID `0x108` (feedback `0x018`). Choose `MOTOR_HERO_DOWN` for the old MIT
  scaling. The upper board yaw DM uses ID `0x07`; choose `MOTOR_HERO_UP`.
- The old upper board LK pitch motor uses CAN ID `0x141`. For its position
  command, configure `position_zero_count = -43838`,
  `position_counts_per_degree = 800`, `max_speed_dps = 150`, and measured
  `zero_encoder = 55791`. Confirm these on the actual mechanism before motion.
- DJI wheel and friction motors use feedback IDs `0x201`-`0x208`, with grouped
  control IDs `0x200`/`0x1FF`. `RMMotor` groups motors by CAN bus and control
  ID. Construct all members before sending commands.

## Build and adoption

The installed `xrobot_create_mod` generated the eight directory skeletons;
`xrobot_mod_parser --path Modules/<Name>` parses their V2 manifests. LibXR's
CMake integration discovers each directory and adds its include path. The
`User/module_compile_check.cpp` translation unit includes every new header
and checks the motor inheritance and referee packet layout. Run:

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel 8
```

The ARM GNU toolchain must be on PATH when configuring. A successful firmware
build verifies that all headers compile with the current LibXR revision; it
does not verify UART timing, CAN payloads on a device, or motor direction.
Module instances and hardware binding still need to be added to a robot
application after choosing the actual board wiring. The current
`User/xrobot.yaml` remains the existing BMI088/TestModule configuration.

Each QDU-derived directory keeps its Apache-2.0 `LICENSE` and the upstream
commit in its README. `LKMotor` was implemented from the old Hero LK driver
protocol and also carries Apache-2.0. The old Hero source and QDU examples
were references; no HAL-dependent Hero source files were copied into these
modules. The upstream README examples still show the QDU namespace and its
CLI version. The installed local CLI uses `xrobot_*` commands. The public
module index is `https://github.com/3SE-xrobot-dev/modules-index`; it is
registered in `Modules/sources.yaml`. This project compiles the local working
copies directly, so `Modules/modules.yaml` does not list them to avoid cloning
over local changes.
