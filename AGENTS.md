<a id="agent-context"></a>
# FreeRTOS_PSA — Agent Context (v1.0.0) — 2026-05-31

## Project Setup
- **Project Name**: FreeRTOS_PSA
- **Version**: v1.0.0 (基准物理通道与非阻塞驱动固化版本)
- **Status**: Active
- **Tech Stack**: STM32G4xx HAL, FreeRTOS (CMSIS-OS V2), PySide6, Python, PowerShell
- **Context Anchors**: None

## Documentation Priority
- `docs/` 是项目行为、架构、协议和实现逻辑的唯一真理源泉。
- `AGENTS.md` 仅作为高优先级的会话入口、关键规则、元数据和高层引导，绝不包含详细的开发标准和逻辑细节。
- **遇到不确定事项？** 请首先加载并阅读 [docs/ARCH_documentation-governance.md](docs/ARCH_documentation-governance.md#documentation-governance)。

## Commands
- **编译固件 (MDK-ARM)**: 启动 Keil uVision，打开 `MDK-ARM/FreeRTOS_PSA.uvprojx` 编译工程。
- **运行控制台 GUI**:
  ```bash
  cd gui
  python main.py
  ```
- **运行硬件与标定配置校验**:
  ```powershell
  powershell -File tests/check_adc_config.ps1
  ```
- **运行通信协议与正则校验**:
  ```powershell
  powershell -File tests/check_uart_gui_protocol.ps1
  ```

## Milestones
- [x] v1.0.0: 多通道 ADC 乒乓 DMA 采集、指数 EMA 基准自校准与片温插值解算，非阻塞环形 UART DMA 驱动自愈及 PySide6 监测看板实现。
- [ ] v1.1.0: 引入多传感器双向偏置校准支持，增强电网谐波分析计算（Planned）。

---

## Agent Session Guardrails

为保证 AI 代理在长周期会话中与项目真理库（Code & Docs）保持严格一致，必须遵守以下七项原则：

| 规则 | 描述 |
|---|---|
| **不主动发起重构** | 绝不自发对稳定工作的驱动 and 算法代码进行大范围“重写”或“整理”，必须由人类明确指派。 |
| **禁止规则重复** | 每一个架构决策或逻辑标准只能由一个文档专属拥有，严防“双重真理源”。 |
| **只加载必要上下文** | 必须根据当前任务类型，严格按照 `ARCH_documentation-governance.md` 中的映射表按需加载文档。 |
| **绝不手动修改版本号** | 项目版本号是全局绑定的，严禁在开发过程中随意在 ad-hoc 地方变更版本信息。 |
| **一切变更通过校验** | 在完成任何代码或文档的修正后，必须在本地运行并成功通过 `tests/` 下的两个 PowerShell 校验脚本。 |
| **保持注册表一致** | 任何新建、更名或废弃的文档，必须首先在 `ARCH_documentation-governance.md` 中进行同步登记。 |
| **先测试后实现逻辑** | 在修改计算、数据分发或物理换算模块的逻辑时，必须先在 `tests/` 中编写或更新测试分支，严禁“盲写”。 |

---

## Karpathy 代理行为准则 (Karpathy Agent Guidelines)

为进一步规避大语言模型（LLM）在自主编码时的常见痛点，本项目引入源自 Andrej Karpathy 观察提炼的四项核心代理准则：

### 1. 先想后写 (Think Before Coding)
- **不假设，不隐藏困惑**：优先明确方案的先决条件。若遇到需求模糊，必须向人类询问求证，严禁盲目猜测并代为决策。
- **呈现权衡**：如果存在多种实现路径或取舍，应先向人类陈述各方案优缺点，绝不暗中替用户做出选择。
- **敢于反驳**：如果有更简洁的替代方案，应主动指出，不盲从不合理的复杂规划。

### 2. 简约至上 (Simplicity First)
- **极简主义**：仅编写能完美解决当前问题最少、最直接的代码，绝不进行投机性或超出需求的过度设计。
- **禁止过度抽象**：严禁对单次使用的代码或函数进行不必要的封装、设计模式重构或配置化处理。
- **零冗余逻辑**：不编写应对“不可能场景”的冗余错误处理代码。如果 50 行就能写完，绝不写成 200 行。

### 3. 外科手术式修改 (Surgical Changes)
- **精准编辑**：仅触及必须更改的文件和代码行。绝不随意“润色”相邻的未损坏代码、格式或已有注释，保持差异（Diff）的纯净。
- **保持现有风格**：严格遵循项目已有的编码和排版风格，即便这与你自身的偏好存在差异。
- **独立清理无用代码**：如果你的修改导致原有的某些 imports、变量或函数成为死代码，必须由你将其清除；但绝不自发删除非你改动产生的历史死代码。

### 4. 目标驱动执行 (Goal-Driven Execution)
- **定义验证机制**：为复杂或多步骤任务设定明确的验收验证条件，而不是仅使用含糊的“让其运行”。
- **闭环验证循环**：对于长流程开发，应当通过制定类似 `1. [步骤A] -> 验证: [测试A] 2. [步骤B] -> 验证: [测试B]` 的分步验证计划，确保每一步的产出都在验证环中成功收敛。
