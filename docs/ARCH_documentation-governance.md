# ARCH_documentation-governance — 文档注册表与装载治理规范

---

## 1. 文档注册表 (Registry)

> [!WARNING]
> **在本项目中，任何未在下表中登记的文档对 Agent 均视为“不存在”。**
> 新建文档、修改文档职责或废弃文档时，必须首先在本注册表中更新相关信息。

| 文档路径 | 核心关注领域 (Contains) | 绝对禁止包含的内容 (Must NOT contain) | 默认装载时机 (Load when) |
|---|---|---|---|
| `AGENTS.md` | 会话快捷入口、项目高层元数据、关键开发指令、核心 Guardrails。 | 详细的硬件配置、复杂的通信协议、具体算法实现细节。 | **任何会话开启时默认装载** |
| [ARCH_documentation-governance.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/ARCH_documentation-governance.md) | 文档系统的真理注册表、装载路由映射、文档治理与生命周期标准。 | 具体软硬件实现标准、算法公式。 | 进行文档审计或执行 `doc sweep` 时 |
| [GUIDE_developer.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/GUIDE_developer.md) | 工业级 C/Python 开发规范、TDD 决策树、零损失重构协议、RTOS 异步开发原则。 | 芯片级硬件寄存器说明、业务逻辑与具体的通信协议报文。 | 涉及任何代码层面的修改与技术重构时 |
| [ARCH_technical-specs.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/ARCH_technical-specs.md) | 170MHz 时钟树、VREFBUF 电压配置、ADC/USART 底层 DMA 映射及 Table-Driven 解算属性表。 | 数据帧编解码格式、具体的高阶滤波算法代码与应用层业务。 | 涉及引脚调整、时钟变更、通道配置或物理增益修改时 |
| [STANDARDS_interface.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/STANDARDS_interface.md) | 串口 Measure 文本行协议、GUI `MEASURE_PATTERN` 匹配正则、`SetDAC` 指令、测试校验脚本逻辑。 | 底层 DMA 中断自愈的具体寄存器操作、中值排序滤波的内部数学算法。 | 修改通信协议、新增交互命令、或修改 GUI 接收匹配规则时 |
| [LOGIC_adc-dsp.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/LOGIC_adc-dsp.md) | 5点滑动中值滤波、指数 EMA 滤波、温度双点线性插值、常规通道二阶解算链路的具体逻辑及数学公式。 | 串口 DMA 数据拷贝细节、Keil 工程配置参数。 | 修改数据转换比例、微调滤波窗口或优化标定算法时 |

---

## 2. 任务装载路由表 (Task → Load Mapping)

当 AI 代理在处理具体开发任务时，**禁止盲目装载整个 `docs/` 文件夹**以防上下文溢出和真理偏离。必须严格按照下表中的任务分类装载对应的文档组合：

| 任务类型 | 必须加载的文档组合 | 目的 |
|---|---|---|
| **常规修复与业务迭代** | `AGENTS.md` | 获取全局 Guardrails 与快速编译/测试指令。 |
| **信号滤波与数学解算修改** | `AGENTS.md` + [GUIDE_developer.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/GUIDE_developer.md) + [LOGIC_adc-dsp.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/LOGIC_adc-dsp.md) | 规范算法实现标准，严格遵循滤波与插值的原始数学公式。 |
| **底层硬件与通道属性修改** | `AGENTS.md` + [GUIDE_developer.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/GUIDE_developer.md) + [ARCH_technical-specs.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/ARCH_technical-specs.md) | 保证硬件时钟、引脚分配、VREFBUF 参数与二阶计算因子的一致性。 |
| **上位机交互与串口格式更改** | `AGENTS.md` + [GUIDE_developer.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/GUIDE_developer.md) + [STANDARDS_interface.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/STANDARDS_interface.md) | 确保固件格式化输出与 GUI 正则匹配机制保持 100% 同步，不破坏测试闭环。 |
| **文档治理与系统维护** | `AGENTS.md` + [ARCH_documentation-governance.md](file:///d:/zhihai/Software/FreeRTOS_PSA/docs/ARCH_documentation-governance.md) | 升级文档树，清理冗余规则，执行 `doc sweep`。 |

---

## 3. 命名与治理规范 (Convention & Governance)

### 3.1 单一真理所有权 (Canonical Ownership)
- **硬性原则：一条规则或参数在项目中只能有一个绝对所有者。**
- 例如，通信波特率 `115200` 只能在 `STANDARDS_interface.md` 中被定义为真理，其他文件（如 `AGENTS.md`）如需提及，必须通过超链接引用，绝不在本地复制其具体数值。

### 3.2 物理超链标准 (Hyperlink Standard)
- 所有文档之间的关联必须使用相对 Markdown 路径进行互联：`[说明文字](FILENAME.md#header)`。
- 禁止使用绝对路径，必须采用显式锚点，以方便 Agent 精准定位。

### 3.3 文档生命周期与“Doc Sweep”
- 当项目发生代码修改时，**Agent 禁止在未得到人类明确授权前自发更新文档**。
- 文档更新必须作为独立的、由人类确认代码正确后的“文档清扫会话”（Doc Sweep）来执行。
- 在“Doc Sweep”中，Agent 必须检查代码中是否存在新增的控制结构或隐藏常数，并将其剥离到对应的文档中，同时更新本注册表。
