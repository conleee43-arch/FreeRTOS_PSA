<a id="documentation-governance"></a>
# ARCH_documentation-governance — 文档注册表与装载治理规范

---

## 1. 文档注册表 (Registry)

> [!WARNING]
> **在本项目中，任何未在下表中登记的文档对 Agent 均视为“不存在”。**
> 新建文档、修改文档职责或废弃文档时，必须首先在本注册表中更新相关信息。

| 文档路径 | 核心关注领域 (Contains) | 绝对禁止包含的内容 (Must NOT contain) | 默认装载时机 (Load when) |
|---|---|---|---|
| `AGENTS.md` | 会话快捷入口、项目高层元数据、关键开发指令、核心 Guardrails。 | 详细的硬件配置、复杂的通信协议、具体算法实现细节。 | **任何会话开启时默认装载** |
| [ARCH_documentation-governance.md](ARCH_documentation-governance.md#documentation-governance) | 文档系统的真理注册表、装载路由映射、文档治理与生命周期标准。 | 具体软硬件实现标准、算法公式。 | 进行文档审计或执行 `doc sweep` 时 |
| [ARCH_gui-console.md](ARCH_gui-console.md#arch-gui-console) | PySide6 上位机控制台的线程模型、UI 状态收口、虚拟状态映射、GUI runtime 测试契约。 | 串口协议字段字面量、正则细节、固件内部状态机数学逻辑。 | 分析或修改 GUI 架构、串口接收线程、控件状态流转与运行行为时 |
| [GUIDE_developer.md](GUIDE_developer.md#guide-developer) | 工业级 C/Python 开发规范、TDD 决策树、零损失重构协议、RTOS 异步开发原则。 | 芯片级硬件寄存器说明、业务逻辑与具体的通信协议报文。 | 涉及任何代码层面的修改与技术重构时 |
| [ARCH_technical-specs.md](ARCH_technical-specs.md#technical-specs) | 170MHz 时钟树、VREFBUF 电压配置、ADC/USART 底层 DMA 映射及 Table-Driven 解算属性表。 | 数据帧编解码格式、具体的高阶滤波算法代码与应用层业务。 | 涉及引脚调整、时钟变更、通道配置或物理增益修改时 |
| [STANDARDS_interface.md](STANDARDS_interface.md#standards-interface) | 串口 Measure 文本行协议、GUI `MEASURE_PATTERN` 匹配正则、`SetDAC` 指令、测试校验脚本逻辑。 | 底层 DMA 中断自愈的具体寄存器操作、中值排序滤波的内部数学算法。 | 修改通信协议、新增交互命令、或修改 GUI 接收匹配规则时 |
| [LOGIC_adc-dsp.md](LOGIC_adc-dsp.md#logic-adc-dsp) | 5点滑动中值滤波、指数 EMA 滤波、温度双点线性插值、常规通道二阶解算链路的具体逻辑及数学公式。 | 串口 DMA 数据拷贝细节、Keil 工程配置参数。 | 修改数据转换比例、微调滤波窗口或优化标定算法时 |
| [LOGIC_line-limit.md](LOGIC_line-limit.md#logic-line-limit) | 交流线阻闭环限功率与安全链校验的5阶段逻辑、计算常数、通道统计、折算公式和状态机联动。 | 串口 DMA 数据搬移细节、GPIO 初始化。 | 修改限功率保护常数、微调功率折算算法或调整单双通道硬件能力上限时 |
| [LOGIC_output-protection.md](LOGIC_output-protection.md#logic-output-protection) | 过温/过压/过流保护状态机状态转移图、跳闸迟滞计算、2秒观察期自恢复纯逻辑。 | 具体引脚配置代码、中断配置。 | 开发与验证最高优先级过温、过压、过流保护状态机逻辑时 |
| [LOGIC_calc-control.md](LOGIC_calc-control.md#logic-calc-control) | 内阻计算纯状态机（WAIT_SAFE 至 MONITOR 九段设计）、R=(U1-U2)/(I1-I2) 滤波求解数学公式。 | 串口 DMA 数据搬移细节、GPIO 初始化。 | 开发与验证内阻九段测量状态机计算逻辑时 |
| [LOGIC_pw-voc.md](LOGIC_pw-voc.md#logic-pw-voc) | PW (PB0) 与 VOC (PA5) 协作控制逻辑、三级优先级控制策略及斜坡跟随算法设计。 | 底层 ADC 物理量化公式、串口报文格式。 | 修改 PW / VOC 控制逻辑、调节步长、或优先级策略时 |
| [plans/2026-06-03-calc-3a-stability-plan.md](plans/2026-06-03-calc-3a-stability-plan.md#calc-3a-stability-plan) | 已确认的 3A 动态判稳实施计划，覆盖测试顺序、受影响文件与回归命令。 | 任何新的架构真理、硬件常数定义或替代 `LOGIC_calc-control.md` 的行为规范。 | 执行本次 3A 动态判稳修正前，用于按步骤实施时 |
| [plans/2026-06-19-ac-line-limit-implementation-plan.md](plans/2026-06-19-ac-line-limit-implementation-plan.md#ac-line-limit-implementation-plan) | 基于交流线路等效线阻动态限制系统充电功率的实现文档，包含问题描述、实施过程、测试顺序与验收标准。 | 替代 `LOGIC_calc-control.md` 的状态机真理定义、未确认的硬件规格变更。 | 执行交流线阻闭环限功率开发前，用于按步骤实施时 |
| [plans/2026-06-19-ac-line-limit-bringup-checklist.md](plans/2026-06-19-ac-line-limit-bringup-checklist.md#ac-line-limit-bringup-checklist) | 交流线阻闭环限功率上板联调检查单，覆盖示波观察点、串口状态帧顺序、期望波形与 VIN1/VIN2/VOC/IOC 参数记录要求。 | 替代接口协议真理、未确认的硬件改线结论、脱离现有命名体系的新字段定义。 | 进行上板联调、工装验收或现场故障复盘前，用于逐项核对时 |
| [plans/2026-06-20-stm32g431kb-migration-plan.md](plans/2026-06-20-stm32g431kb-migration-plan.md#stm32g431kb-migration-plan) | STM32G431CB 迁移到 STM32G431KB 且 ADC 基准改为外部稳定 2.5V 直供的实施计划，覆盖测试先行顺序、CubeMX/Keil 受影响文件、测量链路兼容策略与最终回归命令。 | 替代 `ARCH_technical-specs.md` 的长期硬件真理定义、跳过测试直接实施的口头迁移步骤、未经验证的板级改线结论。 | 执行本次 32 引脚 KB 平台移植前，用于按步骤实施时 |
| [firmware_tasks.drawio](firmware_tasks.drawio) | 固件任务逻辑关系图 (包括 4 个核心 FreeRTOS 任务、物理外设、缓冲区与消息队列总线)。 | 具体计算公式、寄存器配置字面量。 | 梳理固件的并发多任务实时架构与交互时 |
| [calc_control_fsm.drawio](calc_control_fsm.drawio) | 内阻计算任务状态机逻辑关系图 (九段式 FSM 状态、稳定/开路判定与保护重置退回逻辑)。 | 具体算法代码细节。 | 开发与验证内阻计算状态机逻辑与观察期时 |



---

## 2. 任务装载路由表 (Task → Load Mapping)

当 AI 代理在处理具体开发任务时，**禁止盲目装载整个 `docs/` 文件夹**以防上下文溢出和真理偏离。必须严格按照下表中的任务分类装载对应的文档组合：

| 任务类型 | 必须加载的文档组合 | 目的 |
|---|---|---|
| **常规修复与业务迭代** | `AGENTS.md` | 获取全局 Guardrails 与快速编译/测试指令。 |
| **信号滤波与数学解算修改** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [LOGIC_adc-dsp.md](LOGIC_adc-dsp.md#logic-adc-dsp) + [LOGIC_line-limit.md](LOGIC_line-limit.md#logic-line-limit) | 规范算法实现标准，严格遵循滤波与插值的原始数学公式。 |
| **底层硬件与通道属性修改** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [ARCH_technical-specs.md](ARCH_technical-specs.md#technical-specs) | 保证硬件时钟、引脚分配、VREFBUF 参数与二阶计算因子的一致性。 |
| **上位机交互与串口格式更改** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [STANDARDS_interface.md](STANDARDS_interface.md#standards-interface) | 确保固件格式化输出与 GUI 正则匹配机制保持 100% 同步，不破坏测试闭环。 |
| **上位机 GUI 架构与运行行为调整** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [ARCH_gui-console.md](ARCH_gui-console.md#arch-gui-console) | 规范 GUI 接收线程、连接状态收口、控件启禁、虚拟状态呈现与 runtime 契约。 |
| **文档治理与系统维护** | `AGENTS.md` + [ARCH_documentation-governance.md](ARCH_documentation-governance.md#documentation-governance) | 升级文档树，清理冗余规则，执行 `doc sweep`。 |
| **输出保护状态机开发与验证** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [LOGIC_output-protection.md](LOGIC_output-protection.md#logic-output-protection) | 规范保护状态机的跳转逻辑、时效判定、断开次序和回归测试规范。 |
| **内阻计算状态机开发与验证** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [LOGIC_calc-control.md](LOGIC_calc-control.md#logic-calc-control) | 规范九步计算状态机的时序图、阶跃电流DAC控制与电阻公式解算机制。 |
| **交流线阻限功率算法开发与验证** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [LOGIC_line-limit.md](LOGIC_line-limit.md#logic-line-limit) | 规范线阻闭环限功率与安全链校验的计算逻辑与参数阈值。 |
| **PW 与 VOC 协作控制开发与验证** | `AGENTS.md` + [GUIDE_developer.md](GUIDE_developer.md#guide-developer) + [LOGIC_pw-voc.md](LOGIC_pw-voc.md#logic-pw-voc) + [LOGIC_calc-control.md](LOGIC_calc-control.md#logic-calc-control) | 规范三级优先级控制机制及斜坡跟随自校准算法。 |

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
