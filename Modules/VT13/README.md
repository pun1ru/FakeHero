# VT13

3SE fork of [QDU-Robomaster/VT13](https://github.com/QDU-Robomaster/VT13)
at `7927154b47f5da7bab47e86da098705a8d4e26ae` (Apache-2.0). Upstream
CLI examples below retain the QDU namespace; this repository is published at
https://github.com/3SE-xrobot-dev/VT13.

VT13 链路遥控解析模块：从 UART 接收 21 字节协议帧，向 CMD 输入控制量并发出事件 / VT13 link remote controller Module that receives 21-byte frames over UART, feeds control data to CMD and emits events

## 1. 模块作用 / Purpose

构造时，VT13 默认把 UART 设为旧 Hero 使用的 100000 bit/s、8E2，并创建线程 `uart_vt13`（栈深与优先级见 `Param`）。线程逐字节阻塞读取，用状态机对齐帧头 `0xA9 0x53` 并拼出 21 字节帧。波特率、校验和停止位可通过 `Param` 调整。

帧校验：前 19 字节的 CRC16（LibXR `CRC16`，即 CRC-16/MCRF4XX，多项式 0x1021 反射、初值 0xFFFF）与帧尾 2 字节小端值比较；摇杆与拨轮通道取值在 364 到 1684 之间，挡位不超过 S。通过校验的帧经 `cmd.FeedRC(CMD::RCInputSource::RC_INPUT_VT13, ...)` 输入给 CMD。

超过 100 ms 没有有效帧时判定离线：向 CMD 输入一次控制量全零、`chassis_online` 与 `gimbal_online` 为 `false` 的数据，并清除各切换状态。恢复后的第一帧用于建立边沿基线，不触发事件。

控制源默认为遥控器模式；Shift+Ctrl+Q 切到遥控器模式，Shift+Ctrl+E 切到键鼠模式。

- 遥控器模式：底盘 `x` 为左摇杆 Y，`y` 为左摇杆 X，`z` 为右摇杆 X；云台 `yaw` 为右摇杆 X，`pit` 为右摇杆 Y；均归一化到 [-1, 1]；扳机按下时开火。
- 键鼠模式：W / S、A / D 使底盘 `y`、`x` 为 ±1，`z` 为 0，按住 Shift 时底盘模式为 `BOOST`；云台 `pit` 为鼠标 Y × 1000/32768，`yaw` 为鼠标 X × 1000/32768；鼠标左键按下时开火。

At construction, VT13 defaults to the old Hero UART setting of 100000 bit/s, 8E2. Its receive thread reads one byte at a time and assembles 21-byte frames. Baud rate, parity and stop bits are configurable in `Param`.

Frame validation: the CRC16 of the first 19 bytes (LibXR `CRC16`, i.e. CRC-16/MCRF4XX with reflected polynomial 0x1021 and initial value 0xFFFF) is compared with the little-endian value in the last 2 bytes; stick and dial channels lie in 364 to 1684 and the switch position is at most S. Frames that pass are fed to CMD through `cmd.FeedRC(CMD::RCInputSource::RC_INPUT_VT13, ...)`.

When no valid frame arrives for more than 100 ms the link is judged offline: one data set with all control values zero and `chassis_online` and `gimbal_online` set to `false` is fed to CMD, and the toggle states are cleared. The first frame after recovery establishes the edge baseline and triggers no events.

The control source defaults to remote controller mode; Shift+Ctrl+Q switches to remote controller mode and Shift+Ctrl+E to keyboard and mouse mode.

- Remote controller mode: chassis `x` is the left stick Y, `y` the left stick X and `z` the right stick X; gimbal `yaw` is the right stick X and `pit` the right stick Y; all are normalized to [-1, 1]; the trigger fires.
- Keyboard and mouse mode: W / S and A / D set the chassis `y` and `x` to ±1 and `z` to 0, holding Shift sets the chassis mode to `BOOST`; gimbal `pit` is the mouse Y × 1000/32768 and `yaw` the mouse X × 1000/32768; the left mouse button fires.

## 2. 事件 / Events

`GetEvent()` 返回 VT13 的 `LibXR::Event`，EventBinder 等模块用它绑定下列事件 ID：

- 挡位开关变化：`VT13_SW_POS_C` / `N` / `S`（0 到 2）。
- 自定义左键、右键、暂停键、扳机的按下与松开：`VT13_KEY_PRESSED_*` / `VT13_KEY_RELEASE_*`（`0x100` 到 `0x107`）。
- 切换结果（`0x130` 到 `0x135`）：自定义左键每次按下翻转一次，发出 `VT13_KEY_CUSTOM_L_TOGGLE_ON` 或 `OFF`；自定义右键同理发出 `VT13_KEY_CUSTOM_R_TOGGLE_ON` 或 `OFF`；暂停键翻转后发出 `VT13_KEY_PAUSE_TOGGLE_ON` 或 `OFF`；右键切换状态为 ON 时，按下暂停键发出 OFF。
- 拨轮（阈值为中值 ±180）：上拨后 500 ms 内回中发出 `VT13_DIAL_UP_SHORT`，上拨保持 500 ms 发出 `VT13_DIAL_UP_LONG`，下拨发出 `VT13_DIAL_DOWN_TOUCH`（`0x120` 到 `0x122`）。
- 键盘按下（上升沿）：`Key::KEY_W` 到 `KEY_B`；同时按住 Shift、Ctrl 或 Shift+Ctrl 时，事件 ID 分别加上 1、2、3 倍 `KEY_NUM`，可用 `ShiftWith()`、`CtrlWith()`、`ShiftCtrlWith()` 计算。
- 鼠标（键鼠模式）：`KEY_L_PRESS`、`KEY_R_PRESS`、`KEY_M_PRESS` 及对应的 `*_RELEASE`。

`GetEvent()` returns the `LibXR::Event` of VT13, to which modules such as EventBinder bind the following event IDs:

- Switch position change: `VT13_SW_POS_C` / `N` / `S` (0 to 2).
- Press and release of the custom left key, the custom right key, the pause key and the trigger: `VT13_KEY_PRESSED_*` / `VT13_KEY_RELEASE_*` (`0x100` to `0x107`).
- Toggle results (`0x130` to `0x135`): each press of the custom left key flips a state and emits `VT13_KEY_CUSTOM_L_TOGGLE_ON` or `OFF`; the custom right key emits `VT13_KEY_CUSTOM_R_TOGGLE_ON` or `OFF` likewise; the pause key emits `VT13_KEY_PAUSE_TOGGLE_ON` or `OFF` after flipping, and emits OFF while the right key toggle is ON.
- Dial (threshold ±180 around the middle value): returning to the middle within 500 ms after pushing up emits `VT13_DIAL_UP_SHORT`, holding up for 500 ms emits `VT13_DIAL_UP_LONG`, and pushing down emits `VT13_DIAL_DOWN_TOUCH` (`0x120` to `0x122`).
- Keyboard press (rising edge): `Key::KEY_W` to `KEY_B`; with Shift, Ctrl or Shift+Ctrl held the event ID is increased by 1, 2 or 3 times `KEY_NUM`, computed with `ShiftWith()`, `CtrlWith()` and `ShiftCtrlWith()`.
- Mouse (keyboard and mouse mode): `KEY_L_PRESS`, `KEY_R_PRESS`, `KEY_M_PRESS` and the matching `*_RELEASE`.

## 3. 构造接口 / Constructor

```cpp
VT13(LibXR::UART& uart,
     CMD& cmd,
     const Param& param = {.task_stack_depth_uart = 1536,
                           .thread_priority_uart = LibXR::Thread::Priority::HIGH});
```

依赖：

- `uart`：连接 VT13 链路的 `LibXR::UART`，取自 BSP 的硬件注册（`XR_REGISTER`）；构造时重新设置波特率与校验。
- `cmd`：`CMD` 实例，接收解析后的控制数据。

配置参数（`Param`）：

- `task_stack_depth_uart`：接收线程栈深，默认 1536。
- `thread_priority_uart`：接收线程优先级，默认 `LibXR::Thread::Priority::HIGH`。

Dependencies:

- `uart`: the `LibXR::UART` connected to the VT13 link, taken from the BSP's Registration (`XR_REGISTER`); its baud rate and parity are reconfigured at construction.
- `cmd`: the `CMD` instance that receives the parsed control data.

Configuration parameters (`Param`):

- `task_stack_depth_uart`: stack depth of the receive thread, default 1536.
- `thread_priority_uart`: priority of the receive thread, default `LibXR::Thread::Priority::HIGH`.

## 4. Topic

无 / None

## 5. 配置示例 / Configuration Example

`xrobot instance add QDU-Robomaster/VT13` 写入的实例，`uart` 填写为 BSP 中注册的串口名称，`cmd` 填写为 `QDU-Robomaster/CMD` 实例的 id：

An instance written by `xrobot instance add QDU-Robomaster/VT13`, with `uart` set to a UART name registered by the BSP and `cmd` set to the id of a `QDU-Robomaster/CMD` instance:

```yaml
modules:
  - module: QDU-Robomaster/VT13
    id: vt13
    args:
      - uart: usart6
      - cmd: cmd
      - param:
          task_stack_depth_uart: 1536
          thread_priority_uart: LibXR::Thread::Priority::HIGH
```

`QDU-Robomaster/CMD` 实例列在本实例之前。DR16 与 VT13 可使用同一个 CMD 实例，CMD 在两路遥控之间选择活动源。

The `QDU-Robomaster/CMD` instance is listed before this instance. DR16 and VT13 can share one CMD instance, which selects the active source between the two remote controllers.

## 6. 依赖与硬件 / Dependencies and Hardware

依赖：

- `QDU-Robomaster/CMD`：接收解析后的控制数据（`CMD::FeedRC`）。
- LibXR。

硬件：VT13 图传链路接收端，经 UART（旧 Hero 默认 100000 bit/s、8E2）连接，UART 对象通过 `XR_REGISTER` 注册。

Dependencies:

- `QDU-Robomaster/CMD`: receives the parsed control data (`CMD::FeedRC`).
- LibXR.

Hardware: the VT13 receiver on UART (old Hero default 100000 bit/s, 8E2), registered with `XR_REGISTER`.
