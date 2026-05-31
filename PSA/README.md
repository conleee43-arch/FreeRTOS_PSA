# PSA 固件

PSA 是一个基于 STM32G431 的 STM32CubeMX + HAL 裸机固件工程，当前仓库以内建 Keil MDK-ARM 工程为主要构建与调试入口。

## 快速事实

- MCU: `STM32G431CBT6` / `STM32G431CBTx`
- 框架: STM32CubeMX 生成的 HAL 项目
- 主要签入的 IDE/构建流程: Keil MDK-ARM (`MDK-ARM/PSA.uvprojx`)
- 来自构建日志的当前工具链: ArmClang 6.24, MDK-ARM 5.43
- 主要自定义模块: `Core/Src/uart_safe.c` + `Core/Inc/uart_safe.h`

## 代码库映射

- `Core/` — 应用程序代码和 CubeMX 生成的外设初始化
- `Drivers/` — CMSIS 和 STM32 HAL 供应商代码
- `MDK-ARM/` — Keil 工程、调试配置、构建输出
- `PSA.ioc` — STM32CubeMX 硬件/外设源配置
- `.mxproject` — CubeMX 元数据
- `docs/` — 遵循活文档 (living-docs) 结构的共享项目文档

## 文档入口点

面向人类的概览从这里开始，然后继续阅读：

- [docs/ARCH_documentation-governance.md](docs/ARCH_documentation-governance.md) — 文档映射和更新规则
- [docs/ARCH_repo-overview.md](docs/ARCH_repo-overview.md) — 代码库架构概览
- [docs/GUIDE_build-flash-debug.md](docs/GUIDE_build-flash-debug.md) — 构建、烧录、调试工作流
- [docs/REF_hardware-software-map.md](docs/REF_hardware-software-map.md) — 引脚/DMA/中断映射
- [docs/LOGIC_uart-safe.md](docs/LOGIC_uart-safe.md) — UART DMA 命令/日志记录行为

代理专用引导文件：

- [CLAUDE.md](CLAUDE.md) — Claude Code 入口说明
- [AGENTS.md](AGENTS.md) — Codex 入口说明

这些仅为入口点；共享规则位于 `docs/` 目录下。

## 构建与调试

当前代码库支持的主要工作流：

1. 在 Keil µVision 中打开 `MDK-ARM/PSA.uvprojx`。
2. 构建目标 `PSA`。
3. 输出产物将写入 `MDK-ARM/PSA/` 目录下。
4. 使用 Keil 配置的调试/下载流程以及签入的 J-Link 设置进行操作。

详情和注意事项请参阅 [docs/GUIDE_build-flash-debug.md](docs/GUIDE_build-flash-debug.md)。

## 生成代码与手动维护代码

这是一个由 CubeMX 管理的项目。通常情况下：

- `Drivers/` 应被视为供应商代码。
- `Core/` 中的生成文件通常只应在 `USER CODE BEGIN/END` 区域内进行编辑。
- 自定义逻辑目前主要位于 `uart_safe.c/.h` 中。

有关代码库规则，请参阅 [docs/STANDARDS_generated-code-boundaries.md](docs/STANDARDS_generated-code-boundaries.md)。

## 已知异常

在代码库检查期间，`Core/Src/main.c` 文件的原始内容似乎混合了 HTML 构建日志内容，然后才是可读的 C 源码尾部。在将文件头部视为权威文档输入之前，应验证此问题。

跟踪记录：
- [docs/INCIDENT_main-c-integrity-anomaly.md](docs/INCIDENT_main-c-integrity-anomaly.md)
