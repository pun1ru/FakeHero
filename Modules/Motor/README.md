# Motor

3SE fork of [QDU-Robomaster/Motor](https://github.com/QDU-Robomaster/Motor)
at `b78274996f93eec611efab2ba7ecc51fbd5672b9` (Apache-2.0). Upstream
CLI examples below retain the QDU namespace; this repository is published at
https://github.com/3SE-xrobot-dev/Motor.

统一电机控制接口的抽象基类库 / Library of the abstract base class that unifies the motor control interface

## 1. 模块作用 / Purpose

Motor 是库型模块（`standalone: false`）。业务模块只持有 `Motor&` 或 `Motor*`，通过它统一不同电机驱动（例如 `RMMotor`、`DMMotor`）的差异。Motor 统一以下三方面：

- 控制命令结构：位置、速度、力矩、电流、MIT。
- 反馈结构：角度、转速、角速度、扭矩、温度、错误码。
- 上层模块依赖的抽象接口。

驱动模块和业务模块包含 `Motor.hpp`，并在各自 manifest 的 `depends` 中列出 `QDU-Robomaster/Motor`，由此被拉入工程。

`Motor` 是纯虚接口：

- 生命周期：`Enable()`、`Disable()`、`Relax()`。
- 反馈刷新：`LibXR::ErrorCode Update()`。
- 反馈读取：`const Feedback& GetFeedback()`。
- 控制下发：`Control(const MotorCmd&)`。
- 维护：`ClearError()`、`SaveZeroPoint()`。

数据结构：

- `Motor::ControlMode`：`MODE_POSITION`、`MODE_VELOCITY`、`MODE_TORQUE`、`MODE_CURRENT`、`MODE_MIT`。
- `Motor::MotorCmd`：`mode`、`reduction_ratio`（默认 1.0）、`torque`、`position`、`velocity`、`kp`、`kd`。各驱动实现其中一部分模式，字段的解释由驱动决定，例如 `RMMotor` 的 `MODE_CURRENT` 从 `velocity` 字段读取归一化电流，详见各驱动的 README。
- `Motor::Feedback`：`error_id`、`state`、`position`（原始角度）、`abs_angle`、`velocity`（转速）、`omega`（角速度）、`torque`、`temp`。`abs_angle` 是 `LibXR::CycleValue<float>`，即归一化到 [0, 2π) 的单圈角；多圈角度由上层用相邻两次 `abs_angle` 的差值累加得到，`CycleValue` 相减得到 [-π, π) 的最短差。

接入新驱动 `MyMotor`：`class MyMotor : public Motor`，实现全部纯虚函数；在 `Control()` 中按 `ControlMode` 分发到底层协议，在 `Update()` 中刷新 `Feedback`。上层模块继续使用 `Motor&`。`Update()` 与 `Control()` 在固定周期调用。

Motor is a library Module (`standalone: false`). Business Modules hold only a `Motor&` or `Motor*`, through which the differences between motor drivers (for example `RMMotor` and `DMMotor`) are unified. Motor unifies the following three aspects:

- The control command structure: position, velocity, torque, current and MIT.
- The feedback structure: angle, speed, angular velocity, torque, temperature and error code.
- The abstract interface that upper-layer Modules depend on.

Driver Modules and business Modules include `Motor.hpp` and list `QDU-Robomaster/Motor` in the `depends` of their own manifest, which pulls it into the project.

`Motor` is a pure virtual interface:

- Lifecycle: `Enable()`, `Disable()`, `Relax()`.
- Feedback refresh: `LibXR::ErrorCode Update()`.
- Feedback read: `const Feedback& GetFeedback()`.
- Control output: `Control(const MotorCmd&)`.
- Maintenance: `ClearError()`, `SaveZeroPoint()`.

Data structures:

- `Motor::ControlMode`: `MODE_POSITION`, `MODE_VELOCITY`, `MODE_TORQUE`, `MODE_CURRENT`, `MODE_MIT`.
- `Motor::MotorCmd`: `mode`, `reduction_ratio` (default 1.0), `torque`, `position`, `velocity`, `kp`, `kd`. Each driver implements a subset of the modes and defines how the fields are interpreted; for example the `MODE_CURRENT` of `RMMotor` reads the normalized current from the `velocity` field, see the README of each driver.
- `Motor::Feedback`: `error_id`, `state`, `position` (raw angle), `abs_angle`, `velocity` (speed), `omega` (angular velocity), `torque`, `temp`. `abs_angle` is a `LibXR::CycleValue<float>`, a single-turn angle normalized to [0, 2π); the upper layer accumulates a multi-turn angle from the difference of two consecutive `abs_angle` values, where subtracting `CycleValue` gives the shortest difference in [-π, π).

To attach a new driver `MyMotor`: `class MyMotor : public Motor` implements all pure virtual functions; `Control()` dispatches to the low-level protocol by `ControlMode`, and `Update()` refreshes `Feedback`. Upper-layer Modules keep using `Motor&`. `Update()` and `Control()` are called at a fixed period.

## 2. 构造接口 / Constructor

`Motor` 是由驱动类继承的纯虚接口，构造由各驱动模块定义。基类提供虚析构函数：

```cpp
virtual ~Motor() = default;
```

依赖：无。配置参数：无。

`Motor` is a pure virtual interface inherited by the driver classes, and construction is defined by each driver Module. The base class provides the virtual destructor shown above.

Dependencies: none. Configuration parameters: none.

## 3. Topic

无 / None

## 4. 配置示例 / Configuration Example

Motor 是库，`xrobot instance add` 输出：

Motor is a library, and `xrobot instance add` prints:

```text
$ xrobot instance add QDU-Robomaster/Motor
QDU-Robomaster/Motor is a library (standalone: false) and cannot be instantiated
```

在 `User/xrobot.yaml` 中配置的是驱动模块的实例；业务模块以 `Motor&` 接收这些实例。C++ 中的典型用法：

The instances configured in `User/xrobot.yaml` are those of the driver Modules, and business Modules receive them as `Motor&`. Typical use in C++:

```cpp
void DriveMotor(Motor& motor)
{
  motor.Update();
  const Motor::Feedback& fb = motor.GetFeedback();

  Motor::MotorCmd cmd{};
  cmd.mode = Motor::MODE_TORQUE;
  cmd.torque = 0.5f;
  cmd.reduction_ratio = 1.0f;
  motor.Control(cmd);
}
```

## 5. 依赖与硬件 / Dependencies and Hardware

依赖：LibXR。

硬件：由派生自 `Motor` 的驱动模块接入具体的电机。

Dependencies: LibXR.

Hardware: the driver Modules derived from `Motor` connect the concrete motors.
