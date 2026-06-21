# FreeRTOS_PSA 高精度测量系统与智能控制看板

这是一个工业级、高安全等级的微控制器电网信号采集与动态自校准系统。项目基于 **STM32G4xx** 芯片平台，采用 **FreeRTOS** 多任务实时操作系统对底层高频采样与自愈式 DMA 串口通信进行编排调度；上位机控制台采用 **PySide6** (Python) 构建高保真深色工业控制看板，实现了电网与高压参数的实时捕获、闭环控制指令下发与 Painter 高流畅折线绘制。

---

## 1. 系统核心设计亮点

### 1.1 高精度自适应信号滤波与解算 (LOGIC)
- **5点自适应滑动中值滤波**：通过冒泡排序算法实时计算有效中位数，完美滤除大电网瞬态脉冲尖峰噪声，并在开机样本数小于5时自适应收缩排序窗口，消除爬坡等待延迟。
- **动态供电基准自校准**：通过采集内部参考电压 $V_{REFINT}$，逆向推导其实时真实的参考基准 $V_{REF+}$，并配合 EMA 指数移动平均算法（平滑因子 0.05）滤除高频抖动，彻底抵消电源波动引入的采样增益误差。
- **片温双点线性插值**：利用温度传感器在 3.0V 下的等效标定电压转换，并读取芯片内部 ROM 中固化存储的 $30^\circ\text{C}$ 与 $130^\circ\text{C}$ 标定码进行高精度线性插值解算结温。
- **表驱动二阶物理量转换**：集中配置前级衰减比例因子与通道偏置数值，解算“数码量 -> 引脚模拟电压 -> 前级物理量”的严密二阶转换链路。

### 1.2 高可靠、自愈式非阻塞 UART DMA 驱动 (DRV)
- **非阻塞链式环形发送**：在 PRIMASK 硬件原子保护下，深拷贝应用层发送包入二维队列，规避并发篡改风险；物理 DMA 发送结束后以中断链式拉出下一帧。
- **双缓冲不定长接收与自愈**：依靠串口空闲中断 IDLE 与满缓冲区 TC 接收不定长帧，显式禁用半传输中断以消除开销；若发生总线溢出（ORE）或接收断裂，驱动在主循环 Poll 时间片中实施安全重启自愈。
- **发送容错帧解耦**：将遥测物理量数据帧 `[Measure]` 与辅助诊断码帧 `[Code]` 彻底解耦。即使诊断帧溢出截断，也绝不抑制主测量帧的发送，符合高安全容错准则。

---

## 2. 软硬件系统架构

项目严格遵循三层架构分离（3-Layer Separation）的开发原则，实现了高度解耦：

```mermaid
graph TD
    subgraph 硬件与底层抽象 (HAL/DRV)
        A[ADC1 DMA 乒乓采集] -->|原始 6通道码值| B[滑动中值滤波器]
        C[USART1 DMA 接收与发送] -->|Poll 时间片自愈| D[非阻塞环形发送队列]
    end
    
    subgraph 中间件与数学解算 (PROCESSOR)
        B -->|中值滤波原始码| E[动态 VREF 逆向解算]
        E -->|EMA 平滑基准| F[温度 3.0V 标定转换]
        F -->|ROM 双点线性插值| G[摄氏结温计算]
        E -->|VREF_EMA 供电基准| H[通道二阶增益还原]
    end
    
    subgraph 任务编排与调度 (ORCHESTRATOR)
        I[sampleFilterTask 1ms 采样/滤波] -->|高频更新物理解算| H
        I -->|1000ms 心跳| J[广播 Measure 与 Code 帧]
        E1[emergencyTask 2ms 保护轮询] -->|过温/过压/过流门控| O[Output Control]
        C1[calcControlTask 10ms 内阻状态机] -->|3A/2A 阶跃与 Calc 帧| O
        K[uartTask 5ms 串口轮询] -->|分发命令与非阻塞发送| L[Usart1_RxParser_Callback]
    end

    J -->|物理串口| M[PySide6 工业监测看板]
    C1 -->|State/Stable/Calc 诊断帧| M
    M -->|SystemReset / DebugPause2A / DebugResume2A / 手动命令| L
```

---

### 2.1 当前 FreeRTOS 任务拓扑

当前固件由四个应用任务协同运行：

| 任务 | 周期 | 职责 |
|---|---:|---|
| `sampleFilterTask` | 1ms | 驱动 `Measure_Update()`，每 1000ms 发送 `[Measure]` 与 `[Code]` 帧 |
| `emergencyTask` | 2ms | 轮询输出保护状态机，执行 OF_EN/DAC 安全动作，并上报保护/输出状态 |
| `calcControlTask` | 10ms | 执行内阻九段状态机，控制 3A/2A 阶跃，输出 `[Stable]`、`[Calc]` 与计算状态帧 |
| `uartTask` | 5ms | 轮询 UART DMA RX、解析上位机命令、串行化发送队列 |

---

## 3. 快速开始与构建指南

### 3.1 固件编译 (MDK-ARM)
1. 启动 Keil uVision 开发环境。
2. 打开项目工程文件 `MDK-ARM/FreeRTOS_PSA.uvprojx`。
3. 执行 `Rebuild` 进行固件全编译并烧录至 STM32G4 目标板。

### 3.2 运行上位机监测控制台
推荐使用 Python 虚拟环境以防止依赖库冲突：

```bash
cd gui
python -m venv venv
.\venv\Scripts\activate
pip install -r requirements.txt
python main.py
```

---

## 4. 自动化回归测试与校验

为防止 CubeMX 重新生成代码时覆写关键的 VREFBUF 基准配置，或串口协议字段与 GUI 的正则表达式捕获逻辑产生脱节，项目在 `tests/` 文件夹中提供了 PowerShell 自动化测试脚本，执行命令如下：

```powershell
# 1. 运行硬件标定与内部基准地址自检脚本
powershell -File tests/check_adc_config.ps1

# 2. 运行通信行协议、状态机模型与 GUI runtime 一致性脚本
powershell -File tests/check_uart_gui_protocol.ps1

# 3. 如需单独检查 GUI 运行态布局与交互契约
powershell -File tests/check_gui_runtime_behavior.ps1
```

---

## 5. 活文档（Living Docs）维护指南

本项目遵循 `Diew/living-docs` 活文档体系规范。关于开发标准、架构机密以及协议定义的唯一真理源泉全部维护在 `docs/` 目录下。

### 5.1 会话快捷入口 (`AGENTS.md`)
AI 代理在启动任务前默认会阅读项目根目录下的 [AGENTS.md](AGENTS.md#agent-context) 文件。它包含高层项目元数据、基本测试命令以及防重构等八项防错 Guardrails 交互原则。

### 5.2 任务装载真理路由 (`docs/`)
在开发或维护不同的子系统时，严禁盲目读取整个 `docs/` 文件夹。必须首先根据 [docs/ARCH_documentation-governance.md](docs/ARCH_documentation-governance.md#documentation-governance) 文档注册表，按需装载对应的核心子文档组合：

- **开发指南与重构标准**：参考 [docs/GUIDE_developer.md](docs/GUIDE_developer.md#guide-developer)。包含 **200Hz Poll 轮询时间片标准**、TDD 决策树和零损失重构协议。
- **硬件配置与通道定义**：参考 [docs/ARCH_technical-specs.md](docs/ARCH_technical-specs.md#technical-specs)。包含 170MHz 时钟、VREFBUF 内部电压缓冲 SCALE1 配置及静态表驱动解算属性。
- **通信行协议与 GUI 接口**：参考 [docs/STANDARDS_interface.md](docs/STANDARDS_interface.md#standards-interface)。包含 `[Measure]`、`[Code]`、`[Calc]`、`[State]` 帧格式，GUI 捕获正则表达式，控制命令与安全门控回执语义。
- **核心解算算法**：参考 [docs/LOGIC_adc-dsp.md](docs/LOGIC_adc-dsp.md#logic-adc-dsp)。包含滑动中值滤波、EMA 基准滤波和双点线性插值的数学公式实现。
