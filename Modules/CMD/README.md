# CMD

3SE fork of [QDU-Robomaster/CMD](https://github.com/QDU-Robomaster/CMD)
at `37b7700e9cce3ee74601d9fdcb67961792a21359` (Apache-2.0). Upstream
The module below uses the 3SE namespace; this repository is published at
https://github.com/3SE-xrobot-dev/CMD.

控制命令中枢：汇总遥控器与上位机输入，发布底盘、云台、摩擦轮、拨盘命令 / Control command hub for chassis, gimbal, shooter and stir commands

## 1. 模块作用 / Purpose

CMD 把遥控器（DR16、VT13）与上位机等来源的输入整理为四路命令 Topic：底盘、云台、摩擦轮和拨盘。下游模块按职责订阅对应 Topic。

输入由其他模块调用 `FeedRC(...)` 或 `FeedAI(...)` 写入 `CMD::Data`，每次写入立即执行一轮处理并发布四路命令。共享状态由互斥锁保护；事件与 Topic 在释放锁后、调用者上下文中同步发布，回调可以重入 CMD。`SetCtrlMode(...)`、`GetCtrlMode()`、`Online()`、`GetAIGimbalStatus()` 也通过同一把锁访问状态。

遥控器在线状态变化时，CMD 在自己的事件上激活 `CMD_EVENT_START_CTRL`（`0x13212508`，遥控器上线）或 `CMD_EVENT_LOST_CTRL`（`0x13212509`，遥控器离线）。

CMD merges remote-controller (DR16, VT13) and host input into four command Topics: chassis, gimbal, shooter and stir. Downstream modules subscribe to the relevant Topic.

Other modules write a `CMD::Data` through `FeedRC(...)` or `FeedAI(...)`. Shared state is protected by a mutex; events and Topics are published in the caller's context after releasing it, so callbacks may reenter CMD. State accessors use the same mutex.

When the remote-controller online state changes, CMD activates `CMD_EVENT_START_CTRL` (`0x13212508`, remote controller online) or `CMD_EVENT_LOST_CTRL` (`0x13212509`, remote controller offline) on its own event.

## 2. 输入源与控制模式 / Input Sources and Control Modes

遥控输入分 DR16 与 VT13 两路：`FeedRC(RCInputSource, const Data&)` 指定输入源，`FeedRC(const Data&)` 使用 DR16。某一路写入的数据 `chassis_online` 为真，且底盘或云台有非零操作、`shooter.isfric` 或 `stir.isfire` 为真时，该路成为活动源。活动源在线时持续使用；活动源离线后切换到另一路在线源；两路都离线时使用全零的离线数据。

控制模式决定发布的内容：

- `CMD_OP_CTRL`（操作手控制）：各路命令取有效的遥控数据，无效时输出零值。
- `CMD_AUTO_CTRL`（自动控制）：底盘、云台和摩擦轮各自按 AI 的 `*_online` 选择 AI 或遥控数据，无有效来源时输出零值。拨盘需要 AI 与遥控的 `stir_online` 和 `stir.isfire` 均为真才开火。

接口：

- `FeedRC(const Data&)`、`FeedRC(RCInputSource, const Data&)`：写入遥控数据。
- `FeedAI(const Data&)`：写入上位机或自动控制数据。
- `SetCtrlMode(Mode)`、`GetCtrlMode()`：设置、读取控制模式。
- `GetEvent()`：返回 CMD 的 `LibXR::Event`。在其上激活事件 ID `static_cast<uint32_t>(CMD::Mode::CMD_OP_CTRL)` 或 `static_cast<uint32_t>(CMD::Mode::CMD_AUTO_CTRL)` 即切换到对应的控制模式，`EventBinder` 等模块通过它绑定事件。
- `Online()`：遥控器是否在线。
- `GetAIGimbalStatus()`：AI 数据的 `gimbal_online`。

The remote-controller input has two channels, DR16 and VT13. `FeedRC(RCInputSource, const Data&)` selects one; `FeedRC(const Data&)` uses DR16. A channel becomes active when `chassis_online` is true and its chassis or gimbal controls exceed 0.05, or `shooter.isfric` or `stir.isfire` is true. The active channel remains selected until it goes offline; CMD then selects the other online channel or zero data.

The control mode determines what is published:

- `CMD_OP_CTRL` (operator control): publish valid RC commands, or zero when invalid.
- `CMD_AUTO_CTRL` (automatic control): select chassis, gimbal and shooter independently using the AI validity flags, falling back to valid RC commands or zero. Stir fires only when both AI and RC mark it valid and request fire.

Interface:

- `FeedRC(const Data&)`, `FeedRC(RCInputSource, const Data&)`: write remote-controller data.
- `FeedAI(const Data&)`: write host or automatic control data.
- `SetCtrlMode(Mode)`, `GetCtrlMode()`: set and read the control mode.
- `GetEvent()`: returns the `LibXR::Event` of CMD. Activating the event ID `static_cast<uint32_t>(CMD::Mode::CMD_OP_CTRL)` or `static_cast<uint32_t>(CMD::Mode::CMD_AUTO_CTRL)` on it switches to the corresponding control mode; Modules such as `EventBinder` bind events through it.
- `Online()`: whether the remote controller is online.
- `GetAIGimbalStatus()`: the `gimbal_online` of the AI data.

## 3. 构造接口 / Constructor

```cpp
CMD(Mode mode = CMD::Mode::CMD_OP_CTRL,
    const char* chassis_cmd_topic_name = "chassis_cmd",
    const char* gimbal_cmd_topic_name = "gimbal_cmd",
    const char* shooter_cmd_topic_name = "shooter_cmd",
    const char* stir_cmd_topic_name = "stir_cmd");
```

依赖：无。

配置参数：

- `mode`：初始控制模式，`CMD::Mode::CMD_OP_CTRL` 或 `CMD::Mode::CMD_AUTO_CTRL`，默认 `CMD_OP_CTRL`。
- `chassis_cmd_topic_name`：底盘命令 Topic 名称，默认 `"chassis_cmd"`。
- `gimbal_cmd_topic_name`：云台命令 Topic 名称，默认 `"gimbal_cmd"`。
- `shooter_cmd_topic_name`：摩擦轮命令 Topic 名称，默认 `"shooter_cmd"`。
- `stir_cmd_topic_name`：拨盘命令 Topic 名称，默认 `"stir_cmd"`。

Dependencies: none.

Configuration parameters:

- `mode`: initial control mode, `CMD::Mode::CMD_OP_CTRL` or `CMD::Mode::CMD_AUTO_CTRL`, default `CMD_OP_CTRL`.
- `chassis_cmd_topic_name`: name of the chassis command Topic, default `"chassis_cmd"`.
- `gimbal_cmd_topic_name`: name of the gimbal command Topic, default `"gimbal_cmd"`.
- `shooter_cmd_topic_name`: name of the shooter command Topic, default `"shooter_cmd"`.
- `stir_cmd_topic_name`: name of the stir command Topic, default `"stir_cmd"`.

## 4. Topic

四个 Topic 均以多发布者模式创建。

| Topic（默认名） | 方向 | 类型 | 说明 |
| --- | --- | --- | --- |
| `chassis_cmd` | 发布 | `CMD::ChassisCMD` | 底盘 `x`、`y`、`z` 控制量 |
| `gimbal_cmd` | 发布 | `CMD::GimbalCMD` | 云台命令：`yaw`、`pit`、`rol` 及其一阶、二阶导数 |
| `shooter_cmd` | 发布 | `CMD::ShooterCMD` | 摩擦轮开关 `isfric` |
| `stir_cmd` | 发布 | `CMD::StirCMD` | 拨盘开火 `isfire` |

All four Topics are created in multi-publisher mode.

| Topic (default name) | Direction | Type | Meaning |
| --- | --- | --- | --- |
| `chassis_cmd` | Publish | `CMD::ChassisCMD` | Chassis `x`, `y`, `z` controls |
| `gimbal_cmd` | Publish | `CMD::GimbalCMD` | Gimbal command: `yaw`, `pit`, `rol` and their first and second derivatives |
| `shooter_cmd` | Publish | `CMD::ShooterCMD` | Friction-wheel enable `isfric` |
| `stir_cmd` | Publish | `CMD::StirCMD` | Stir fire request `isfire` |

## 5. 配置示例 / Configuration Example

`xrobot instance add 3SE-xrobot-dev/CMD` 写入的实例，各项为默认值：

An instance written by `xrobot instance add 3SE-xrobot-dev/CMD`, with all values at their defaults:

```yaml
modules:
  - module: 3SE-xrobot-dev/CMD
    id: cmd
    args:
      - mode: CMD::Mode::CMD_OP_CTRL
      - chassis_cmd_topic_name: "chassis_cmd"
      - gimbal_cmd_topic_name: "gimbal_cmd"
      - shooter_cmd_topic_name: "shooter_cmd"
      - stir_cmd_topic_name: "stir_cmd"
```

DR16、VT13、Gimbal 等模块以 `CMD&` 参数引用 CMD，配置中填写 CMD 的实例 id（上例为 `cmd`），CMD 实例列在它们之前。

Modules such as DR16, VT13 and Gimbal take CMD as a `CMD&` parameter; the configuration holds the instance id of CMD (`cmd` above), and the CMD instance is listed before them.

## 6. 依赖与硬件 / Dependencies and Hardware

依赖：LibXR。

硬件：无。输入数据由 DR16、VT13 等模块写入。

Dependencies: LibXR.

Hardware: none. The input data is written by Modules such as DR16 and VT13.
