# LibXR / XRobot 速查卡（只查，不读）

> 用途：写代码时查一行。**不要通读，不要顺着函数点进去。**
> 里面每个调用都来自你工程里已经跑通的代码（BlinkLED / BMI088 / app_main.cpp）。

## 三条止损规则

**规则 1 —— 不认识的函数，只记「输入什么、返回什么」，绝不点进去。**
你在 90% 的调用上永远不需要知道它的实现。点进去 = 掉进无底洞。

**规则 2 —— 万一必须看一个函数，只看 3 个地方：**
签名（参数和返回）、第一行、最后的 return。中间的循环、宏、模板全部跳过。

**规则 3 —— 一次只带一个问题读代码，30 分钟没答案就写下来问人。**
同时问两个问题，就是你现在「看不进去」的原因。

> 为什么会觉得无穷无尽？因为库代码为了通用，每一层都包了一层抽象。
> 你读的不是「一个程序」，是别人多年的抽象设计。这东西是**查**的，不是**懂**的。

## 一、拿硬件（模块构造函数里）

```cpp
// 按别名要一个硬件，返回指针；找不到就致命错误
LibXR::GPIO* gpio = hw.template FindOrExit<LibXR::GPIO>({"led", "LED"});

// 多个别名 = 命中任意一个就行。类型不对也找不到（GPIO 和 SPI 不通用）
```

出处：`Modules/BMI088/BMI088.hpp:211` 一带。

## 二、GPIO

```cpp
gpio->Write(true);      // 输出高/低
bool v = gpio->Read();  // 读当前电平
```

## 三、SPI（读写寄存器）

```cpp
LibXR::Semaphore sem;                    // 异步完成信号
LibXR::SPI::OperationRW op(sem);         // SPI 操作句柄，构造一次反复用

spi->MemWrite(0x7E, 0xB6, op);            // 写：寄存器, 数据, 操作句柄
spi->MemRead(0x00, {buf, 2}, op);         // 读：寄存器, {缓冲区, 长度}, 操作句柄
```

出处：`Modules/BMI088/BMI088.hpp:85`（WriteSingle / ReadSingle）。

## 四、串口 / 打印

```cpp
STDIO::read_  = uart7.read_port_;   // 全局读端口（app_main.cpp 里接终端）
STDIO::write_ = uart7.write_port_;

uart.read_port_->Read(buf, op);     // 收
uart.write_port_->Write(buf, op);   // 发

LibXR::STDIO::Printf<"x = %f\r\n">(x);   // 格式化打印，尖括号里是格式串
XR_LOG_INFO("值 = %d", n);               // 日志：INFO / PASS / WARN / ERROR
```

## 五、周期任务（最常用）

```cpp
static void MyTask(MyModule* self) { ... }   // 必须是普通函数指针

auto h = LibXR::Timer::CreateTask(MyTask, this, 250);  // 回调, 参数, 周期(ms)
LibXR::Timer::Add(h);      // 挂进链表（第一次会创建定时器线程）
LibXR::Timer::Start(h);    // 开始跑
LibXR::Timer::Stop(h);     // 停
LibXR::Timer::SetCycle(h, 500);  // 改周期
```

**注意**：所有定时回调都在**同一个线程**里跑（栈大小来自 `PlatformInit`）。
所以回调里不要写耗时/阻塞代码，会把别人一起卡住。

## 六、自己的线程

```cpp
LibXR::Thread thread_;
thread_.Create(this, ThreadFunc, "my_thread", 2048,
               LibXR::Thread::Priority::REALTIME);
// 参数, 函数, 名字, 栈深度, 优先级

LibXR::Thread::Sleep(100);              // 睡 100ms
LibXR::Thread::GetTime();               // 当前毫秒
// 优先级：IDLE < LOW < MEDIUM < HIGH < REALTIME
```

出处：`Modules/BMI088/BMI088.hpp` 构造函数末尾。

## 七、模块之间通信（Topic）

```cpp
LibXR::Topic topic = LibXR::Topic::CreateTopic<Eigen::Matrix<float,3,1>>("bmi088_gyro");
topic.Publish(data);                 // 发（自动带时间戳）
topic.Publish(data, timestamp);      // 发（指定时间戳）
```

订阅方按**同名 topic** 取数据。模块之间不互相 `#include`，靠名字解耦。

## 八、线程间同步

```cpp
LibXR::Semaphore sem;
sem.Wait();                  // 等（可带超时）
sem.Post();                  // 在普通代码里发信号
sem.PostFromCallback(in_isr); // 在中断/回调里发信号（注意参数）
```

## 九、掉电保存

```cpp
LibXR::Database::Key<Eigen::Matrix<float,3,1>> key(db, "bmi088_gyro_data", {0,0,0});
key.Set(value);       // 写并保存
key.Load();           // 重新载入
float v = key.data_.x();   // 读
```

## 十、加一条终端命令

```cpp
int CommandFunc(MyModule* self, int argc, char** argv);   // 固定签名

LibXR::RamFS::File file = LibXR::RamFS::CreateFile("mycmd", CommandFunc, this);
hw.FindOrExit<LibXR::RamFS>({"ramfs"})->Add(file);
// 之后在串口终端敲 mycmd 就能进这个函数
```

出处：`Modules/BMI088/BMI088.hpp:224`。

## 十一、被周期调度（可选）

```cpp
class MyModule : public LibXR::Application {
  void OnMonitor() override { ... }   // 框架每隔 monitor_sleep_ms 调一次
};
app.Register(*this);                  // 构造里注册，不注册就不会被调
```

## 十二、断言

```cpp
REQUIRE(ptr != nullptr);   // 条件不成立 → 致命错误，程序停在这里
ASSERT(x > 0);             // 同上（Debug/Release 行为可能不同）
```

## 查不到怎么办

不用猜、不用翻源码，直接问我，或者用：

```powershell
xrobot_mod_parser --path Modules\BMI088\    # 看模块要什么参数、什么硬件
```