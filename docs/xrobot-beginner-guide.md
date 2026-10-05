# XRobot 新手上路：一边看懂框架，一边补 C++

> 写给「C++ 还没学完 + 官方文档看不懂」的人。
>
> 官方文档 <https://xrobot.work/docs/concept> 讲的是**设计取向**，默认你已经会 C++、写过驱动、懂 RTOS。
> 这份文档反过来：**从你会的东西出发**，用你工程里真实存在的代码，一步步把框架和 C++ 一起讲明白。
>
> 读完的目标：你能看懂这个工程 80% 的代码，并且敢自己动手改。

---

## 怎么用这份文档（先读这 10 行）

1. **不要一次读完。** 一次只读一章，读完立刻打开对应的 `.hpp` 看一眼。
2. **标注「🔧 动手」的地方，真的去敲一遍命令。** 看会 ≠ 会。
3. **遇到看不懂的符号，先别查。** 记在纸上，继续往下读。80% 的疑问会在后面自动消失。
4. **不要点进 `Middlewares/Third_Party/LibXR/` 的深处。** 那是「查」的，不是「读」的。
5. 配套文件：
   - `docs/xrobot-getting-started.md`：**速查版**（改哪个文件、跑哪条命令）
   - `docs/libxr-api-cheatsheet.md`：**抄代码版**（写代码时查一行）
   - 本文：**教学版**（为什么这么写、C++ 是什么意思）

---

# 第 0 章 先把「看不懂」这件事拆开

## 0.1 你现在的感受是正常的

你打开 `BMI088.hpp`，看到 650 行，里面全是：

```cpp
: gyro_range_(gyro_range),
  topic_gyro_(LibXR::Topic::CreateTopic<decltype(gyro_data_)>(gyro_topic_name)),
  cs_accl_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_accl_cs"})),
  ...
```

这不是「你太菜」，这是**两种东西叠在一起**了：

| 叠加的东西 | 例子 | 难度 |
| --- | --- | --- |
| C++ 语法 | `template`、`decltype`、初始化列表、lambda | 学会就好 |
| 框架设计 | 为什么要 `hw.FindOrExit`、为什么要 Topic、为什么构造函数里干这么多事 | 需要理解意图 |

新手常犯的错：**把「框架设计」当成「C++ 语法」去死磕**，结果两头都卡住。
本文的每一节都会明确告诉你：这是 **C++ 问题** 还是 **设计问题**。

## 0.2 读代码的三条止损规则

> 这三条来自 `docs/libxr-api-cheatsheet.md`，是一样的道理，但值得再强调一遍。

**规则 1：不认识的函数，只记「输入什么、返回什么」，绝不点进去。**
你在 90% 的调用上永远不需要知道它的实现。

**规则 2：万一必须看一个函数，只看 3 个地方**——签名（参数和返回）、第一行、最后的 `return`。中间的循环、宏、模板全部跳过。

**规则 3：一次只带一个问题读代码。**
同时问两个问题，就是你现在「看不进去」的原因。30 分钟没答案就写下来问人。

## 0.3 为什么库代码看起来无穷无尽

因为库代码为了**通用**，每一层都包了一层抽象。
你读的不是「一个程序」，是别人多年的抽象设计。

```
你看到的:   hw.template FindOrExit<LibXR::GPIO>({"led"})
实际发生:   遍历别名链表 → 比对字符串 → 比对类型 ID → static_cast → 找不到就致命错误
你需要的:   知道它「按名字+类型要一个硬件，找不到就死」
```

**这东西是查的，不是懂的。** 本文会帮你建立「什么时候该查什么」的直觉。

---

# 第 1 章 这个框架到底在解决什么问题

## 1.1 不用框架的话，代码长什么样

假设没有 XRobot，你要做一个「读 BMI088 并通过串口打印」的板子。你大概会写成这样：

```c
/* main.c —— 传说中的 800 行 */
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart7;
GPIO_TypeDef* CS_PORT = GPIOA;
uint16_t CS_PIN = GPIO_PIN_4;

int main(void) {
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI2_Init();
  MX_UART7_Init();
  BMI088_Init(&hspi2, CS_PORT, CS_PIN);   // 传了一堆硬件细节
  while (1) {
    float gx, gy, gz;
    BMI088_ReadGyro(&gx, &gy, &gz);
    printf("gyro: %f %f %f\r\n", gx, gy, gz);
    HAL_Delay(10);
  }
}
```

问题在哪？

- **业务代码知道硬件细节**：`BMI088_Init` 要传 SPI 句柄、CS 引脚。换一块板子，引脚变了，所有业务代码都得跟着改。
- **想加第二个传感器**：`main.c` 越写越长。
- **想换个 MCU**：`HAL_xxx` 全得重写。
- **一个模块崩了，整个系统崩**：没有隔离。

## 1.2 框架的分工：把「硬件」和「业务」拆开

XRobot / LibXR 的核心思路只有一句话：

> **业务模块不知道引脚是哪个，只用「名字」要硬件。**

```
没有框架：
  业务代码 ──直接调用──> HAL_SPI / HAL_GPIO / 具体引脚

有框架：
  业务模块 ──"我要一个叫 spi_bmi088 的 SPI"──> LibXR::SPI（抽象接口）
                                                    ▲
                                                    │ 实现
                                              STM32SPI（封装 HAL）
                                                    ▲
                                                    │ 用别名注册
                                          app_main.cpp: {"spi2", "spi_bmi088"}
```

于是：

- 换引脚？只改 `app_main.cpp` 一行注册，模块代码**一行不用动**。
- 换 MCU？换掉 `STM32SPI` 这个实现，模块代码**一行不用动**。
- 加模块？只要它声明「我需要什么硬件」，插上去就行。

这就是**依赖注入（Dependency Injection）**。别被这个词吓到，它说的就是上面这件事：**不要自己 new 硬件，让别人发给你。**

## 1.3 三个名字，千万别混

新手最容易晕的就是这三个词。记住它们**各自负责一段流水线**：

| 名字 | 是什么 | 干什么 | 你碰不碰 |
| --- | --- | --- | --- |
| **LibXR** | C++ 库（`Middlewares/Third_Party/LibXR`） | 驱动抽象 + 系统原语 + 中间件 + 数学工具 | 基本不改，只调用 |
| **libxr** | Python 工具（命令 `xr_*`） | 读 CubeMX 的 `.ioc` → 生成 `app_main.cpp`、`.config.yaml` 等 | 改生成物的 `User Code` 区 |
| **xrobot** | Python 工具（命令 `xrobot_*`） | 管模块仓库、读模块清单 → 生成 `xrobot_main.hpp` | 改 `xrobot.yaml`，然后重新生成 |

一个生活比喻：

- **LibXR** = 国标插座。规定了「两孔/三孔、220V」。
- **libxr（生成器）** = 装修队。看你的户型图（`.ioc`），把墙上的插座（`app_main.cpp` 里的硬件对象）装好。
- **xrobot（模块管理器）** = 家电清单。列出「我要一台冰箱、一台洗衣机」，并负责把它们插到插座上（`xrobot_main.hpp` 里实例化）。

## 1.4 代码是怎么生成出来的（你只碰两个地方）

```
STM32CubeMX (.ioc)                     ← 唯一真源：引脚 / 外设 / 时钟
        │  xr_gen_code / xr_cubemx_cfg
        ▼
.config.yaml  (device_aliases)         ← 结构化的硬件结果
        │  xr_gen_code
        ▼
User/app_main.cpp                      ← 造硬件对象 + 注册别名
User/libxr_config.yaml                 ← 缓冲区 / 栈大小 / 用哪个 RTOS

Modules/<模块>/*.hpp (MANIFEST 注释)   ┐
User/xrobot.yaml (要跑哪些实例、参数)   │  xrobot_gen_main
        ▼                             ┘
User/xrobot_main.hpp                   ← 模块实例化 + 主循环（生成物，别手改）

        ▼  cmake --build --preset Debug
   NewDM.elf / NewDM.hex
```

**你日常只碰两个地方：**

- `User/xrobot.yaml` —— 要跑哪些模块、每个模块什么参数。
- `Modules/<你的模块>/*.hpp` —— 你自己的业务代码。

---

# 第 2 章 C++ 最小知识包（全部来自你自己的工程）

> 这一章是本文的核心。每一节都按同一个格式：
> **① C++ 是什么 → ② 在你工程里长什么样 → ③ 框架为什么要这么写**。
>
> 出现的代码全部来自 `Modules/BlinkLED/BlinkLED.hpp`、`Modules/BMI088/BMI088.hpp`、
> `User/app_main.cpp`、`User/xrobot_main.hpp`，你可以随时打开对照。

## 2.1 头文件、`#pragma once`、`#include`

**① C++ 是什么**
C++ 把代码分成「声明」和「实现」。`.hpp` 头文件负责声明（告诉别人我有什么），`.cpp` 负责实现。`#include` 就是「把那个文件的内容抄到这儿」。`#pragma once` 保证同一个头文件在一次编译里只被抄一遍（防止重复定义）。

**② 在你工程里**

`Modules/BlinkLED/BlinkLED.hpp:1`

```cpp
#pragma once

#include "app_framework.hpp"   // 提供 LibXR::Application / HardwareContainer
#include "gpio.hpp"            // 提供 LibXR::GPIO
#include "timer.hpp"           // 提供 LibXR::Timer
```

**③ 为什么要这样**
每个模块只 `#include` 自己真正用到的东西。你数一下 `BlinkLED.hpp` 的 include，就知道这个模块只跟 **GPIO + Timer + 框架基类** 打交道——一秒钟就能看出它的依赖范围。

> 💡 **C++ 提示**：头文件里写的是「我需要什么」，不是「我实现了什么」。
> LibXR 的接口（如 `gpio.hpp`）和 STM32 的实现（`stm32_gpio.hpp`）是分开的两个文件，这正是后面 2.6 节「接口与实现分离」的物理体现。

---

## 2.2 命名空间 `namespace`：同名函数不打架

**① C++ 是什么**
`namespace` 是给名字加前缀，避免不同库、不同人写的东西重名。`LibXR::GPIO` 的意思是「`LibXR` 这个命名空间里的 `GPIO`」。

**② 在你工程里**

`User/app_main.cpp:22`

```cpp
using namespace LibXR;      // 之后可以写 GPIO 而不是 LibXR::GPIO

STM32GPIO CS1_ACCEL(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin);   // 用的是 STM32 实现
```

`User/xrobot_main.hpp:7`

```cpp
static void XRobotMain(LibXR::HardwareContainer &hw) {   // 也可以写全名，更清晰
  using namespace LibXR;                                 // 局部 using，只在函数里生效
  ApplicationManager appmgr;
```

**③ 为什么要这样**
LibXR 的所有东西都在 `LibXR::` 里，不会和 HAL、FreeRTOS、Eigen 撞名。你在自己的模块里也可以用 `namespace my_robot { ... }` 把自己的代码包起来。

> 💡 **`using namespace` 放哪？**
> 放全局（文件顶部）= 整个文件都简化名字；放函数里 = 只有这个函数简化。
> 框架给你生成的代码是「函数里 `using`」，这是更安全的写法。**你照抄就行。**

---

## 2.3 `class` 和 `struct`：把数据和方法打包在一起

**① C++ 是什么**
`class` 是「把一组数据 + 一组操作这些数据的函数打包成一个新类型」。

- 数据叫**成员变量**（member variable）
- 函数叫**成员函数**（member function / method）

`struct` 和 `class` 几乎一样，唯一区别是默认访问权限：`struct` 默认 `public`，`class` 默认 `private`。

**② 在你工程里**

`Modules/BlinkLED/BlinkLED.hpp:21`

```cpp
class BlinkLED : public LibXR::Application {
 public:
  // ... 构造函数 ...

  static void BlinkTaskFun(BlinkLED* blink) {   // 成员函数
    blink->flag_ = !blink->flag_;
    blink->led_->Write(blink->flag_);
  }

  void OnMonitor() override {}                  // 成员函数（框架会调它）

 private:
  bool flag_ = false;                    // 成员变量：当前灯亮不亮
  LibXR::GPIO* led_;                     // 成员变量：指向 LED 硬件
  LibXR::Timer::TimerHandle timer_handle_;  // 成员变量：指向定时器任务
};
```

**③ 为什么要这样**
`BlinkLED` 这个类把「LED 状态 `flag_`」「LED 硬件 `led_`」「闪灯定时器 `timer_handle_`」打包成一个独立单元。
所以你可以有 `BlinkLED led1;` 和 `BlinkLED led2;` 两个实例，各自的 `flag_` 互不干扰。

> 💡 **`public` / `private` 是什么？**
> `public` 的东西外人能碰；`private` 的只有类自己能用。
> 这是**封装**：把内部实现藏起来，只暴露必要的口子。

---

## 2.4 构造函数：对象「出生」时自动执行的函数

**① C++ 是什么**
构造函数的名字和类名一样，没有返回值，在你创建对象时**自动**执行。它的职责是：**把对象变成一个可用的状态**。

**② 在你工程里**

`Modules/BlinkLED/BlinkLED.hpp:23`

```cpp
BlinkLED(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
         uint32_t blink_cycle)
    : led_(hw.template FindOrExit<LibXR::GPIO>({"led", "LED", "led1", "LED1"})),
      timer_handle_(LibXR::Timer::CreateTask(BlinkTaskFun, this, blink_cycle)) {
  UNUSED(app);

  LibXR::Timer::Add(timer_handle_);
  LibXR::Timer::Start(timer_handle_);
}
```

拆开看：

| 部分 | 含义 |
| --- | --- |
| `BlinkLED(...)` | 构造函数 |
| `LibXR::HardwareContainer& hw` | 第 1 个参数：硬件容器（一个引用，见 2.7） |
| `LibXR::ApplicationManager& app` | 第 2 个参数：应用管理器 |
| `uint32_t blink_cycle` | 第 3 个参数：自定义参数，来自 `xrobot.yaml` |
| `: led_(...)` | **初始化列表**：在函数体执行之前，先给成员变量赋初值 |
| `{ ... }` | 函数体：真正干活的代码 |

**③ 为什么要这样：这条是 XRobot 最重要的设计思想之一**

注意 `BlinkLED` 的构造函数里发生了很多事：**找硬件、创建定时器、挂载、启动**。
也就是说——**对象一旦被构造出来，它就已经在工作了**。

框架管这叫做「**构造即初始化**」（construction is initialization）。
对应的官方设计思想是：

> 「所有需要堆分配的对象都应该只在初始化时构造一次，**永不析构**。」
> —— 官方《设计思想》

这样一来：

- 不存在「对象存在但还没初始化」的中间状态（这种状态最容易出 bug）。
- 每个硬件资源只被申请一次，运行时不再 `new`/`malloc`，内存行为可预测。

> 🔧 **回头看一眼 `User/xrobot_main.hpp:12`**：模块对象是 `static BMI088 bmi088(...)`。
> `static` 局部变量**只构造一次**，之后每次进函数都用同一个。这就是「构造一次，永不析构」的实现方式。

---

## 2.5 初始化列表 vs 函数体：为什么要在冒号后面写

**① C++ 是什么**
成员变量的初始化有两种写法：

```cpp
// 写法 A：初始化列表（推荐）
Foo::Foo(int x) : value_(x) { }

// 写法 B：函数体里赋值
Foo::Foo(int x) { value_ = x; }
```

对「基本类型」两者差不多；但对「对象类型」差别很大：写法 A 是**直接构造**，写法 B 是**先默认构造再赋值**，多一次开销，而且如果这个成员**没有默认构造函数**，写法 B 直接编译不过。

**② 在你工程里**

`Modules/BMI088/BMI088.hpp:198`（做了删减）

```cpp
BMI088(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
       GyroFreq freq, AcclFreq accl_freq, ...)
    : gyro_range_(gyro_range),
      accel_range_(accl_range),
      topic_gyro_(LibXR::Topic::CreateTopic<decltype(gyro_data_)>(gyro_topic_name)),
      cs_accl_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_accl_cs"})),
      spi_(hw.template FindOrExit<LibXR::SPI>({"spi_bmi088", "spi1", "SPI1"})),
      op_spi_(sem_spi_),
      gyro_data_key_(*hw.template FindOrExit<LibXR::Database>({"database"}), ...) {
  app.Register(*this);
  // ... 后面还有很多：注册中断回调、Init()、建线程、建定时器
}
```

**③ 为什么要这样**
`topic_gyro_`、`cs_accl_`、`spi_` 这些成员都是对象类型，且需要**立刻**拿到硬件才能构造。
用初始化列表，它们在做函数体之前就已经是可用状态了，函数体里就能直接 `app.Register(*this)`。

> 💡 **读代码技巧（对应第 0 章规则 1）**
> 冒号后面这一长串，你**不需要逐字读懂**。只要看出「每个成员变量各自拿到了什么」就够了：
> `topic_gyro_` 拿到一个 Topic，`cs_accl_` 拿到一个 GPIO 指针，`spi_` 拿到一个 SPI 指针。

---

## 2.6 继承、虚函数、`override`、纯虚函数（本节最重要）

**① C++ 是什么**

- **继承**：`class 子类 : public 父类` 表示「子类是一种父类」。父类的成员，子类都有。
- **虚函数 `virtual`**：允许子类**改写**父类的行为。调用时按对象的**真实类型**去执行，这叫做「多态」。
- **`override`**：告诉编译器「我是在改写父类的虚函数」，写错了会报错（防手滑）。
- **纯虚函数 `= 0`**：父类只声明、不实现，强制子类必须实现。有纯虚函数的类叫**抽象类**，不能直接创建对象，只能被继承。

**② 在你工程里**

父类，`LibXR/src/middleware/app_framework/application.hpp:22`：

```cpp
class Application {
 public:
  virtual void OnMonitor() = 0;      ///< 纯虚函数：必须由子类实现
  virtual ~Application() = default;  ///< 虚析构函数：多态删除时需要
};
```

子类，`Modules/BlinkLED/BlinkLED.hpp:21`：

```cpp
class BlinkLED : public LibXR::Application {
 public:
  void OnMonitor() override {}   // 实现父类要求的 OnMonitor()，空实现也算实现
};
```

框架怎么用它，`application.hpp:72`：

```cpp
void MonitorAll() {
  app_list_.Foreach<Application*>([](Application* app) {
    app->OnMonitor();     // 这里不需要知道 app 到底是 BlinkLED 还是 BMI088
    return ErrorCode::OK; // 多态：自动调用到正确的那个版本
  });
}
```

**③ 为什么要这样**

这就是框架的**插件机制**：

- 框架只认识 `LibXR::Application` 这个「插座」。
- 你写任何模块，只要继承它、实现 `OnMonitor()`，就能被 `MonitorAll()` 统一调度。
- 框架**完全不需要知道**你写了多少个模块、模块叫什么名字。

官方说的「**模块可插拔化接入，支持快速扩展与平台无关运行**」，代码上就是这几行。

> 💡 **为什么 `OnMonitor()` 要 `override`？**
> 不写也能编译（C++ 里叫隐式改写），但写 `override` 后，万一你签名打错了，编译器会立刻报错。
> 框架生成的模块模板都带 `override`，**照着写**。

> ⚠️ **`virtual ~Application() = default;` 是干嘛的？**
> 当通过父类指针 `Application*` 删除子类对象时，如果析构函数不是虚的，只会调用父类析构，子类的资源不会释放。
> 框架里对象都是「永不析构」的，但作者还是加上了——这是 C++ 的规范做法。你只要知道「有继承就配虚析构」即可。

---

## 2.7 指针 `*` 和引用 `&`：它们到底是啥

**① C++ 是什么**

- **指针 `T* p`**：存的是「地址」。可以指向空（`nullptr`），可以改指向别人。
- **引用 `T& r`**：是某个对象的**别名**。必须一开始就绑定，之后不能换，也不能为空。

用 `*` 取指针指向的东西，用 `&` 取一个对象的地址。

**② 在你工程里**

指针，`BlinkLED.hpp` 的私有成员：

```cpp
LibXR::GPIO* led_;    // 「指向一个 GPIO 的指针」，可能为 nullptr
```

成员函数里用它：

```cpp
blink->led_->Write(blink->flag_);
//     ^^^^^  ^  ^
//     指针    解引用  调用成员函数
```

引用，构造函数参数：

```cpp
BlinkLED(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app, ...)
//                            ^                        ^
//                          引用                      引用
```

调用处，`User/app_main.cpp:234`：

```cpp
XRobotMain(peripherals);   // 直接传对象本身，不用加 &
```

`User/xrobot_main.hpp:7`：

```cpp
static void XRobotMain(LibXR::HardwareContainer &hw) {
```

**③ 为什么框架这样设计：这是「接口与实现分离」的入口**

看这一行，`BMI088.hpp:211`：

```cpp
cs_accl_(hw.template FindOrExit<LibXR::GPIO>({"bmi088_accl_cs"}))
```

它返回的是 `LibXR::GPIO*`——**指向抽象基类的指针**。
而 `app_main.cpp:146` 传进去的其实是 `STM32GPIO`（STM32 的具体实现）。

```cpp
STM32GPIO CS1_ACCEL(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin);
//  ↑ STM32 实现            内部其实继承自 LibXR::GPIO
```

因为 `STM32GPIO` 是 `LibXR::GPIO` 的子类，所以「`STM32GPIO` 的地址」可以安全地当成「`LibXR::GPIO*`」使用（向上转型）。
BMI088 拿到这个指针后，只调用 `Write()` / `Read()` / `RegisterCallback()` 这些**抽象接口**，完全不知道底下是 HAL、是 STM32、还是 Linux 的 sysfs。

> 💡 **好处**：把 `STM32GPIO` 换成 `CH32GPIO`，BMI088 一行都不用改。
> 这就是官方「**接口中不应出现任何平台相关类型**」这条设计思想的落地。

> 💡 **为什么参数用 `&`（引用）而不是 `*`（指针）？**
> `hw` 和 `app` 是**一定存在**且**不允许为空**的。引用天然表达「不能为空、不可换绑」；
> 指针 `led_` 则表示「可能还没拿到 / 可以指向不同实现」。**用哪种，取决于语义，不只是语法。**

---

## 2.8 `enum class`：给数字起名字

**① C++ 是什么**
`enum`（枚举）用来把「一组有意义的整数」起名字。
`enum class`（强类型枚举）比老式 `enum` 更安全：必须写 `GyroRange::DEG_2000DPS`，不能隐式转成 `int`，也不会和别的枚举撞名。

**② 在你工程里**

`BMI088.hpp:101` 起：

```cpp
enum class GyroRange : uint8_t {
  DEG_2000DPS = 0x00,
  DEG_1000DPS = 0x01,
  DEG_500DPS  = 0x02,
  ...
};
```

用的时候，`User/xrobot.yaml` 写的是名字，`User/xrobot_main.hpp:12` 生成的是带命名空间的完整写法：

```cpp
static BMI088 bmi088(
    hw, appmgr,
    BMI088::GyroFreq::GYRO_2000HZ_BW532HZ,    // 陀螺仪频率
    BMI088::AcclFreq::ACCL_1600HZ,            // 加速度计频率
    BMI088::GyroRange::DEG_2000DPS,           // 量程
    BMI088::AcclRange::ACCL_24G,
    ...);
```

**③ 为什么要这样**

看 `BMI088.hpp:296`：

```cpp
WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_RANGE,
            static_cast<uint8_t>(accel_range_));
```

底层寄存器要的是 `0x00 ~ 0x03` 这种裸数字。但**你的配置里写的是 `ACCL_24G`**，可读性完全不同。
枚举就是「**人话** ↔ **寄存器值**」之间的翻译层。`static_cast<uint8_t>` 是「我明确知道我在做类型转换」的写法（比 C 风格的 `(uint8_t)x` 更安全、更好搜）。

> 💡 **`uint8_t` / `int16_t` 是啥？**
> 在 `<cstdint>` 里定义的固定宽度整数：`uint8_t` = 无符号 8 位（0~255），`int16_t` = 有符号 16 位。
> 嵌入式里**永远不要用 `int`/`char` 去表示硬件数据**，因为宽度依赖平台。LibXR 全程用固定宽度类型。

---

## 2.9 `static` 的四种意思（新手最容易被绕晕的关键字）

`static` 在不同位置意思完全不同。你工程里四种都有：

**① 文件级 `static` —— 「这个函数只在本文件可见」**

`User/xrobot_main.hpp:7`

```cpp
static void XRobotMain(LibXR::HardwareContainer &hw) {
```

这个 `XRobotMain` 只会被 `app_main.cpp`（它 `#include` 了 `xrobot_main.hpp`）使用，不需要给别的文件用。
好处：链接时不会和其他 `.cpp` 里的同名函数冲突。这叫**内部链接（internal linkage）**。

**② 静态成员函数 —— 「不属于某个对象，属于这个类」**

`BlinkLED.hpp:60`

```cpp
static void BlinkTaskFun(BlinkLED* blink) {
  blink->flag_ = !blink->flag_;
  blink->led_->Write(blink->flag_);
}
```

没有 `static` 的成员函数，调用时必须先有个对象（`obj.Func()`），而且函数内部能直接用 `this`。
加了 `static`，它就**没有 `this`**，只能通过参数拿对象。
**这正是它能被当成普通函数指针传给定时器的原因**（见 2.11）：

```cpp
LibXR::Timer::CreateTask(BlinkTaskFun, this, blink_cycle)
//                        ^^^^^^^^^^^^  函数指针    ^^^^ 传进去当参数
```

> ⚠️ **为什么不能直接传 `&BlinkLED::BlinkTaskFun`（非静态成员函数）？**
> 因为非静态成员函数隐含一个 `this` 参数，它的类型不是普通函数指针，C 风格的定时器接口装不下它。
> **框架约定：回调函数必须是「静态成员函数」或「自由函数」，对象通过参数传进去。** 记住这条就够用了。

**③ 静态局部变量 —— 「只初始化一次，活到程序结束」**

`User/xrobot_main.hpp:12`

```cpp
static BMI088 bmi088(hw, appmgr, ...);   // 在 XRobotMain 里
```

`XRobotMain` 只会被调用一次，但即使被调用多次，`bmi088` 也只会构造这一次。
更重要的是：**它不会在函数返回时销毁**（局部变量正常是函数结束就析构）。配合「构造即初始化、永不析构」，行为完全可预测。

**④ 静态数据成员 —— 「整个类共享一份」**

`BMI088.hpp:138`

```cpp
static constexpr float M_DEG2RAD_MULT = 0.01745329251f;
```

所有 `BMI088` 对象共用这一个常量。`constexpr` 表示「编译期就能算出结果」的常量——比 `#define` 更安全（有类型、有作用域）。

**③ 为什么要这样：对应「运行时不做内存分配」**

「静态局部变量 + 构造即初始化」组合起来，效果是：**所有模块对象在系统启动阶段一次性构造完毕，之后永不销毁、永不重新分配。**
于是运行时就没有「内存碎片」「分配失败」这类问题。这就是官方那条：

> 「作为一个稳定的嵌入式系统，所有需要堆分配的对象都应该**只在初始化时构造一次，永不析构**。」

---

## 2.10 模板 `template <typename T>`：同一份代码适配多种类型

**① C++ 是什么**
模板是「**给类型留个空**」。写一次代码，编译器对每种实际用到的类型各生成一份。

```cpp
template <typename T>
T Max(T a, T b) { return a > b ? a : b; }

Max(1, 2);        // 编译器生成 Max<int>
Max(1.5, 2.5);    // 编译器生成 Max<double>
```

**② 在你工程里**

`HardwareContainer` 的查找接口（`hardware.hpp:63`）：

```cpp
template <typename T>
T* Find(const char* alias) const {
  const auto wanted_id = TypeID::GetID<T>();   // 拿到「类型身份证」
  // ... 在别名链表里同时比对名字和类型 ...
}
```

你的模块这样用（`BlinkLED.hpp:25`）：

```cpp
hw.template FindOrExit<LibXR::GPIO>({"led", "LED", "led1", "LED1"})
//                ^^^^^^^^^^^^^^^ T = LibXR::GPIO
```

于是返回类型就是 `LibXR::GPIO*`。
`BMI088.hpp:215` 换成 `LibXR::SPI`，返回类型就变成 `LibXR::SPI*`。**同一份查找代码，通吃所有硬件类型。**

**③ 为什么要这样**

硬件容器里存的是「一堆不同类型的对象 + 各自的名字」。用模板 + 类型 ID 比对，就能做到：

- 名字对了但类型不对（比如你想要 `GPIO`，但 `"spi2"` 注册的是 `SPI`）→ 找不到 → 报错。
- 这叫**类型安全查找**。

> 💡 **那个 `.template` 是什么鬼？**
> 在 C++ 里，`hw` 的类型依赖模板参数，编译器不敢确定 `FindOrExit<T>` 里的 `<` 是「模板参数开始」还是「小于号」。
> 加 `.template` 就是告诉编译器：「后面那个 `<` 是模板，不是小于号。」
> **这是纯粹的语法补丁，理解到这一层就够了，不用深究。** 你写自己模块时照抄即可。

> 💡 **`decltype(x)` 是什么？**
> 「推导 `x` 的类型」。`BMI088.hpp:209`：
> ```cpp
> topic_gyro_(LibXR::Topic::CreateTopic<decltype(gyro_data_)>(gyro_topic_name))
> ```
> `gyro_data_` 是 `Eigen::Matrix<float,3,1>`，所以 `CreateTopic` 的模板参数就是它。
> 好处：**以后改 `gyro_data_` 的类型，这一行自动跟着变，不会写错。**

---

## 2.11 lambda 与回调：把「一段代码」当参数传

**① C++ 是什么**

- **函数指针**：变量里存一个函数的地址，类型是 `void(*)(int)` 这种。
- **lambda**：就地写一个匿名函数，语法是 `[](参数){ 函数体 }`。`[]` 里写要「捕获」的外部变量。
- **回调（callback）**：把「事情发生后要执行什么」提前交给别人，别人在合适的时候调你。

**② 在你工程里**

函数指针版（定时器），`BlinkLED.hpp:28` + `:59`：

```cpp
timer_handle_(LibXR::Timer::CreateTask(BlinkTaskFun, this, blink_cycle))
//                                   ^^^^^^^^^^^ 函数指针  ^^^^ 调用时传给它的参数

static void BlinkTaskFun(BlinkLED* blink) {
  blink->flag_ = !blink->flag_;
  blink->led_->Write(blink->flag_);
}
```

lambda 版（中断回调），`BMI088.hpp:230`：

```cpp
auto gyro_int_cb = LibXR::GPIO::Callback::Create(
    [](bool in_isr, BMI088* bmi088) {          // ← lambda：捕获列表 [] 为空
      auto timestamp = LibXR::Timebase::GetMicroseconds();
      bmi088->dt_gyro_ = timestamp - bmi088->last_gyro_int_time_;
      bmi088->last_gyro_int_time_ = timestamp;
      bmi088->sample_timestamp_ = timestamp;
      bmi088->new_data_.PostFromCallback(in_isr);   // 只是「发个信号」，不干活
    },
    this);                                     // ← 把 this 绑进回调，当 BMI088* 用
```

`Create` 做了两件事：把 lambda 存起来，把 `this`（当前对象的地址）绑成第一个参数。
所以回调真正执行时，`bmi088` 这个参数就是当初传进去的 `this`。

**③ 为什么要这样：这是「ISR 只管交接，线程负责展开」的核心**

看这个回调，它**只做三件事**：打时间戳、记状态、发信号。**它没有读传感器、没有解析数据、没有打印。**
真正的活在外面的线程里干，`BMI088.hpp:375`：

```cpp
static void ThreadFunc(BMI088* bmi088) {
  while (true) {
    if (bmi088->new_data_.Wait(50) == LibXR::ErrorCode::OK) {   // 等中断发来信号
      bmi088->RecvGyro();          // 读
      bmi088->ParseGyroData();     // 解析
      bmi088->RecvAccel();
      bmi088->ParseAccelData();
      bmi088->topic_accl_.Publish(...);   // 发出去
      bmi088->topic_gyro_.Publish(...);
    }
  }
}
```

一条数据要经过 **中断 → 信号量 → 线程**，故意分成两段。

> 💡 **为什么中断里不能干这些活？**
> 中断会打断一切。你在中断里花 100 µs，系统的实时性就烂 100 µs，而且中断里不能等、不能睡、不能加锁。
> 所以官方规定：「**一切回调/中断都必须是无阻塞的**」——ISR 只负责「交接」，耗时的事情交给线程。

---

## 2.12 `auto`、`const`、`nullptr`、`static_cast`

**① C++ 是什么 / ② 在你工程里**

`auto`：让编译器自己推断类型。`BMI088.hpp:280`

```cpp
auto accl_id = ReadSingle(Device::ACCELMETER, BMI088_REG_ACCL_CHIP_ID);
// accl_id 的类型自动 = uint8_t
```

`const`：只读，不许改。`hardware.hpp:63`

```cpp
T* Find(const char* alias) const;   // 末尾的 const = 「这个函数不会修改对象自己」
```

`nullptr`：空指针。**永远用它，不要用 `NULL` 或 `0`。**
`BlinkLED.hpp` 里其实没有空判断（因为 `FindOrExit` 保证非空），但你自己写代码时：

```cpp
if (ptr != nullptr) { ptr->Write(true); }
```

`static_cast`：显式类型转换。`BMI088.hpp:296`

```cpp
WriteSingle(Device::ACCELMETER, BMI088_REG_ACCL_RANGE,
            static_cast<uint8_t>(accel_range_));   // enum class → uint8_t
```

**③ 为什么要这样**

- `auto` 让你少写类型名，但**不降低类型安全**——类型是编译器推的，不是猜的。
- 硬件寄存器操作里，`static_cast<uint8_t>` 是在明确声明「我知道我在把枚举转成字节」，比隐式转换更不容易出错，也方便搜索。

> 💡 **什么时候别用 `auto`？**
> 当推断出来的类型不明显、或者你想强调的是「这是个指针/这是个浮点」时，写全类型更清楚。
> 框架代码两种都有：`auto accl_id`（一眼能懂）、`LibXR::GPIO* led_`（故意写明是指针）。

---

## 2.13 数组和 `Eigen::Matrix`：怎么表示三维数据

**① C++ 是什么**
C 风格数组 `float a[3];` 只是「一块连续内存」，不知道长度，不能直接赋值、不能相加。
工程里更常用 `std::array<float,3>`（有长度、能赋值），以及数学库 `Eigen`。

**② 在你工程里**

C 风格数组（硬件缓冲区），`BMI088.hpp:636`

```cpp
uint8_t rw_buffer_[20];   // 收发数据用的裸缓冲区
```

`std::array`（解析中间结果），`BMI088.hpp:436` 一带

```cpp
std::array<int16_t, 3> raw_int16;
std::array<float, 3> raw;
for (int i = 0; i < 3; i++) { ... }
```

Eigen（业务数据），`BMI088.hpp:637`

```cpp
Eigen::Matrix<float, 3, 1> gyro_data_, accl_data_;   // 3 行 1 列 = 三维向量
```

**③ 为什么要这样**

`Eigen::Matrix<float,3,1>` 不只是「3 个 float」，它自带向量/矩阵运算。`BMI088.hpp` 里可以直接写：

```cpp
accl_data_ = rotation_ * Eigen::Matrix<float, 3, 1>(raw[0], raw[1], raw[2]);
```

**一行完成「四元数 × 向量」的坐标旋转。** 用 C 数组你得自己写 3×3 矩阵乘法。

> 💡 **`Matrix<float, 3, 1>` 里的 `3, 1` 是什么？**
> `Matrix<数据类型, 行数, 列数>`。`3,1` 是列向量（三维点/速度/角速度），`3,3` 是旋转矩阵。
> 模板参数就是 2.10 节讲的「给类型和尺寸留空」。

---

# 第 3 章 上电之后发生了什么（`app_main.cpp` 逐段讲）

> 打开 `User/app_main.cpp`，跟着往下看。这是**唯一**需要你完整理解的启动文件。

## 3.1 先建立「调用链」的地图

```
上电
 └─ Reset_Handler（startup_stm32h723xx.s，汇编）
     └─ main()（Core/Src/main.c，CubeMX 生成）
         └─ app_main()（User/app_main.cpp，CubeMX 调用）
             ├─ 造硬件对象（STM32GPIO / STM32SPI / STM32UART …）
             ├─ 注册别名（HardwareContainer）
             ├─ 接终端、建数据库
             └─ XRobotMain(peripherals)（User/xrobot_main.hpp，生成物）
                 ├─ 构造模块（构造函数里完成初始化）
                 └─ while(true) { MonitorAll(); Sleep(1000); }
```

**记住这张图，再看具体代码就不慌了。**

## 3.2 时间基准 + `PlatformInit`

`User/app_main.cpp:141`

```cpp
STM32TimerTimebase timebase(&htim1);
PlatformInit(2, 4096);
```

**① `timebase`** —— LibXR 需要一个「毫秒时钟」。这里用硬件定时器 TIM1 来提供。
为什么不用 SysTick？代码里有注释（`BMI088.hpp:365` 一带）：SysTick 的优先级最低，会被其他中断干扰，导致 `dt` 测量不准。

**② `PlatformInit(2, 4096)`** —— 签名在 `LibXR/system/freertos/libxr_system.hpp:39`：

```cpp
void PlatformInit(uint32_t timer_pri = 2, uint32_t timer_stack_depth = 512);
```

| 参数 | 你的值 | 含义 |
| --- | --- | --- |
| `timer_pri` | `2` | LibXR 定时器线程的优先级 |
| `timer_stack_depth` | `4096` | 定时器线程的栈深度（字节） |

**③ 为什么这两个参数关键**
LibXR 的 `Timer` 不是「一个定时器对应一个线程」，而是**所有定时任务在同一个线程里排队执行**。
证据，`LibXR/src/system/timer.cpp:27`：

```cpp
void Timer::Add(TimerHandle handle) {
  if (!LibXR::Timer::list_) {
    LibXR::Timer::list_ = new LibXR::LockFreeList();
    thread_handle_.Create<void*>(nullptr, RefreshThreadFunction, "libxr_timer_task",
                                 stack_depth_, priority_);   // 只在这里建一次
  }
  list_->Add(*handle);
}
```

而 `RefreshThreadFunction` 每 1 ms 遍历一次任务链表，把到期的任务依次调一遍。

> ⚠️ **对你的约束（很重要）：**
> 所有定时器回调（包括你模块里的 `BlinkTaskFun`、`ControlTemperature`，以及**终端的命令处理**）
> 都在**同一个线程、同一个栈**上跑。
> - 回调里写耗时/阻塞的代码 → 别人的定时任务一起被卡住。
> - `User/libxr_config.yaml` 里的 `software_timer.stack_depth: 4096` 就是给这个线程的。
>   `app_main.cpp:134` 的注释专门提醒：串口终端敲 `bmi088 show` 这种命令也在这个栈上跑，所以栈不能太小，否则会溢出。

## 3.3 造硬件对象：平台实现的入口

`User/app_main.cpp:146` 起：

```cpp
STM32GPIO CS1_ACCEL(CS1_ACCEL_GPIO_Port, CS1_ACCEL_Pin);
STM32GPIO CS1_GYRO(CS1_GYRO_GPIO_Port, CS1_GYRO_Pin);
STM32GPIO INT1_ACCEL(INT1_ACCEL_GPIO_Port, INT1_ACCEL_Pin, EXTI15_10_IRQn);
STM32GPIO INT1_GYRO(INT1_GYRO_GPIO_Port, INT1_GYRO_Pin, EXTI15_10_IRQn);

STM32PWM pwm_tim12_ch2(&htim12, TIM_CHANNEL_2, false);
STM32SPI spi2(&hspi2, spi2_rx_buf, spi2_tx_buf, 3);
STM32UART usart1(&huart1, usart1_rx_buf, usart1_tx_buf, 5);
...
```

**看清楚三件事：**

1. 类型是 **`STM32xxx`**，不是 `LibXR::GPIO`。这些是**具体实现**，定义在 `LibXR/driver/st/` 下。
2. 构造参数里全是 HAL 句柄（`&hspi2`）、引脚（`CS1_ACCEL_Pin`）、缓冲区。**平台细节只出现在这个文件里。**
3. 这些对象是**普通的局部变量**，建在栈上——但 `app_main` 永不返回，所以它们活到系统结束。

> 💡 **缓冲区 `spi2_rx_buf` / `spi2_tx_buf` 从哪来？**
> `app_main.cpp` 上半部分的 `/* DMA Resources */` 区域，CubeMX 生成器根据 `.ioc` 里的 DMA 配置写出来的。
> `spi2` 构造函数的第 4 个参数 `3`，对应 `User/libxr_config.yaml` 里的 `dma_enable_min_size: 3`——
> 意思是「传输超过 3 字节才用 DMA」。

## 3.4 注册别名：整个框架最关键的一步

`User/app_main.cpp:196`

```cpp
LibXR::HardwareContainer peripherals{
  LibXR::Entry<LibXR::GPIO>({CS1_ACCEL, {"CS1_ACCEL", "bmi088_accl_cs"}}),
  LibXR::Entry<LibXR::GPIO>({CS1_GYRO,  {"CS1_GYRO",  "bmi088_gyro_cs"}}),
  LibXR::Entry<LibXR::GPIO>({INT1_GYRO, {"INT1_GYRO", "bmi088_gyro_int"}}),
  LibXR::Entry<LibXR::SPI>({spi2, {"spi2", "spi_bmi088"}}),
  LibXR::Entry<LibXR::PWM>({pwm_tim12_ch2, {"pwm_bmi088_heat", "pwm_tim12_ch2"}}),
  ...
  LibXR::Entry<LibXR::RamFS>({ramfs, {"ramfs"}}),
  LibXR::Entry<LibXR::Terminal<32, 32, 5, 5>>({terminal, {"terminal"}})
};
```

**逐层拆解这一行：**

```cpp
LibXR::Entry<LibXR::GPIO>({ CS1_ACCEL, { "CS1_ACCEL", "bmi088_accl_cs" } })
//        ^^^^^^^^^^^^^^^^     ^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//        声明"我注册的是GPIO"   实际对象     这个对象的所有别名
```

`Entry` 的定义，`hardware.hpp:29`：

```cpp
template <typename T>
struct Entry {
  T& object;                                  // 引用：被注册的设备本体
  std::initializer_list<const char*> aliases; // 别名列表：可以有好几个名字
};
```

> 💡 **两个没讲过的小知识：**
> - `std::initializer_list<const char*>` 就是「用 `{...}` 包起来的一串东西」，也就是 2.3 节说过的聚合初始化。
> - `Entry` 里存的是 `T&`（引用），所以 `HardwareContainer` **不复制**硬件对象，只是「记了个门牌号」。

**接着看 `HardwareContainer` 怎么用（`hardware.hpp:44`）：**

```cpp
template <typename... Entries>            // 2.10 的模板：接受任意多个 Entry
constexpr HardwareContainer(Entries&&... entries) {
  (Register(std::forward<Entries>(entries)), ...);   // 逗号折叠表达式：逐个注册
}
```

> 💡 **`typename...` 和 `(... , ...)` 是什么？**
> 这是 C++17 的**可变参数模板 + 折叠表达式**，翻译成人话就是「把传进来的每一个参数，挨个执行一次 `Register`」。
> **你现在完全不需要会写它**，只要知道：`HardwareContainer peripherals{ 一堆 Entry };` 会把这些 Entry 全部登记进一张表。

**查找就发生在这里（`hardware.hpp:106`）：**

```cpp
template <typename T>
T* FindOrExit(std::initializer_list<const char*> aliases) const {
  T* result = Find<T>(aliases);     // 挨个别名找，命中第一个就返回
  REQUIRE(result != nullptr);       // 一个都没找到 → 致命错误
  return result;
}
```

`REQUIRE` 的定义，`LibXR/src/core/libxr_def.hpp:308`：

```cpp
#define REQUIRE(arg)                                \
  do {                                              \
    if (!(arg)) {                                   \
      libxr_fatal_error(__FILE__, __LINE__, false); \
    }                                               \
  } while (0)
```

**所以 `hw.FindOrExit<GPIO>({"led"})` 的完整语义是：**
「在注册表里找一个叫 `led` 的 `GPIO`。找到就给我指针；**找不到就调用致命错误处理，程序停在那里**。」

> ⚠️ **这是新手调试时最常撞的墙。**
> 模块跑不起来、串口一片死寂，很可能就是某个 `FindOrExit` 没找到硬件。
> 排查口诀：**把模块 manifest 里的 `required_hardware` 和 `app_main.cpp` 里的别名列表对着看。**

> 💡 **`.config.yaml` 里的 `device_aliases` 是什么？**
> 打开 `.config.yaml`，你会发现有一段和上面几乎一模一样的 `device_aliases:`。
> 那就是生成器读 `.ioc` 得到的「硬件 → 别名」表，`app_main.cpp` 的 `Entry` 列表就是根据它生成的。
> **真想加硬件时，正确的入口是 CubeMX（`.ioc`），不是手改 `app_main.cpp`。**

## 3.5 串口终端：STDIO / RamFS / Terminal

`User/app_main.cpp:186`

```cpp
STDIO::read_  = uart7.read_port_;    // 全局输入端口 = uart7 的接收端
STDIO::write_ = uart7.write_port_;   // 全局输出端口 = uart7 的发送端

RamFS ramfs("XRobot");                                  // 内存文件系统
Terminal<32, 32, 5, 5> terminal(ramfs);                 // 命令行终端
auto terminal_task = Timer::CreateTask(terminal.TaskFun, &terminal, 10);
Timer::Add(terminal_task);                              // 每 10ms 轮询一次终端输入
Timer::Start(terminal_task);
```

`STDIO` 就是「标准输入输出」的缩写（和 C 的 `stdio.h` 同源概念）。设好之后：

- `STDIO::Printf<"x = %f\r\n">(x)` → 从 uart7 发出去。
- `XR_LOG_INFO("...")` / `XR_LOG_WARN("...")` → 也走这里。
- 模块里 `LibXR::RamFS::CreateFile("bmi088", CommandFunc, this)` 注册的命令，能在串口里敲 `bmi088 show 1000 10` 调用。

`Terminal<32, 32, 5, 5>` 的 4 个模板参数来自 `User/libxr_config.yaml` 的 `Terminal:` 段：

```yaml
Terminal:
  read_buff_size: 32      # → 第 1 个 32
  max_line_size: 32       # → 第 2 个 32
  max_arg_number: 5       # → 第 3 个 5
  max_history_number: 5   # → 第 4 个 5
```

> 💡 **`Terminal<32,32,5,5>` 又出现了模板实参。** 这次是「用数字当模板参数」——数组大小在编译期就定死，没有运行时开销。

## 3.6 掉电保存：Database

`User/app_main.cpp:225`

```cpp
STM32Flash flash(FLASH_SECTORS, FLASH_SECTOR_NUMBER);
LibXR::DatabaseRaw<32> database(flash);     // 32 = H7 的 flash word 是 256bit = 32 字节
peripherals.Register(LibXR::Entry<LibXR::Database>{database, {"database"}});
```

**注意这次的写法**：`peripherals` 已经构造完了，所以用 `Register()` 方法**追加**注册。
（`HardwareContainer` 既支持「构造时注册一堆」，也支持「之后补一个」。）

模块侧这样用，`BMI088.hpp:221`：

```cpp
gyro_data_key_(*hw.template FindOrExit<LibXR::Database>({"database"}),
               "bmi088_gyro_data",
               Eigen::Matrix<float, 3, 1>(0.0, 0.0, 0.0))
```

读写成键值对：`key.Set(v)` 写并保存，`key.Load()` 读回，`key.data_` 拿当前值。
这就是 BMI088 陀螺仪标定值在重启后不丢的原因。

## 3.7 `XRobotMain`：模块在这里活过来

`User/xrobot_main.hpp:7`（**生成物，别手改**）

```cpp
static void XRobotMain(LibXR::HardwareContainer &hw) {
  using namespace LibXR;
  ApplicationManager appmgr;

  // Auto-generated module instantiations
  static BMI088 bmi088(
      hw, appmgr,
      BMI088::GyroFreq::GYRO_2000HZ_BW532HZ,
      ...
      45,        // target_temperature
      2048       // task_stack_depth
  );

  while (true) {
    appmgr.MonitorAll();
    Thread::Sleep(1000);
  }
}
```

四行关键信息：

| 代码 | 含义 |
| --- | --- |
| `ApplicationManager appmgr;` | 模块调度器（2.6 节的 `ApplicationManager`） |
| `static BMI088 bmi088(hw, appmgr, ...)` | **构造 = 初始化**。这一行执行完，BMI088 已经在跑了 |
| `appmgr.MonitorAll();` | 调用所有已注册模块的 `OnMonitor()` |
| `Thread::Sleep(1000);` | 每 1000 ms 一轮，值来自 `xrobot.yaml` 的 `global_settings.monitor_sleep_ms` |

> 🔧 **动手：验证「配置 → 代码」的关系**
> 1. 打开 `User/xrobot.yaml`，把 `monitor_sleep_ms: 1000` 改成 `500`。
> 2. 在工程根目录跑：`xrobot_gen_main --output User\xrobot_main.hpp`
> 3. 重新打开 `User/xrobot_main.hpp`，看 `Thread::Sleep(...)` 变成了什么。
>
> 这个练习的目的：**建立「yaml 是输入、hpp 是输出」的直觉。** 以后你改模块参数，就再也不慌了。

## 3.8 完整启动链（背下来）

```
+--------------------- app_main.cpp ---------------------+
| 1. STM32TimerTimebase timebase(&htim1)    起时钟         |
| 2. PlatformInit(2, 4096)                  起定时器线程    |
| 3. STM32GPIO / STM32SPI / STM32UART ...   造硬件对象      |
| 4. STDIO::read_ / write_ = uart7           接终端         |
| 5. RamFS + Terminal                        建命令行       |
| 6. HardwareContainer peripherals{...}      注册别名 ★核心  |
| 7. STM32Flash + DatabaseRaw                掉电保存       |
| 8. XRobotMain(peripherals)  ---------------+             |
+--------------------------------------------+-------------+
                                             v
+--------------------- xrobot_main.hpp --------------------+
| 9.  static BMI088 bmi088(hw, appmgr, ...)               |
|       +- 构造函数执行：                                   |
|          FindOrExit<SPI/GPIO/PWM/Database>()  <- 用别名要硬件 |
|          CreateTopic("bmi088_gyro")            <- 建发布通道  |
|          app.Register(*this)                   <- 登记到调度器 |
|          注册中断回调 + Init() 初始化芯片                     |
|          thread_.Create(...)                   <- 起采集线程   |
|          Timer::CreateTask(...)                <- 起温控定时器 |
| 10. while(true) { appmgr.MonitorAll(); Sleep(1000); }   |
+---------------------------------------------------------+
```

**第 9 步是重点：构造函数执行 = 模块初始化完成。** 这就是「构造即初始化」。

---

# 第 4 章 一个模块长什么样（`BlinkLED` 逐行讲）

> `BlinkLED` 只有 68 行，却是所有模块的骨架。看懂它，你就能看懂任何模块。

## 4.1 模块目录结构

```
Modules/BlinkLED/
├── BlinkLED.hpp           ← 模块实现 + 顶部 MANIFEST 注释（核心）
├── CMakeLists.txt         ← 告诉构建系统「把这个目录加进编译」
├── README.md              ← 由 MANIFEST 自动生成
└── .github/workflows/build.yml   ← CI（本地开发用不到）
```

`Modules/TestModule/` 是 `xrobot_create_mod` 刚生成出来的空壳，可以对照着看：

```cpp
class TestModule : public LibXR::Application {
public:
  TestModule(LibXR::HardwareContainer &hw, LibXR::ApplicationManager &app) {
    // Hardware initialization example:
    // auto dev = hw.template Find<LibXR::GPIO>("led");
  }
  void OnMonitor() override {}
private:
};
```

**这就是最小模块：一个类 + 两个固定函数。**

## 4.2 MANIFEST：写在注释里的「模块说明书」

`BlinkLED.hpp:3`

```cpp
// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 控制 LED 闪烁的简单模块 / A simple module to control LED blinking
constructor_args:
  - blink_cycle: 250
template_args: []
required_hardware: led/LED/led1/LED1
depends: []
=== END MANIFEST === */
// clang-format on
```

**为什么用注释？** 因为这样**不用编译**就能读。`xrobot_gen_main`、`xrobot_mod_parser`、README 生成，都靠解析这段文本。

| 字段 | 作用 | 对应到 C++ |
| --- | --- | --- |
| `module_description` | 一句话描述 | 只给文档用 |
| `constructor_args` | 构造函数的自定义参数 + 默认值 | 构造函数第 3 个及以后的参数 |
| `template_args` | 模板参数（本例无） | `template <...>` |
| `required_hardware` | 需要哪些硬件别名 | 构造函数里的 `FindOrExit` |
| `depends` | 依赖哪些其他模块 | 一般留空（模块间用 Topic 通信，见第 5 章） |

> 🔧 **动手：亲眼看看 MANIFEST 被解析成什么**
> ```powershell
> xrobot_mod_parser --path Modules\BlinkLED\
> ```
> 输出会告诉你这个模块要什么参数、什么硬件。写代码前先跑一下，比翻源码快。

> ⚠️ **`required_hardware: led/LED/led1/LED1` 里的 `/` 是什么意思？**
> 「**任一别名命中即可**」。它和代码里的
> ```cpp
> hw.template FindOrExit<LibXR::GPIO>({"led", "LED", "led1", "LED1"})
> ```
> 是同一个意思：先找 `led`，没有就找 `LED`，再没有就找 `led1`……全都没有才报错。

## 4.3 构造函数逐行拆

`BlinkLED.hpp:23`（完整代码）

```cpp
BlinkLED(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
         uint32_t blink_cycle)
    : led_(hw.template FindOrExit<LibXR::GPIO>(
          {"led", "LED", "led1", "LED1"})),
      timer_handle_(
          LibXR::Timer::CreateTask(BlinkTaskFun, this, blink_cycle)) {
  UNUSED(app);

  LibXR::Timer::Add(timer_handle_);
  LibXR::Timer::Start(timer_handle_);

  auto error_callback = LibXR::Callback<const char*, uint32_t>::Create(
      [](bool in_isr, BlinkLED* led, const char* file, uint32_t line) {
        UNUSED(file);
        UNUSED(line);
        LibXR::Timer::Stop(led->timer_handle_);
        if (!in_isr) {
          while (true) { /* 用特殊闪烁节奏报警 */ }
        }
      },
      this);

  LibXR::Assert::RegisterFatalErrorCallback(error_callback);
}
```

| 代码 | 干什么 | 用到 2.x 哪个知识点 |
| --- | --- | --- |
| `BlinkLED(...)` | 构造函数 | 2.4 |
| `: led_(...)` | 初始化列表，拿 LED 硬件 | 2.5 |
| `hw.template FindOrExit<LibXR::GPIO>({...})` | 按别名 + 类型找 GPIO | 2.7 / 2.10 |
| `CreateTask(BlinkTaskFun, this, blink_cycle)` | 建一个周期任务，回调是静态函数 | 2.9 / 2.11 |
| `LibXR::Timer::Add(...)` | 把任务挂进定时器链表（第一次会创建定时器线程） | 3.2 |
| `LibXR::Timer::Start(...)` | 开始跑 | — |
| `UNUSED(app)` | 「这个参数我故意没用」——**消除编译器警告**，不是语法必需 | — |
| `Callback<...>::Create(lambda, this)` | 注册致命错误回调，出错时用特殊闪烁报警 | 2.11 |

> 💡 **`UNUSED(app)` 是什么？**
> 一个宏，展开后大概是 `(void)app;`——「我确实拿到了这个参数，但我不打算用」。
> 不加的话编译器会警告 `unused parameter`。这不是 C++ 语法，是工程的洁癖。
>
> **注意**：`app` 参数虽然这里没用，但**签名必须保留**，因为这是模块的固定契约（见 4.4）。

## 4.4 一个模块的三条契约（背下来）

只要你写模块，就必须满足这三条，否则 `xrobot_gen_main` 生成的代码编译不过：

**契约 1：继承 `LibXR::Application`，实现 `OnMonitor()`**

```cpp
class BlinkLED : public LibXR::Application {
 public:
  void OnMonitor() override {}   // 不需要周期监控就写空
};
```

**契约 2：构造函数前两个参数固定**

```cpp
MyModule(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
         ...你自己定义的参数...);
```

`hw` 用来要硬件，`app` 用来注册自己（`app.Register(*this)`）。
**自定义参数必须排在它俩后面**，因为生成器会固定先传 `hw, appmgr`。

**契约 3：头文件顶部必须有 MANIFEST 注释块**

```cpp
/* === MODULE MANIFEST V2 ===
module_description: 我的第一个模块
constructor_args:
  - period_ms: 500
template_args: []
required_hardware: led/LED
depends: []
=== END MANIFEST === */
```

**为什么是这三条？**

| 契约 | 对应设计思想 |
| --- | --- |
| 继承 `Application` + `OnMonitor` | 插件机制：框架只认基类，不认具体模块 |
| 固定 `(hw, app, ...)` | 依赖注入：模块不自己造硬件，向容器要 |
| MANIFEST | 元信息驱动代码生成：机器读注释，生成实例化代码 |

## 4.5 不需要自己写模板：`xrobot_create_mod`

```powershell
xrobot_create_mod MyModule --desc "我的模块" --hw led/LED --constructor period_ms=500
```

它会生成 `Modules/MyModule/`（`.hpp` + `README.md` + `CMakeLists.txt` + CI 配置），
里面已经把三条契约的骨架搭好，你只填业务代码。

**生成之后要做两件事：**

1. 把模块加进实例列表（`User/xrobot.yaml` 的 `modules:` 下），或直接：
   ```powershell
   xrobot_add_mod MyModule
   ```
2. 重新生成主函数：
   ```powershell
   xrobot_gen_main --output User\xrobot_main.hpp
   ```

> ⚠️ **如果模块是从 GitHub 拉的第三方模块**，还要先在 `Modules/modules.yaml` 里登记仓库，再跑 `xrobot_init_mod` 同步下来。详见 `docs/xrobot-getting-started.md` 第 5、6 节。

---
# 第 5 章 模块之间怎么说话（Topic）

## 5.1 新手的第一反应，以及为什么它是错的

假设你写了 `BMI088`，现在想让另一个模块 `Balance`（平衡控制）用陀螺仪数据。你的第一反应大概是：

```cpp
// Balance.hpp
#include "BMI088.hpp"        // ← 直接 include 别人

class Balance : public LibXR::Application {
 public:
  Balance(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
          BMI088* imu)       // ← 还要拿到别人对象的指针
      : imu_(imu) {}
 private:
  BMI088* imu_;
};
```

**问题在哪？**

1. `Balance` 和 `BMI088` **编译期绑死**。BMI088 改了构造函数，Balance 也得改。
2. 如果换成 `BMI088_2` 或另一个厂家、另一款 IMU，Balance 要重写。
3. 一个模块 `#include` 另一个模块，依赖会像滚雪球一样越滚越大。
4. **无法跨板子复用**：Balance 只想「要三轴角速度」，不关心是谁给的。

## 5.2 正确做法：通过「名字」收发数据

```
            发布者 (BMI088)                     订阅者 (Balance)
        ┌──────────────────────┐          ┌──────────────────────┐
        │ CreateTopic<Vector3> │          │ 按名字订阅            │
        │   ("bmi088_gyro")    │          │  "bmi088_gyro"        │
        │        │             │          │        ▲              │
        │     Publish(data) ───┼──────────┼────────┘              │
        └──────────────────────┘  只认名字  └──────────────────────┘
                    ↑                                  ↑
                    └──── 双方都不需要知道对方的类型 ────┘
```

**发布者**，`BMI088.hpp:209`：

```cpp
topic_gyro_(LibXR::Topic::CreateTopic<decltype(gyro_data_)>(gyro_topic_name))
//                                          ^^^^^^^^^^^^^^^^^^^^ T = Eigen::Matrix<float,3,1>
```

`BMI088.hpp:389`（采集线程里）：

```cpp
bmi088->topic_accl_.Publish(bmi088->accl_data_, sample_timestamp);
bmi088->topic_gyro_.Publish(bmi088->gyro_data_, sample_timestamp);
```

**订阅者**（这是你自己写模块时会用到的）——用「同步订阅」，最简单：

```cpp
#include "message.hpp"     // Topic 在这里面

class Balance : public LibXR::Application {
 public:
  Balance(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app)
      : gyro_sub_("bmi088_gyro", gyro_),      // ① 按名字订阅，写进 gyro_
        gyro_{0.0f, 0.0f, 0.0f} {
    UNUSED(hw);
    app.Register(*this);
  }

  void OnMonitor() override {}   // 不想在这里等，就别用它（见下面提醒）

  // 在某个线程/定时器回调里：
  void Update() {
    if (gyro_sub_.Wait(10) == LibXR::ErrorCode::OK) {   // ② 等下一帧
      float wx = gyro_.x();                             // ③ 用数据
      float wy = gyro_.y();
      float wz = gyro_.z();
      // ... 你的控制逻辑 ...
    }
  }

 private:
  Eigen::Matrix<float, 3, 1> gyro_;                       // 数据存这里
  LibXR::Topic::SyncSubscriber<Eigen::Matrix<float, 3, 1>> gyro_sub_;
};
```

要点：

| 代码 | 说明 |
| --- | --- |
| `SyncSubscriber<Vector3>("bmi088_gyro", gyro_)` | 构造函数参数是「topic 名字」和「数据存哪」 |
| `gyro_sub_.Wait(10)` | 阻塞等待最多 10 ms，返回 `ErrorCode::OK` 表示收到 |
| `gyro_` | 数据直接写进你的变量，不需要自己拷贝 |

> ⚠️ **不要在 `OnMonitor()` 里 `Wait()`！**
> `OnMonitor()` 也是跑在定时器线程上的（第 3 章 3.2）。在里面 `Wait` 会阻塞整个定时器线程，把别人一起卡死。
> `Wait()` 应该放在**你自己的线程**里，或者用「异步订阅 / 回调订阅」的方式。

## 5.3 订阅的四种姿势

LibXR 的 Topic 支持四种消费方式。新手先会第一种就行：

| 方式 | 一句话 | 什么时候用 |
| --- | --- | --- |
| **同步** `SyncSubscriber` | `Wait()` 等下一帧，数据写进你的变量 | 你自己的线程里，要最新的数据（**推荐先学这个**） |
| **异步** `ASyncSubscriber` | 先 `StartWaiting()`，之后 `Available()` 看有没有、`GetData()` 取 | 不连续处理，来一帧算一帧 |
| **队列** `QueuedSubscriber` | 收到就塞进 `SPSCQueue`，你自己 `Pop` | 怕漏数据，要排队 |
| **回调** `Topic::Callback` | 每来一帧立刻执行你的函数 | 极轻量的处理，**必须非阻塞** |

> ⚠️ **Topic 不再缓存「最近一帧」。**
> 官方明确写了：Topic 只负责**本次发布的分发**，不保存 latest payload。
> 所以「我没订阅到，之后还能取到刚才那帧」是**错误的心智模型**。要么用队列，要么在发布时同步处理。

## 5.4 发布者/订阅者的名字必须一模一样

```cpp
// 发布者（BMI088）
LibXR::Topic::CreateTopic<Eigen::Matrix<float, 3, 1>>("bmi088_gyro");

// 订阅者（Balance）
LibXR::Topic::SyncSubscriber<Eigen::Matrix<float, 3, 1>>("bmi088_gyro", gyro_);
//                                                        ^^^^^^^^^^^^ 必须逐字符相同
```

**类型也必须一致**（`payload_type_id + size + alignment` 共同定义类型契约）。
类型不一致 → 找不到 / 报错。

在 BMI088 里，这个名字是**参数**，来自 `User/xrobot.yaml`：

```yaml
modules:
- id: bmi088
  name: BMI088
  constructor_args:
    gyro_topic_name: bmi088_gyro     # ← 就是这里
    accl_topic_name: bmi088_accl
```

> 💡 **为什么把 topic 名字做成参数，而不是写死？**
> 如果一块板子上有两个 BMI088（比如平衡车 + 云台），可以起名叫 `bmi088_gyro_1` / `bmi088_gyro_2`，
> **不用改一行代码，只改 yaml**。这就是「配置驱动」的威力。

## 5.5 BMI088 的完整数据流：一条真实链路

这是整个工程里最典型、最值得反复看的一段。它把**中断、信号量、线程、Topic** 全串起来了：

```
[硬件] BMI088 陀螺仪数据就绪
   │  拉低 INT1_GYRO 引脚
   ▼
[ISR] GPIO 中断 → 执行 gyro_int_cb（BMI088.hpp:230）
   │  · 记时间戳 dt_gyro_
   │  · 只发一个信号：new_data_.PostFromCallback(in_isr)
   │  · 绝不读传感器、绝不解析、绝不打印
   ▼
[Semaphore] new_data_ 被唤醒
   ▼
[Thread] bmi088_thread → ThreadFunc（BMI088.hpp:375）
   │  new_data_.Wait(50) 返回 OK
   │  · RecvGyro()       SPI 读原始字节
   │  · ParseGyroData()  换算成 rad/s，做坐标旋转
   │  · topic_gyro_.Publish(data, sample_timestamp)   ← 发到 Topic
   ▼
[Topic] "bmi088_gyro"
   ▼
[其他模块] Balance / 上位机 / 日志 …… 谁订阅谁拿
```

对应到 C++ 知识点：

- **函数指针 / lambda**（2.11）：`gyro_int_cb` 是个 lambda，注册给 GPIO。
- **信号量**：`LibXR::Semaphore`，`PostFromCallback(in_isr)` 在中断里发信号，`Wait(50)` 在线程里等。
- **线程**：`thread_.Create(this, ThreadFunc, "bmi088_thread", task_stack_depth, REALTIME)`（`BMI088.hpp:253`）。
- **Topic**：跨模块解耦的出口。

关键代码，`BMI088.hpp:230`：

```cpp
auto gyro_int_cb = LibXR::GPIO::Callback::Create(
    [](bool in_isr, BMI088* bmi088) {
      auto timestamp = LibXR::Timebase::GetMicroseconds();
      bmi088->dt_gyro_ = timestamp - bmi088->last_gyro_int_time_;
      bmi088->last_gyro_int_time_ = timestamp;
      bmi088->sample_timestamp_ = timestamp;

      bmi088->new_data_.PostFromCallback(in_isr);   // ← 全部工作就这一句
    },
    this);
```

> 💡 **`in_isr` 为什么要作为参数传来传去？**
> 因为「我现在到底在中断上下文还是线程上下文」会决定哪些 API 能用（比如 `sem.Post()` 不能用在中断里，得用 `PostFromCallback()`）。
> 官方设计思想第 4 条：**「上下文（thread/isr）必须在回调中显式传递」**——不靠框架偷偷判断，而是写进接口里，让调用者不可能搞错。
> 详见第 6 章第 4 条。

---

# 第 6 章 六条设计思想（官方《设计思想》的白话版）

> 官方原文：<https://xrobot.work/docs/concept>
> 那一页的信息密度很高。下面把每一条翻译成「人话 + 你工程里的证据 + 对你的约束」。

## 6.1 思想一：ISR 只管交接，线程负责展开

**官方在说什么**
高频路径（串口、USB、DMA）尽量不依赖 mutex、阻塞式队列和长时间关中断。用预分配缓冲、环形队列、双缓冲，让「硬件事件推动数据流」：中断负责交接与状态推进，线程负责展开后续处理。

**人话**
中断是系统里最「急」的上下文，它会打断一切。所以中断里只做**交接**：把数据/状态从硬件手里接过来，打个信号，然后立刻退出。真正的处理放到线程里慢慢做。

**你工程里的证据**

| 位置 | 做了什么 |
| --- | --- |
| `BMI088.hpp:237` | 中断里只调 `PostFromCallback(in_isr)`（发信号） |
| `BMI088.hpp:375` | `ThreadFunc` 里才 `RecvGyro / ParseGyroData / Publish` |
| `uart7` 的收发 | LibXR 的 UART 驱动用 DMA + 环形缓冲，中断只搬运，不解析协议 |

**对你的约束**

- 中断回调里**不要**：等信号量、`Sleep`、加锁、`new`、打印一大段、做复杂运算。
- 中断回调里**可以**：读写几个变量、搬一小段数据、`PostFromCallback` 发信号。
- 判断标准是官方那句话：**「无阻塞」不等于「什么都不做」，而是「不做不可控的事」。**

## 6.2 思想二：运行时不做内存分配

**官方在说什么**
高频运行期路径不应该依赖临时资源分配来维持正确性。所有需要堆分配的对象，都应该**只在初始化时构造一次，永不析构**。

**人话**
`malloc` / `new` 不是不能用，而是**别在热路径、中断、高频回调里用**。启动时一次性把内存要够，之后运行时不再申请，系统行为就可预测：不会有碎片，不会有「内存不够导致偶发崩溃」。

**你工程里的证据**

| 位置 | 做了什么 |
| --- | --- |
| `xrobot_main.hpp:12` | `static BMI088 bmi088(...)` 只构造一次，永不析构 |
| `BlinkLED.hpp:28` | `Timer::CreateTask` 在构造时建任务，之后只复用 |
| `HardwareContainer` / `ApplicationManager` | 注册用的 `new` 只在启动阶段发生一次 |
| `BMI088.hpp:636` | `uint8_t rw_buffer_[20]` 是**成员数组**，不是运行时 malloc |

**对你的约束**

- 能在构造时分配/准备好的东西，就别拖到运行时。
- 需要缓冲区就设成成员变量或静态数组，别在回调里 `new`。
- 万一真的必须动态分配，官方给了策略：**按阶段分配、按生命周期回收**，并且要能说出「内存上界是多少」。

> 💡 这也解释了一个新手常见的困惑：「为什么模块构造函数里要干那么多事？」
> 因为那正是「初始化阶段」——**唯一被允许做资源准备的时机**。

## 6.3 思想三：一切回调/中断都必须是无阻塞的

**官方在说什么**
回调和中断里不应该做让时延、调度或资源边界失控的事。反对的是阻塞等待、复杂业务展开、额外资源申请，以及任何依赖调度器和唤醒顺序才能完成的处理。

**人话**
回调里不能「等」。不能等信号量、不能 `Sleep`、不能等锁、不能等另一个线程。

**你工程里的证据**

`BlinkLED.hpp:41` 的错误回调：

```cpp
if (!in_isr) {          // ← 先判断上下文！
  while (true) {        // ← 只有在线程上下文里，才敢死循环闪灯
    led->led_->Write(false);
    LibXR::Thread::Sleep(125);
    ...
  }
}
```

注意：这个回调**先判断 `in_isr`**：
- 在中断里 → 不做阻塞的事，直接返回。
- 在线程里 → 才敢 `Sleep` + `while(true)`。

**对你的约束**

- 回调函数里出现 `Sleep` / `Wait(非0)` / `while(1)` 之前，先问自己：**我保证自己在哪个上下文？**
- 定时器回调里不要 `Wait`（会卡住整个定时器线程，见 3.2）。
- 想做耗时的事 → 交给线程，用信号量/队列把活传过去。

## 6.4 思想四：上下文（thread/isr）必须显式传递

**官方在说什么**
回调发生在线程里还是 ISR 里，会直接影响哪些 API 能调、哪些行为安全。LibXR 的选择是让上下文成为接口语义的一部分，所以你会看到 `in_isr` 或成对的「普通接口 / callback-safe 接口」。

**人话**
框架**不帮你猜**「现在是不是在中断里」，而是让你**自己写出来**。看起来啰嗦，但误用成本低。

**你工程里的证据**

| 接口 | 差别 |
| --- | --- |
| `Semaphore::Post()` | 只能在普通上下文用 |
| `Semaphore::PostFromCallback(bool in_isr)` | 中断/回调里用，把上下文一起传进去 |
| `Topic::Publish(data)` | 普通上下文 |
| `Topic::PublishFromCallback(data, in_isr)` | 中断/回调里用 |
| `Timer::CreateTask` 回调签名 | 没有 `in_isr`（因为定时器回调只在线程里） |
| `GPIO::Callback::Create` 回调签名 | 第一个参数就是 `bool in_isr` |

**对你的约束**

- 写回调时，**先看它的函数签名里有没有 `in_isr`**。有 → 必须判断；没有 → 说明这个回调只会在一个固定上下文里跑。
- 不要「反正编译过了，先乱调」。这类误用往往表现为「偶发死机」，极难查。

## 6.5 思想五：任何 I/O 必须绑定确定的完成行为

**官方在说什么**
I/O 操作要在**发起时**就确定完成行为：完成后由谁接收、如何处理、什么时候算结束。这就是 `Operation` 模型存在的原因（回调完成 / 阻塞等待 / 轮询 / 忽略结果）。

**人话**
点一份外卖的时候，就要说清楚「送到哪、谁收」。
发起一次 SPI/UART 读写时，就要明确「**这活干完了，谁来接通知**」。

**你工程里的证据**

`BMI088.hpp:158`：

```cpp
void WriteSingle(Device device, uint8_t reg, uint8_t data) {
  Select(device);
  spi_->MemWrite(reg, data, op_spi_);   // ← 第三个参数 op_spi_ 就是"完成行为"
  Deselect(device);
  LibXR::Thread::Sleep(1);
}
```

`op_spi_` 是怎么来的，`BMI088.hpp:219`：

```cpp
op_spi_(sem_spi_),    // ← 用 sem_spi_ 这个信号量来接收"完成了"的通知
```

成员里，`BMI088.hpp:646` 一带：

```cpp
LibXR::Semaphore sem_spi_, new_data_;
LibXR::SPI::OperationRW op_spi_;
```

于是 `spi_->MemWrite(...)` 的语义是：**「发起写寄存器；写完之后给 `sem_spi_` 发信号」**。
调用者想等结果就 `sem_spi_.Wait(...)`，不想等就直接往下走。

**对你的约束**

- 看到 `OperationRW` / `op_` 这样的成员，就明白「这个驱动的 I/O 完成通知走这个信号量」。
- 你自己写驱动时，同样要把「等还是不等」明确表达出来，别让上层猜。

## 6.6 思想六：接口中不应出现任何平台相关类型

**官方在说什么**
公共接口应该表达能力和语义（「这是一条 UART」「这里需要一个完成行为」），而不是暴露平台底层句柄（`HAL_xxx`、`TaskHandle_t`、`termios`）。

**人话**
业务代码里**不应该出现 HAL、FreeRTOS、具体 MCU 的名字**。只出现「GPIO / SPI / UART / Timer / Topic」这种能力名。

**你工程里的证据**

| 角色 | 文件 | 内容 |
| --- | --- | --- |
| 抽象接口 | `LibXR/src/driver/gpio.hpp` | `class GPIO { virtual void Write(bool) = 0; ... }` |
| 平台实现 | `LibXR/driver/st/stm32_gpio.hpp` | `class STM32GPIO : public GPIO`，内部调 HAL |
| 业务模块 | `Modules/BMI088/BMI088.hpp` | 只用 `LibXR::GPIO*` / `LibXR::SPI*`，**从不出现 `HAL_`** |
| 平台细节集中地 | `User/app_main.cpp` | 唯一出现 `&hspi2`、`CS1_ACCEL_Pin`、`huart7` 的地方 |

验证方法：**在 `Modules/` 下搜 `HAL_`，应该搜不到。**
（在 `Modules/` 目录下搜 `HAL_` 是 **0 条命中**。唯一出现裸 HAL 调用的是 `User/app_main.cpp` 里临时点亮 WS2812 的代码——那正是「临时绕过框架」的坏例子，注释里也写了用完要删。）

**对你的约束**

- 写模块时，参数类型只准用 `LibXR::` 开头的抽象类型。
- 一旦你想在模块里 `#include "stm32_spi.hpp"`，先停下来——**那是设计出问题了的信号**。

## 6.7 六条思想总结成一张表

| # | 设计思想 | 一句话记住 | 你工程里的关键词 |
| --- | --- | --- | --- |
| 1 | ISR 只管交接 | 中断只发信号，不干活 | `PostFromCallback` |
| 2 | 运行时不做分配 | 启动时一次要够 | `static`、`rw_buffer_[20]` |
| 3 | 回调无阻塞 | 回调里不等等等 | `if (!in_isr)` |
| 4 | 上下文显式传递 | 中断里能干啥，写清楚 | `in_isr`、`PostFromCallback` |
| 5 | I/O 绑定完成行为 | 发起时就说好谁收尾 | `OperationRW`、`op_spi_(sem_spi_)` |
| 6 | 接口不含平台类型 | 模块里不出现 HAL | `LibXR::GPIO`、`STM32GPIO` |

---
# 第 7 章 动手练习（由易到难）

> **原则：每一步都要能观察到结果。**看不见结果的学习等于没学。
> 所有命令都在**工程根目录**执行；若提示找不到命令，把 `C:\Users\liaoz\.local\bin` 加进 PATH 或重开终端。

## 练习 1（10 分钟）：感受「配置 → 代码」

**目标**：建立「`xrobot.yaml` 是输入，`xrobot_main.hpp` 是输出」的直觉。

1. 打开 `User/xrobot.yaml`，找到 `global_settings.monitor_sleep_ms: 1000`，改成 `500`。
2. 跑：
   ```powershell
   xrobot_gen_main --output User\xrobot_main.hpp
   ```
3. 打开 `User/xrobot_main.hpp`，找到 `Thread::Sleep(...)`，确认变成了 `500`。
4. 把 `500` 改成 `blink_cycle` 类似的**模块参数**试试：把 `target_temperature: 45` 改成 `50`，重新生成，看 `xrobot_main.hpp` 里 `static BMI088 bmi088(...)` 的第 10 个参数。
5. 改回 `45` / `1000`，重新生成。

**你要体会的**：你**没有改任何 C++ 代码**，但程序行为变了。这就是「配置驱动」。

## 练习 2（15 分钟）：读生成物，回答三个问题

不要编译、不要运行，只用眼睛看 `User/xrobot_main.hpp` 和 `User/xrobot.yaml`：

1. `bmi088` 这个实例的目标温度是多少？这个数字在 `yaml` 的哪一行？
2. `bmi088` 的陀螺仪 Topic 叫什么名字？它在 `BMI088.hpp` 里被用在了哪一行？
3. 如果把 `yaml` 里 `modules:` 下 `bmi088` 整段删掉再重新生成，`xrobot_main.hpp` 会变成什么样？

> 参考答案在 yaml/hpp 里都能找到，别急着问人——**先练习「文件之间互相对照」这个动作**。

## 练习 3（20 分钟）：创建一个自己的空模块，并让它出现在生成代码里

```powershell
# 1. 生成模块骨架
xrobot_create_mod MyModule --desc "我的第一个模块" --hw led/LED

# 2. 看它生成了什么
xrobot_mod_parser --path Modules\MyModule\

# 3. 把它加入实例列表（改 User/xrobot.yaml，或直接跑下面这条）
xrobot_add_mod MyModule

# 4. 重新生成主函数
xrobot_gen_main --output User\xrobot_main.hpp

# 5. 打开 xrobot_main.hpp，找 MyModule —— 它现在应该被实例化了
```

**注意**：`xrobot_create_mod` 生成的骨架里，硬件查找那一行是**注释掉的**：

```cpp
// Hardware initialization example:
// auto dev = hw.template Find<LibXR::GPIO>("led");
```

所以它**可以直接编译、直接跑**——只不过什么也不做（业务代码是空的）。
manifest 里的 `required_hardware: led/LED` 只是**元信息**，`xrobot_gen_main` 不会去校验它。

**那什么时候才会「一上电就死」？** 当**你自己的代码真的调用了 `hw.FindOrExit<...>({"led"})`**，
而 `app_main.cpp` 里又没有注册这个别名时。骨架里的 `Find` 是注释掉的，所以练习 3 不会出问题。

## 练习 4（30 分钟）：写一个「每秒打印一次」的模块

**目标**：写出第一个真正在运行的模块，并观察串口输出。

```cpp
#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 每秒打印一次心跳
constructor_args:
  - period_ms: 1000
template_args: []
required_hardware:
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "stdio.hpp"
#include "timer.hpp"

class Heartbeat : public LibXR::Application {
 public:
  Heartbeat(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
            uint32_t period_ms)
      : timer_(LibXR::Timer::CreateTask(Tick, this, period_ms)) {
    UNUSED(hw);
    app.Register(*this);          // 登记到调度器（这样 OnMonitor 会被调用）
    LibXR::Timer::Add(timer_);
    LibXR::Timer::Start(timer_);
  }

  // 定时器回调：必须是无阻塞的（第 6 章思想三）
  static void Tick(Heartbeat* self) {
    self->count_++;
    LibXR::STDIO::Printf<"heartbeat #%u\r\n">(static_cast<unsigned>(self->count_));
  }

  void OnMonitor() override {}

 private:
  uint32_t count_ = 0;
  LibXR::Timer::TimerHandle timer_;
};
```

步骤：

1. 生成一个干净的新模块（这次**不带** `--hw`）：
   ```powershell
   xrobot_create_mod Heartbeat --desc "每秒打印一次心跳"
   xrobot_add_mod Heartbeat
   ```
2. 用上面的代码**整个覆盖** `Modules/Heartbeat/Heartbeat.hpp`。
3. `xrobot_gen_main --output User\xrobot_main.hpp`
4. 编译：`cmake --build --preset Debug --parallel 8`
5. 烧录后，用串口工具连 **uart7**（波特率按 CubeMX 配置），应该能看到每秒一条 `heartbeat #n`。

**你会踩到的坑（提前告诉你）：**
- **目录名 = 文件名 = 类名 = `xrobot.yaml` 里的 `name`**，四处必须一致，都叫 `Heartbeat`。
  这是这套代码生成工具最基本的约定（`xrobot_create_mod` 生成时本来就是对上的）。
- 构造函数参数个数必须和 manifest 里的 `constructor_args` 对上。
- `CMake` 靠**目录扫描**找模块（`LibXR/CMakeLists.txt:238`），并且要求每个模块目录里有 `CMakeLists.txt`。
  所以**不要手动新建目录来放 .hpp**——用 `xrobot_create_mod` 生成，它会把 `CMakeLists.txt` 一起给你。
- `STDIO::Printf<"...">` 的格式串写在 `<>` 里，不是写在括号里。这是 LibXR 的特色（编译期解析格式串、更省栈）。

## 练习 5（进阶，45 分钟）：订阅 `bmi088_gyro` 并打印

**目标**：把第 5 章的知识用起来，跨模块取数据。

```cpp
#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 订阅陀螺仪 Topic 并打印
constructor_args:
  - gyro_topic_name: "bmi088_gyro"
  - stack_depth: 2048
template_args: []
required_hardware:
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "message.hpp"
#include "thread.hpp"

class GyroPrinter : public LibXR::Application {
 public:
  GyroPrinter(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
              const char* gyro_topic_name, size_t stack_depth)
      : gyro_(Eigen::Matrix<float, 3, 1>(0.0f, 0.0f, 0.0f)),   // ① 先初始化数据
        gyro_sub_(gyro_topic_name, gyro_) {                    // ② 再初始化订阅者
    UNUSED(hw);
    app.Register(*this);
    thread_.Create(this, ThreadFunc, "gyro_printer", stack_depth,
                   LibXR::Thread::Priority::LOW);
  }

  static void ThreadFunc(GyroPrinter* self) {
    while (true) {
      // 阻塞等待最多 100ms —— 注意：这是在"自己的线程"里，不是在 OnMonitor 里
      if (self->gyro_sub_.Wait(100) == LibXR::ErrorCode::OK) {
        LibXR::STDIO::Printf<"gyro: %f %f %f\r\n">(
            self->gyro_.x(), self->gyro_.y(), self->gyro_.z());
      }
    }
  }

  void OnMonitor() override {}

 private:
  Eigen::Matrix<float, 3, 1> gyro_;   // ★ 必须声明在 gyro_sub_ 前面！
  LibXR::Topic::SyncSubscriber<Eigen::Matrix<float, 3, 1>> gyro_sub_;
  LibXR::Thread thread_;
};
```

**两个必须理解的点：**

> ⚠️ **点 1：成员变量的初始化顺序 = 声明顺序，不是初始化列表的顺序。**
> 上面 `gyro_` 必须写在 `gyro_sub_` 前面。
> 因为 `gyro_sub_(gyro_topic_name, gyro_)` 要用 `gyro_` 的引用；如果 `gyro_sub_` 先声明，
> 它在构造时 `gyro_` 还没初始化，拿到的是一个「还没构造的对象」的引用——**未定义行为**。
> 编译器一般会给你 warning（`-Wreorder`），但不会报错。这是 C++ 经典坑之一。
> （BMI088 里也是同样的顺序：`gyro_data_` 在 637 行，`topic_gyro_` 在 638 行。）

> ⚠️ **点 2：`Wait()` 放在自己的线程里，不能放在 `OnMonitor()` 里。**
> `OnMonitor()` 跑在 LibXR 的定时器线程上（第 3 章 3.2）。在那里 `Wait` 会把整个定时器线程卡住。
> `Wait` 放在 `thread_.Create(...)` 起的**自己的线程**里才安全。

**验证**：串口应该同时收到 `heartbeat #n` 和 `gyro: ... ... ...` 两种输出。
`gyro` 的数值是弧度/秒，静止时应该接近 0，转动板子时某个轴会明显变化。

**如果没数据：**
1. 名字对不上？确认订阅的名字和 `yaml` 里 `gyro_topic_name` 逐字符相同。
2. 类型对不上？BMI088 发布的是 `Eigen::Matrix<float,3,1>`，你也必须用这个类型。
3. BMI088 自己没初始化成功？先看串口有没有 `BMI088: Init succeeded.`（`XR_LOG_PASS`）。

## 练习 6（可选）：给自己的模块加一条终端命令

参考 `BMI088.hpp:220`：

```cpp
cmd_file_(LibXR::RamFS::CreateFile("bmi088", CommandFunc, this))
```

和 `BMI088.hpp:226`：

```cpp
hw.template FindOrExit<LibXR::RamFS>({"ramfs"})->Add(cmd_file_);
```

命令函数签名固定，`BMI088.hpp` 里的 `CommandFunc` 形如：

```cpp
static int CommandFunc(BMI088* self, int argc, char** argv);
```

之后在串口终端敲 `bmi088 show 1000 10` 就能触发。

> ⚠️ 命令处理也跑在定时器线程上（`app_main.cpp:134` 的注释）。别在里面写长时间阻塞的代码。

---

# 第 8 章 常见困惑 FAQ

## Q1：为什么 `xrobot_main.hpp` 里的模块是 `static`？

```cpp
static BMI088 bmi088(hw, appmgr, ...);
```

`static` 局部变量**只构造一次**，且活到程序结束。
配合「构造即初始化、永不析构」的设计（第 6 章思想二），效果是：**模块对象在整个运行期只有一份，不会重复构造、不会析构、不会重新分配内存。**

## Q2：为什么 `FindOrExit` 前面有 `.template`？

```cpp
hw.template FindOrExit<LibXR::GPIO>({...})
```

`hw` 的类型依赖模板参数，编译器不敢断定 `<` 是「模板参数开始」还是「小于号」。
`.template` 是纯粹的语法补丁，告诉编译器「后面的 `<` 是模板」。
**理解到这一层就够了，你自己写时照抄。**

## Q3：为什么初始化都写在构造函数里，不单独写个 `Init()`？

因为那样会出现「对象已经存在，但还没初始化」的中间状态——这是 bug 的高发区。
「构造即初始化」保证：**只要你能拿到这个对象，它就是可用的。**
而且启动阶段是唯一被允许做资源分配的时机（第 6 章思想二）。

> 注意区分：`BMI088::Init()` 是**芯片的初始化**（写寄存器），它在构造函数里被调用；
> 而「模块对象的初始化」就是构造函数本身。两者不矛盾。

## Q4：`override` 不写行不行？

行，但强烈建议写。写了之后，万一你把 `OnMonitor()` 的签名写错（比如少个 `void`、多个 `const`），
编译器会报错；不写的话，你只是「新加了一个没人调用的函数」，而且**不会报错**——非常难查。

## Q5：`Timer::Add()` 和 `Timer::Start()` 有什么区别？

- `Add`：把任务**挂进**定时器的任务链表。第一次调用 `Add` 会创建定时器线程（`timer.cpp:27`）。
- `Start`：把任务的 `enable_` 标志打开，开始真正周期执行。

只 `Add` 不 `Start` → 任务挂着但永远不跑。只 `Start` 不 `Add` → 根本没挂进链表。

## Q6：为什么我改了 `xrobot_main.hpp`，下次生成就没了？

因为它是**生成物**。生成器每次都会整个覆盖。
要加自己的启动逻辑，请改：
- `app_main.cpp` 的 `/* User Code Begin 3 */` 区（CubeMX 保证不覆盖这段），或
- 模块的构造函数里。

同理，`app_main.cpp` 里只有 `User Code Begin/End` 之间的内容是你手写的。

## Q7：编译通过了，就代表代码是对的吗？

不是。这个框架里有两类「编译期查不出来」的错：

| 错误 | 编译 | 运行 | 现象 |
| --- | --- | --- | --- |
| `FindOrExit` 找不到硬件 | ✅ 通过 | ❌ 致命错误 | 一上电就死，串口无输出 |
| Topic 名字写错 | ✅ 通过 | 大概率静默 | 收不到数据，但不报错 |
| Topic 类型不匹配 | ✅ 通常通过 | ❌ 报错 | 可能有断言 |
| 构造函数参数个数不对 | ❌ 报错 | — | 编译期就能发现 |

所以「能编译」只是最低标准。**上线前一定要在硬件上验证行为。**

## Q8：为什么模块里不能 `#include "stm32_gpio.hpp"`？

因为那就把业务代码和 STM32 绑死了（第 6 章思想六）。
一旦你这么做，这个模块就再也不能在其他 MCU 上复用，也失去了「换平台不改业务代码」的全部好处。
**看到自己想 include 平台头，就说明设计出问题了**——正确做法是向 `hw` 要一个抽象接口。

## Q9：我的模块一上电就死，怎么查？

按这个顺序：

1. 看串口有没有 `BMI088: Init succeeded.` 之类的 `XR_LOG_PASS`。完全没有输出 → 可能卡在更早的地方。
2. 把模块 manifest 的 `required_hardware` 和 `app_main.cpp` 的 `Entry` 别名列表**对着看**。
3. 用 `xrobot_mod_parser --path Modules\你的模块\` 打印模块要求。
4. 检查 `User/xrobot.yaml` 里的参数是否和 manifest 的 `constructor_args` 对得上（名字、个数、枚举写法）。
5. `FindOrExit` 找不到会走 `libxr_fatal_error(__FILE__, __LINE__, in_isr)`。
   如果你在 `Assert::RegisterFatalErrorCallback` 注册了回调（BlinkLED 那样），可以借它的动作定位。

## Q10：我 C++ 还没学完，应该先学完再来看框架吗？

**不要。** 「学完 C++ 再看框架」是一个永远开始不了的陷阱。
正确顺序是：

1. 先能看懂 `BlinkLED.hpp`（本文第 2、4 章的知识点足够）。
2. 改参数、写一个打印模块（第 7 章练习 1~4）。
3. 遇到不会的语法，**带着具体问题去补**（这一节的语法，比泛读一本 C++ 书有效得多）。
4. 再回头读 `BMI088.hpp`，你会发现自己突然能看懂了。

---

# 附录 A 命令速查

```powershell
# ---------- 环境 ----------
# 若提示找不到命令，把下面这个目录加进 PATH
$env:Path += ";C:\Users\liaoz\.local\bin"

# ---------- 代码生成 ----------
# 改完 User/xrobot.yaml 后，重新生成模块实例化 + 主循环
xrobot_gen_main --output User\xrobot_main.hpp

# 新建模块骨架
xrobot_create_mod MyModule --desc "我的模块" --hw spi1 --constructor period_ms=500

# 查看模块要求（参数 / 硬件）
xrobot_mod_parser --path Modules\BMI088\

# 把模块加入实例列表（改 User/xrobot.yaml）
xrobot_add_mod BMI088

# 把模块仓库加入 Modules/modules.yaml
xrobot_add_mod xrobot-org/BMI088

# 同步模块仓库（modules.yaml 改过之后）
xrobot_init_mod --config Modules\modules.yaml --directory Modules --sources Modules\sources.yaml

# 首次一键初始化（建 Modules、拉模块、生成主函数）
xrobot_setup

# 改完 NewDM.ioc（CubeMX）后，重新生成 .config.yaml / app_main.cpp 等
xr_cubemx_cfg --xrobot -d .

# ---------- 编译 ----------
cmake --build --preset Debug --parallel 8
cmake --build --preset Release --parallel 8

# ---------- 查代码（比 grep 快） ----------
rg -n "FindOrExit" Modules\
rg -n "CreateTopic|Publish" Modules\
rg -n "HAL_" Modules\            # ← 应该搜不到（第 6 章思想六）
```

# 附录 B 术语表（中英对照）

| 中文 | 英文 | 一句话解释 | 本文位置 |
| --- | --- | --- | --- |
| 命名空间 | namespace | 给名字加前缀，防重名 | 2.2 |
| 类 / 结构体 | class / struct | 把数据和方法打包 | 2.3 |
| 成员变量 / 成员函数 | member variable / function | 类里面的数据和函数 | 2.3 |
| 构造函数 | constructor | 对象出生时自动执行的函数 | 2.4 |
| 初始化列表 | initializer list | 构造函数冒号后面那串 | 2.5 |
| 继承 | inheritance | `class A : public B` | 2.6 |
| 虚函数 / 多态 | virtual function / polymorphism | 子类可改写的函数 | 2.6 |
| 纯虚函数 / 抽象类 | pure virtual / abstract class | `= 0`，只能被继承 | 2.6 |
| 重写 | override | 子类实现父类的虚函数 | 2.6 |
| 指针 / 引用 | pointer / reference | 地址 / 别名 | 2.7 |
| 强类型枚举 | enum class | 给整数起名字 | 2.8 |
| 静态 | static | 四种意思，看位置 | 2.9 |
| 模板 | template | 给类型/尺寸留空 | 2.10 |
| 匿名函数 | lambda | `[](参数){...}` | 2.11 |
| 回调 | callback | 事情发生后执行的代码 | 2.11 |
| 依赖注入 | dependency injection | 不自己造，向容器要 | 1.2 |
| 硬件容器 | HardwareContainer | 别名注册表 | 3.4 |
| 别名 | alias | 硬件的「外号」 | 3.4 |
| 模块清单 | MANIFEST | 注释里的模块说明书 | 4.2 |
| 契约 | contract | 写模块必须满足的规矩 | 4.4 |
| 发布/订阅 | publish / subscribe | 按名字传数据 | 5.1 |
| 主题 | topic | 一条具名的数据通道 | 5.2 |
| 信号量 | semaphore | 线程间/中断与线程间的信号 | 5.5 |
| 中断 | ISR (interrupt service routine) | 打断一切的最高优先级代码 | 6.1 |
| 临界区 | critical section | 不能被中断/切换打断的一段 | 6.1 |
| 原子操作 | atomic | 不可被打断的操作 | 6.1 |
| 无锁 | lock-free | 不用锁也能保证安全 | 6.1 |

# 附录 C 文件地图

| 我想…… | 打开这个文件 |
| --- | --- |
| 改引脚 / 外设 / 时钟 | `NewDM.ioc`（用 CubeMX GUI 打开） |
| 看引脚被解析成了什么 | `.config.yaml`（只看不改） |
| 看/改启动流程 | `User/app_main.cpp`（只改 `User Code` 区） |
| 改运行参数（缓冲区、栈、RTOS） | `User/libxr_config.yaml` |
| 决定要跑哪些模块、模块参数 | `User/xrobot.yaml` ★最常改 |
| 看「配置生成了什么代码」 | `User/xrobot_main.hpp`（生成物，别手改） |
| 要哪些第三方模块仓库 | `Modules/modules.yaml` |
| 模块索引源 | `Modules/sources.yaml` |
| 写自己的模块 | `Modules/<你的模块>/<你的模块>.hpp` |
| 看最简单的模块示例 | `Modules/BlinkLED/BlinkLED.hpp` |
| 看完整实战模块（SPI/中断/线程/Topic/命令） | `Modules/BMI088/BMI088.hpp` |
| 看模块空壳模板 | `Modules/TestModule/TestModule.hpp` |
| 查 LibXR 抽象接口 | `Middlewares/Third_Party/LibXR/src/driver/*.hpp` |
| 查 LibXR 中间件（Topic/Timer/Application） | `Middlewares/Third_Party/LibXR/src/middleware/`、`src/system/` |

# 附录 D 建议的学习路线

**第 1 天**
1. 读本文第 0、1、2 章（可以只看每节加粗的部分）。
2. 做练习 1（改 yaml → 生成）。
3. 目标：**不再害怕 `xrobot.yaml` 和 `xrobot_main.hpp`**。

**第 2 天**
4. 读本文第 3 章，对照打开 `User/app_main.cpp`。
5. 做练习 2。
6. 目标：**能说清「上电之后按什么顺序跑了什么」**。

**第 3 天**
7. 读本文第 4 章 + 打开 `Modules/BlinkLED/BlinkLED.hpp` 对照。
8. 做练习 3、4。
9. 目标：**写出第一个自己会跑的模块**。

**第 4~7 天**
10. 读本文第 5、6 章。
11. 做练习 5。
12. 目标：**会用 Topic 跨模块取数据，理解六条设计思想**。

**之后**
13. 回头读 `Modules/BMI088/BMI088.hpp`。你会发现以前天书一样的东西现在基本能读懂。
14. 想深入 C++，按「遇到什么补什么」的原则，重点补：`RAII`、`std::atomic`、`内存模型`、`模板特化`。
15. 想做更多模块，看官方文档的 `basic_coding/` 和 `proj_man/` 两章。

---

## 配套文档

| 文档 | 什么时候看 |
| --- | --- |
| `docs/xrobot-getting-started.md` | 想知道「改哪个文件、跑哪条命令」时 |
| `docs/libxr-api-cheatsheet.md` | 写代码时想抄一行 API 时 |
| 本文 | 想理解「为什么这么写」「这个 C++ 是什么意思」时 |
| <https://xrobot.work/docs/concept> | 想读官方原版设计思想时（建议学完本文再读） |
| <https://xrobot.work/docs/basic_coding> | 想查具体某个驱动/中间件怎么用时 |

> 最后一句：**看不懂很正常，看得懂才是意外。** 这个框架是别人多年抽象的产物，
> 你不需要「一次全懂」，只需要「每次多懂一行」。加油。