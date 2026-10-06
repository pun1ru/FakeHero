# Referee

3SE fork of [QDU-Robomaster/Referee](https://github.com/QDU-Robomaster/Referee)
at `786d041da0bff162ebc0c70ea18434b0e9e675d4` (Apache-2.0). Upstream
CLI examples below retain the QDU namespace; this repository is published at
https://github.com/3SE-xrobot-dev/Referee.

RoboMaster 裁判系统（2025 协议）串口收发模块：解析数据并发布 Topic，发送客户端 UI 与哨兵、雷达决策 / RoboMaster referee system (2025 protocol) UART Module that parses data, publishes Topics and sends client UI and sentry and radar decisions

## 1. 模块作用 / Purpose

构造时，Referee 按 `baudrate` 配置串口（8N1），创建输出 Topic，并启动线程 `Referee`（栈深 `task_stack_depth_uart`，优先级 `thread_priority_uart`）。线程循环执行：

1. 逐字节读取到起始字节 `0xA5`，读取其余包头并校验 CRC8。串口读取失败时链路状态置为 `OFFLINE`，收到有效包头时置为 `RUNNING`。
2. 读取命令码、数据段与帧尾，校验帧 CRC16，把数据段复制到内部数据集的对应字段：0x0001–0x0310 为机器人与比赛数据，0x0A01–0x0A06 为雷达链路数据，0x0F01 / 0x0F02 为图传信道应答。命令码未知或数据段长度不足的帧被丢弃。
3. 帧解析成功后发布下文的 Topic。
4. 休眠 10 ms。

当 `cmd` 不为空时，来自图传链路的键鼠数据（0x0304）经单参数的 `CMD::FeedRC()` 以 DR16 输入源（`RC_INPUT_DR16`）写入 CMD，与 `QDU-Robomaster/DR16` 使用同一个输入槽：W / A / S / D 设置底盘 x / y 指令为 ±0.5（按住 Shift 时加倍），鼠标 x / y 位移取反后乘以 1000/32768，作为云台 yaw / pitch 指令，左键开火，控制源为 `CTRL_SOURCE_RC`。`cmd` 为 `nullptr` 时不转发，之后可用 `BindCMD(CMD&)` 绑定。

发送：所有帧带有包头 CRC8、帧 CRC16 与序号，写入由互斥锁串行化。

- `SendFrame(cmd_id, payload)` 与 `SendStudentCmd(data_cmd_id, sender, receiver, payload)`（机器人交互帧，0x0301）。
- 客户端 UI：`FillLine`、`FillRect`、`FillCircle`、`FillEllipse`、`FillArc`、`FillFloat`、`FillInt`、`FillCharacter` 生成图形；`SendUIFigure`、`SendUIFigure2/5/7`、`SendUICharacter`、`SendUILayerDelete` 发送图形；`GetRobotID()` 与 `GetClientID(robot_id)` 给出发送者与接收者 ID。
- 哨兵决策：`SetNeedBullet`、`SetConfirmRevival`、`SetBulletRemote`、`SetHPRemote`、`SetRevivalRemote`、`SetSwitchMode` 更新保存的 0x0120 数据段，`SendSentryPack()` 将其发送给裁判系统服务器；`SendSentryDecision`、`SendRadarDecision`、`SendRadarPack` 发送指定的数据段。
- 图传链路：`SendSetVideoTransChannel(channel)`、`SendQueryVideoTransChannel()`、`SendCustomDataToController(...)`。

At construction, Referee configures the UART with `baudrate` (8N1), creates the output Topics and starts the thread `Referee` (stack depth `task_stack_depth_uart`, priority `thread_priority_uart`). The thread loops:

1. Read byte by byte up to the start byte `0xA5`, read the rest of the header and verify its CRC8. A failed UART read sets the link status to `OFFLINE`; a valid header sets it to `RUNNING`.
2. Read the command ID, data segment and frame tail, verify the frame CRC16 and copy the data segment into the matching field of the internal data set: 0x0001–0x0310 are robot and game data, 0x0A01–0x0A06 are radar link data, 0x0F01 / 0x0F02 are video channel replies. Frames with an unknown command ID or a short data segment are dropped.
3. After a frame is parsed, publish the Topics listed below.
4. Sleep for 10 ms.

When `cmd` is not null, keyboard and mouse data from the video link (0x0304) is written to CMD through the single-argument `CMD::FeedRC()` with the DR16 input source (`RC_INPUT_DR16`), the same input slot that `QDU-Robomaster/DR16` uses: W / A / S / D set the chassis x / y command to ±0.5 (doubled while Shift is held), the mouse x / y displacement is negated and multiplied by 1000/32768 to give the gimbal yaw / pitch command, the left button fires, and the control source is `CTRL_SOURCE_RC`. With `cmd` set to `nullptr` nothing is forwarded; `BindCMD(CMD&)` can bind one later.

Sending: every frame carries a header CRC8, a frame CRC16 and a sequence number, and writes are serialized by a mutex.

- `SendFrame(cmd_id, payload)` and `SendStudentCmd(data_cmd_id, sender, receiver, payload)` (robot interaction frames, 0x0301).
- Client UI: `FillLine`, `FillRect`, `FillCircle`, `FillEllipse`, `FillArc`, `FillFloat`, `FillInt` and `FillCharacter` build figures; `SendUIFigure`, `SendUIFigure2/5/7`, `SendUICharacter` and `SendUILayerDelete` send them; `GetRobotID()` and `GetClientID(robot_id)` give the sender and receiver IDs.
- Sentry decisions: `SetNeedBullet`, `SetConfirmRevival`, `SetBulletRemote`, `SetHPRemote`, `SetRevivalRemote` and `SetSwitchMode` update the stored 0x0120 data segment, and `SendSentryPack()` sends it to the referee server; `SendSentryDecision`, `SendRadarDecision` and `SendRadarPack` send explicit data segments.
- Video link: `SendSetVideoTransChannel(channel)`, `SendQueryVideoTransChannel()` and `SendCustomDataToController(...)`.

## 2. 共享消息类型 / Shared Message Types

`RefereeTypes.hpp` 提供生产者拥有的 `RobotGameRefereePack` 及其组成类型（`GameStatus`、`RobotStatus`、`RobotPOS`、`RFID`、`RobotPosForSentry`、`SentryInfo`），包含 `<cstdint>`，主机端的订阅者可直接包含该头文件。`Referee::RobotGameRefereePack` 与 `Referee` 内的同名组成类型是这些类型的别名，紧凑布局共 92 字节。

在构建中启用 `BUILD_TESTING` 后，目标 `referee_types_test` 在编译期检查 `RobotGameRefereePack` 的大小与字段偏移，运行方式为 `ctest -R referee_types_test`。

`RefereeTypes.hpp` provides the producer-owned `RobotGameRefereePack` and its component types (`GameStatus`, `RobotStatus`, `RobotPOS`, `RFID`, `RobotPosForSentry`, `SentryInfo`). It includes `<cstdint>`, so host-side subscribers can include the header directly. `Referee::RobotGameRefereePack` and the component types of the same names inside `Referee` are aliases of these types; the packed layout is 92 bytes.

With `BUILD_TESTING` enabled in the build, the target `referee_types_test` checks the size and field offsets of `RobotGameRefereePack` at compile time; it is run with `ctest -R referee_types_test`.

## 3. 构造接口 / Constructor

```cpp
Referee(LibXR::UART& uart,
        CMD* cmd,
        const Param& param = {...});  // 节选 / excerpt
```

依赖：

- `uart`：`LibXR::UART`，连接裁判系统或图传链路的串口，取自 BSP 的硬件注册（`XR_REGISTER`）。
- `cmd`：`CMD*`，接收图传键鼠控制的 CMD 实例指针，可为 `nullptr`。

配置参数（`Param`）：

- `task_stack_depth_uart`：线程 `Referee` 的栈深，默认 2048。
- `baudrate`：串口波特率，默认 115200。
- `referee_chassis_tp_name`：底盘 Topic 名称，默认 `"chassis_ref"`。
- `referee_launcher_tp_name`：发射 Topic 名称，默认 `"launcher_ref"`。
- `referee_robot_game_tp_name`：摘要 Topic 名称，默认 `"robot_game_ref"`。
- `referee_radar_tp_name`：雷达 Topic 名称，默认 `"radar_ref"`。
- `thread_priority_uart`：线程优先级，默认 `LibXR::Thread::Priority::MEDIUM`。

Dependencies:

- `uart`: the `LibXR::UART` connected to the referee system or the video link, taken from the BSP's Registration (`XR_REGISTER`).
- `cmd`: `CMD*`, pointer to the CMD instance that receives the video-link keyboard and mouse control, may be `nullptr`.

Configuration parameters (`Param`):

- `task_stack_depth_uart`: stack depth of the `Referee` thread, default 2048.
- `baudrate`: UART baud rate, default 115200.
- `referee_chassis_tp_name`: chassis Topic name, default `"chassis_ref"`.
- `referee_launcher_tp_name`: launcher Topic name, default `"launcher_ref"`.
- `referee_robot_game_tp_name`: summary Topic name, default `"robot_game_ref"`.
- `referee_radar_tp_name`: radar Topic name, default `"radar_ref"`.
- `thread_priority_uart`: thread priority, default `LibXR::Thread::Priority::MEDIUM`.

## 4. Topic

均为多发布者 Topic，名称可配置。

| Topic（默认名称） | 方向 | 类型 | 内容 |
| --- | --- | --- | --- |
| `chassis_ref` | 发布 | `Referee::ChassisPack` | `RobotStatus`（等级、功率上限等）与底盘缓冲能量（J） |
| `launcher_ref` | 发布 | `Referee::LauncherPack` | `RobotStatus`、`RobotBuff`、`LauncherData`、`DartClient`、`PowerHeat` |
| `robot_game_ref` | 发布 | `Referee::RobotGameRefereePack` | 机器人状态、比赛状态、哨兵信息、RFID、17 mm 允许发弹量、前哨站与基地血量、机器人位置 |
| `radar_ref` | 创建 | `Referee::RadarPack` | 地面机器人位置与本机位置（x、y，单位 m） |

All Topics are multi-publisher Topics with configurable names.

| Topic (default name) | Direction | Type | Content |
| --- | --- | --- | --- |
| `chassis_ref` | Publish | `Referee::ChassisPack` | `RobotStatus` (level, power limit, ...) and chassis power buffer (J) |
| `launcher_ref` | Publish | `Referee::LauncherPack` | `RobotStatus`, `RobotBuff`, `LauncherData`, `DartClient`, `PowerHeat` |
| `robot_game_ref` | Publish | `Referee::RobotGameRefereePack` | Robot status, game status, sentry info, RFID, 17 mm allowance, outpost and base HP, robot positions |
| `radar_ref` | Create | `Referee::RadarPack` | Ground robot positions and the position of this robot (x, y in m) |

## 5. 配置示例 / Configuration Example

`xrobot instance add QDU-Robomaster/Referee` 写入的实例，`uart` 填写为 BSP 中注册的串口名称，`cmd` 填写为 `QDU-Robomaster/CMD` 实例或 `nullptr`，指针依赖写成 `'&id'`；参数取自 bsp-dev-c 的 `User/RobotConfig/omni_infantry_3.yaml`：

An instance written by `xrobot instance add QDU-Robomaster/Referee`, with `uart` set to a UART name registered by the BSP and `cmd` set to a `QDU-Robomaster/CMD` instance or `nullptr`, pointer dependencies written as `'&id'`; the parameters are taken from `User/RobotConfig/omni_infantry_3.yaml` of bsp-dev-c:

```yaml
modules:
  - module: QDU-Robomaster/Referee
    id: ref
    args:
      - uart: usart1
      - cmd: nullptr
      - param:
          task_stack_depth_uart: 1536
          baudrate: 115200
          referee_chassis_tp_name: "chassis_ref"
          referee_launcher_tp_name: "launcher_ref"
          referee_robot_game_tp_name: "robot_game_ref"
          referee_radar_tp_name: "radar_ref"
          thread_priority_uart: LibXR::Thread::Priority::MEDIUM
```

在构造时订阅 Referee Topic 的模块（例如 `QDU-Robomaster/SuperPower`、`QDU-Robomaster/SentryProtocol`）的实例列在本实例之后。

Instances of Modules that subscribe to the Referee Topics at construction (for example `QDU-Robomaster/SuperPower` and `QDU-Robomaster/SentryProtocol`) are listed after this instance.

## 6. 依赖与硬件 / Dependencies and Hardware

依赖：

- `QDU-Robomaster/CMD`：经 `CMD::FeedRC()` 接收图传链路的键鼠控制。
- LibXR。

硬件：连接裁判系统（或图传链路）的串口，由 `LibXR::UART` 驱动，并通过 `XR_REGISTER` 注册。

Dependencies:

- `QDU-Robomaster/CMD`: receives the video-link keyboard and mouse control through `CMD::FeedRC()`.
- LibXR.

Hardware: a UART connected to the referee system (or the video link), driven through `LibXR::UART` and registered with `XR_REGISTER`.
