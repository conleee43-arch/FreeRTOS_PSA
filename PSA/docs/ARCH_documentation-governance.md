# ARCH 文档治理规范

本代码库采用“活文档 (living-docs)”风格的文档系统，在根目录下保留精简的入口文件，将共享的权威文档存放在 `docs/` 目录下。

## 核心规则

1. 每条规则由一个文件专属负责（单一事实来源）。
2. 共享规则属于 `docs/` 目录，而不是放在 `CLAUDE.md` 或 `AGENTS.md` 中。
3. 代码和签入（提交）的配置文件是当前行为的事实来源。
4. 文档用于描述意图、工作流、边界以及特定于代码库的知识，避免每次都重新推导。
5. 在发生有意义的更改后，执行文档清理 (doc sweep) 并更新对应的归属文档。

## 文档类型

- `ARCH_` — 架构、治理、代码库结构、系统边界
- `GUIDE_` — 工作流，例如构建 (build)、烧录 (flash)、调试 (debug)、验证 (verification)
- `REF_` — 参考事实，例如引脚映射、内存大小、工具版本
- `STANDARDS_` — 编码和维护规则，特别是关于生成代码和安全边界的规则
- `LOGIC_` — 子系统行为和功能逻辑
- `INCIDENT_` — 异常、退化 (regressions) 或值得注意的调查记录

## 权威文档注册表

| 文件 | 归属内容 | 何时加载 | 何时更新 |
| --- | --- | --- | --- |
| `ARCH_documentation-governance.md` | 文档映射、所有权规则、文档清理规则 | 任何任务 | 添加或重构文档后 |
| `ARCH_repo-overview.md` | 代码库结构和运行时概览 | 了解背景、遇到架构问题时 | 代码库结构或运行时流程更改后 |
| `STANDARDS_generated-code-boundaries.md` | CubeMX/手动编辑边界 | 任何针对生成文件的代码更改时 | `.ioc` 更改、重新生成代码、边界更改后 |
| `GUIDE_build-flash-debug.md` | 构建、烧录、调试工作流 | 构建/调试/工具链任务 | Keil 配置、工具链、烧录方式更改后 |
| `REF_hardware-software-map.md` | MCU、引脚、DMA、中断映射 | 外设和硬件板相关任务 | `.ioc` 更改影响硬件映射后 |
| `REF_build-config-and-memory.md` | 内存映射、输出文件、包版本、代码大小 | 遇到构建输出或占用空间(footprint)问题时 | 工具链/包/构建输出更改后 |
| `LOGIC_uart-safe.md` | UART RX/TX DMA 行为和命令逻辑 | UART、日志、协议相关任务 | `uart_safe.c/.h` 更改后 |
| `LOGIC_output-protection.md` | 1秒节拍输出保护逻辑与故障恢复/锁存规则 | 过温/过压/过流保护任务 | 阈值、回差、执行动作或恢复策略更改后 |
| `INCIDENT_main-c-integrity-anomaly.md` | `main.c` 完整性异常跟踪 | 代码库健康度或事实来源存在争议时 | 异常被确认或解决后 |

## 任务到文档的映射

### 方向指引或常规代码库工作
阅读：
1. `ARCH_documentation-governance.md`
2. `ARCH_repo-overview.md`
3. `STANDARDS_generated-code-boundaries.md`

### 外设 / 引脚 / DMA / 中断任务
阅读：
1. `REF_hardware-software-map.md`
2. `STANDARDS_generated-code-boundaries.md`
3. 任务相关的源码/配置文件 (`PSA.ioc`, `usart.c`, `dma.c`, `stm32g4xx_it.c`)

### 构建 / 烧录 / 调试 / 工具链任务
阅读：
1. `GUIDE_build-flash-debug.md`
2. `REF_build-config-and-memory.md`
3. `PSA.ioc` 和 `MDK-ARM/PSA.uvprojx`

### UART 协议 / 日志记录 / 命令解析任务
阅读：
1. `LOGIC_uart-safe.md`
2. `REF_hardware-software-map.md`
3. `uart_safe.c/.h`

### 输出保护 / 故障锁存任务
阅读：
1. `LOGIC_output-protection.md`
2. `REF_hardware-software-map.md`
3. `output_protection.c/.h` 与 `main.c`

### CubeMX 重新生成代码或项目结构更改
阅读：
1. `STANDARDS_generated-code-boundaries.md`
2. `GUIDE_build-flash-debug.md`
3. `ARCH_repo-overview.md`

## 文档清理 (Doc sweep) 规则

在发生以下任何情况后执行文档清理：

- `PSA.ioc` 更改
- `MDK-ARM/PSA.uvprojx` 更改
- 工具链/包版本更改
- `uart_safe.c/.h` 行为更改
- 引脚、DMA、中断或时钟更改
- 发现了硬件或代码库的怪异现象 (quirk)，以免未来的智能体 (agents) 随意地进行“清理”

## 文档清理清单

1. 确认更改了什么。
2. 从上面的注册表中找到归属文档。
3. 除非文档映射本身发生了变化，否则仅更新归属文档。
4. 如果新的事实不适合任何现有的归属文档，请创建一个新的共享文档并在此处注册。
5. 如果同一条规则漂移 (drifted) 到了多个文件中，请删除重复的指南。
6. 重新检查根入口文件 (`CLAUDE.md`, `AGENTS.md`)，确保它们保持精简的入口点状态。

## 根文件策略

- `README.md` 面向人类开发者。
- `CLAUDE.md` 是 Claude Code 的引导文件。
- `AGENTS.md` 是 Codex 的引导文件。
- 它们都不应成为主要的共享知识库。

## STUBBORN_FACT (顽固事实) 的用法

如果代码库包含有意为之但令人意外的硬件、时序、DMA 或工具链约束，请使用以下模式将其记录在归属的共享文档中：

`STUBBORN_FACT: <事实> — <为什么必须保持这种状态>`

将其用于那些在智能体看来可能是错误的，但实际上是有意为之的事物。

## 事实来源注意事项

- 将 `PSA.ioc`、`MDK-ARM/PSA.uvprojx` 和当前的源文件视为事实来源。
- 如果文件看起来已损坏、混合或与其他证据不一致，请先将差异记录在 `INCIDENT_` 文档中，然后再将其规范化到参考文档中。
