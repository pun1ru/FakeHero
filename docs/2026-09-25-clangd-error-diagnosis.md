# VS Code 大量报错诊断日志

- 日期：2026-09-25
- 工程：NewDM
- 目标芯片：STM32H723
- 工程目录：`E:\Embedded\STM32project\NewDM`

## 现象

VS Code 编辑器中出现大量头文件找不到、符号无法解析等错误，例如：

- `main.h` 找不到
- `FreeRTOS.h` 找不到
- `cmsis_os2.h` 找不到
- `dma.h`、`fdcan.h`、`spi.h`、`tim.h`、`usart.h` 等头文件找不到

这些报错主要出现在编辑器的问题面板和代码红线中。

## 根因

工程同时启用了两个 clangd 扩展：

1. 普通 LLVM clangd：`llvm-vs-code-extensions.vscode-clangd`
2. STM32Cube 自带 clangd：`stmicroelectronics.stm32cube-ide-clangd`

普通 LLVM clangd 在 STM32 工程的 `build/Debug/compile_commands.json` 准备完成前启动，没有获得交叉编译器参数、宏定义和头文件搜索路径，因此使用默认配置解析源码，并产生大量误报。

STM32Cube clangd 日志也明确报告普通 LLVM clangd 与 STM32Cube clangd 存在冲突。

## 实际构建结果

使用真实的 CMake/Ninja 构建进行验证：

```powershell
cmake --build --preset Debug --parallel 8
```

构建成功并生成：

```text
build/Debug/NewDM.elf
```

构建时的内存占用：

```text
DTCMRAM: 47176 B / 128 KB，35.99%
FLASH:   67068 B / 1 MB，6.40%
```

因此，当时编辑器显示的大量错误不是实际编译错误。

## 已采取的修复

在工程级 `.vscode/settings.json` 中加入：

```json
"clangd.enable": false,
"stm32cube-ide-build-cmake.ignoreCubeProjectDiscovery": false
```

该配置只在 NewDM 工作区内关闭普通 LLVM clangd，不影响其他工程，并继续使用 STM32Cube clangd 和 STM32 生成的编译数据库进行代码索引。

修改后再次执行构建，结果为：

```text
ninja: no work to do.
```

构建退出码为 `0`。

## 生效操作

修改设置后，需要在 VS Code 中执行：

1. 按 `Ctrl+Shift+P`。
2. 执行 `Developer: Reload Window`（开发人员：重新加载窗口）。
3. 等待 STM32Cube CMake 扩展重新加载 `build/Debug/compile_commands.json`。

若 STM32Cube 工程没有自动显示，可执行：

- `STM32Cube: Set up STM32Cube projects`
- `STM32Cube: Resume STM32Cube project discovery`
- `STM32Cube: Restore STM32Cube CMake project configuration`

## 非错误警告

实际构建中仍可能看到以下警告，但它们不会导致本次构建失败：

- Eigen 使用旧版 CMake 策略 `CMP0146` 的弃用警告。
- LibXR 建议在不需要软件定时器时关闭 `configUSE_TIMERS`。

