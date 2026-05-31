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
- **遇到不确定事项？** 请首先加载并阅读 [ARCH_documentation-governance.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/ARCH_documentation-governance.md)。

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

为保证 AI 代理在长周期会话中与项目真理库（Code & Docs）保持严格一致，必须遵守以下八项原则：

| 规则 | 描述 |
|---|---|
| **不主动发起重构** | 绝不自发对稳定工作的驱动和算法代码进行大范围“重写”或“整理”，必须由人类明确指派。 |
| **禁止规则重复** | 每一个架构决策或逻辑标准只能由一个文档专属拥有，严防“双重真理源”。 |
| **只加载必要上下文** | 必须根据当前任务类型，严格按照 `ARCH_documentation-governance.md` 中的映射表按需加载文档。 |
| **绝不手动修改版本号** | 项目版本号是全局绑定的，严禁在开发过程中随意在 ad-hoc 地方变更版本信息。 |
| **严防代码注释入档** | **【硬性铁律】在任何文档中引用代码块时，必须把所有 `//`、`/* */`、`#` 注释过滤干净，仅保留代码纯逻辑。** |
| **一切变更通过校验** | 在完成任何代码或文档的修正后，必须在本地运行并成功通过 `tests/` 下的两个 PowerShell 校验脚本。 |
| **保持注册表一致** | 任何新建、更名或废弃的文档，必须首先在 `ARCH_documentation-governance.md` 中进行同步登记。 |
| **先测试后实现逻辑** | 在修改计算、数据分发或物理换算模块的逻辑时，必须先在 `tests/` 中编写或更新测试分支，严禁“盲写”。 |
