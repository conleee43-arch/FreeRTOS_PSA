# STANDARDS 生成代码边界

本代码库由 STM32CubeMX 和 Keil MDK-ARM 管理。最安全的默认做法是保留生成代码的边界，并隔离手动编辑的代码。

## 权威配置来源

- 硬件和外设配置的事实来源：`PSA.ioc`
- 实际签入的构建/调试事实来源：`MDK-ARM/PSA.uvprojx`
- 供应商框架来源：`Drivers/`

## 默认编辑规则

### 安全的手动维护逻辑位置
- `Core/Src/uart_safe.c` 与 `Core/Inc/uart_safe.h` (串口命令/日志)
- `Core/Src/adc_measure.c` 与 `Core/Inc/adc_measure.h` (高精度ADC采样与校准)
- `Core/Src/dac_control.c` 与 `Core/Inc/dac_control.h` (PFC电流DAC输出控制)
- CubeMX 生成文件中的 `USER CODE BEGIN/END` 区域
- `docs/` 下的共享文档
- 根入口文档：`README.md`, `CLAUDE.md`, `AGENTS.md`

### 生成文件：默认策略
对于 `Core/` 下的生成文件：
- 最好仅在 `USER CODE BEGIN/END` 块内进行编辑
- 如果更改必须涉及生成区域，请记录原因并确认它在重新生成后仍然存在
- 在进行此类更改后，执行文档清理 (doc sweep) 并验证 CubeMX 稍后是否会覆盖它

### 供应商文件
- 除非明确需要并有记录，否则不要手动编辑 `Drivers/`
- 任何供应商补丁都应被视为例外，并立即在文档中说明

## 外设功能扩展：解耦与隔离设计规范

当需要对 CubeMX 自动生成的外设（如 `adc.c`、`dac.c` 等）进行控制逻辑扩展、算法封装或档位映射时，应严格遵循**“隔离设计”**原则：

1. **严禁直接修改自动生成文件**：绝对不直接在 `dac.c/h` 或是 `adc.c/h` 中编写定制算法，防止 CubeMX 下次自动生成代码时将修改全面覆盖。
2. **新建专属的隔离逻辑层**：于 `Core/Src/` 和 `Core/Inc/` 目录下创建带有 `_control` 或类似命名的专属逻辑文件（例如 `dac_control.c` 与 `dac_control.h`）。
3. **句柄继承与无缝交互**：通过在新头文件中 `#include "dac.h"` 的方式无缝引入 `hdac1` 等全局外设句柄，并在自己的控制文件内编写全部控制动作与算式。
4. **通过 USER CODE 建立最小关联**：仅在 `main.c` 里的 `USER CODE Includes` 引入新头文件，在外设初始化完成后的 `USER CODE 2` 段安全启动扩展控制，实现系统的平稳运行。

## CubeMX 重新生成策略

在更改 `PSA.ioc` 或重新生成代码后：
1. 验证生成的代码是否仍保留预期的用户区域。
2. 如果项目结构发生变化，请重新检查 `MDK-ARM/PSA.uvprojx`。
3. 更新负责受影响事实的文档：
   - 硬件映射
   - 构建/调试工作流
   - 生成代码边界说明
4. 重新构建并验证输出。

## Keil 工程同步策略

手动添加新源文件时：
- 如果 IDE 工程是受支持工作流的一部分，请将其添加到 Keil 工程中。
- 在相应的共享文档中记录新源文件的归属。

## 构建工具不匹配规则

`PSA.ioc` 当前报告 `ProjectManager.CompilerLinker=GCC`，但签入且最近使用的构建路径是采用 ArmClang 的 Keil MDK-ARM。

规则：
- 在项目被有意迁移之前，将 Keil/ArmClang 视为当前代码库实际支持的工作流。
- 不要仅仅因为 CubeMX 元数据提到了 GCC 就假设 GCC 构建命令可用。

## 中断、DMA 和时序安全规则

对于嵌入式安全敏感路径：
- 避免将阻塞工作移入 ISR（中断服务例程）中。
- 除非任务明确要求重新设计，否则保留 DMA/IRQ 的所有权边界。
- 对于涉及系统时钟、中断优先级、DMA 模式或复位行为的任何更改要保持谨慎。

## STUBBORN_FACT (顽固事实) 记录

当必须保留一个令人意外的约束时，请使用以下模式：

`STUBBORN_FACT: <事实> — <为什么必须保持这种状态>`

在此代码库中，适合记录的情况包括：
- 异常的工具链不匹配
- 有意为之的 DMA 模式选择
- 奇怪的复位或初始化顺序要求
- 正在调查中的代码库完整性异常

## 当前注意事项

当前正在跟踪 `Core/Src/main.c` 的一个代码库完整性异常：在原始文件读取期间显示混合了 HTML 构建日志内容。在验证和解决之前，避免将该文件头部作为架构或标准文档的唯一事实依据。

参见：
- `INCIDENT_main-c-integrity-anomaly.md`
