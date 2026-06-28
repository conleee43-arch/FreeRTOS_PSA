<a id="standards-interface"></a>
# STANDARDS_interface — 软硬件通信接口与交互协议标准

---

## 1. 遥测数据行协议 (Telemetry Line Protocol)

本项目固件通过 USART1 串口向 GUI 上位机高频广播遥测包，遥测数据采用严格的 ASCII 字符文本行进行封装，格式以 `[Measure]` 前缀标识，结尾以 `\r\n` 结尾：

```c
int measure_len = snprintf(measure_buf, sizeof(measure_buf),
                           "\r\n[Measure] V1:%0.2fV V2:%0.2fV CO:%0.2fA VO:%0.2fV T:%0.1fC Vref:%0.3fV\r\n",
                           Measure_GetV1In(),
                           Measure_GetV2In(),
                           Measure_GetCoOut(),
                           Measure_GetVoOut(),
                           Measure_GetTemp(),
                           Measure_GetVref());
```

字段含义：

| 字段 | 单位 | 含义 |
|---|---:|---|
| `V1` | V | 交流输入 1 的物理量还原值 |
| `V2` | V | 交流输入 2 的物理量还原值 |
| `CO` | A | 直流输出电流 `CO_OUT` |
| `VO` | V | 直流输出电压 `VO_OUT` |
| `T` | °C | MCU 结温 |
| `Vref` | V | 当前工作参考电压 |

---

## 2. 上位机捕获正则表达式标准 (MEASURE_PATTERN)

上位机 GUI 看板通过高效的工作线程，在非阻塞时间片中运用如下的高精确正则表达式捕获提取遥测参数（交流电压1峰值、交流电压2峰值、直流母线输出电流、直流母线输出电压、芯片结温、工作参考电压）：

```python
MEASURE_PATTERN = re.compile(
    r"\[Measure\]\s+V1:([\d\.-]+)V\s+V2:([\d\.-]+)V\s+CO:([\d\.-]+)A\s+VO:([\d\.-]+)V\s+T:([\d\.-]+)C\s+Vref:([\d\.-]+)V"
)
```

---

## 3. 控制指令下发协议 (SetDAC Command)

> [!NOTE]
> 在 v1.0.0 固件算法与非阻塞驱动固化版本中，上位机 GUI 控制面板已移除了直接控制电流的滑块与数值调节框，改由下位机计算状态机自主触发阶跃。但在协议层面，下位机固件依然完整兼容 `SetDAC` 指令（可通过上位机底部的“手动发码”控制台进行调试发送）。

发送指令格式（例如设定 3.00A）：
```
SetDAC:3.00\r\n
```

固件在收到指令后会动态调整 PA4 引脚对应的 DAC 理论输出。

### 3.1 安全门控与拒绝回执

`SetDAC` 属于会改变功率级目标电流的控制命令，必须满足以下条件才会被执行：

1. 测量链路已经就绪：`Measure_IsReady() == true`。
2. 输出保护状态机处于 `NORMAL`。
3. `Output_Control_SetCurrent()` 未拒绝该请求。

当上述任一条件不满足时，固件不执行该控制动作，并回送：

```
[State] STATE:CONTROL_REJECTED
```

该回执是安全门控结果，不表示串口解析失败。

---

## 4. 保护使能控制指令 (SetOF Command)

> [!NOTE]
> 上位机 GUI 控制面板已移除了物理保护使能开关，但下位机固件依然完整兼容该控制协议，可通过上位机底部的“手动发码”控制台进行调试发送。

发送使能保护指令格式：
```
SetOF:1\r\n
```

发送禁用保护指令格式：
```
SetOF:0\r\n
```

`SetOF:1` 与 `SetDAC` 使用相同安全门控：测量未就绪、保护状态非 `NORMAL` 或输出控制层拒绝时，固件回送 `[State] STATE:CONTROL_REJECTED` 且不会拉高 `OF_EN`。

`SetOF:0` 是安全关断命令，允许在任意状态下执行，用于拉低 `OF_EN` 并将 DAC 目标电流归零。

输出使能状态变化时，固件会通过状态帧上报：

```
[State] STATE:OF_ENABLED
[State] STATE:OF_DISABLED
```

---

## 5. 系统复位控制指令 (SystemReset Command)

GUI 系统控制按钮在点击后，向下位机推流 MCU 复位命令：

```
SystemReset\r\n
```

固件在收到该命令后，先通过串口回送确认行：

```
[System] Resetting...
```

随后执行 `NVIC_SystemReset()`。从协议视角看，这会导致遥测与状态回传出现一次短暂中断；至于主机侧串口句柄是否保持、断开后是否需要重新打开，由当前 GUI 实现与操作系统串口驱动行为共同决定，本协议不将“自动重连”定义为必备契约。

---

## 6. 2A 稳定等待挂起/恢复调试指令 (DebugPause2A / DebugResume2A)

GUI “状态与内阻诊断”卡片中的 2A 等待挂起按钮在点击时，发送以下调试指令：

### 挂起命令
```
DebugPause2A\r\n
```

固件在收到挂起命令后，如果当前正处于 `WAIT_2A_STABLE` 主状态：
- 设置挂起标志，暂停 10s 等待窗口的计时推进
- 向上位机回送虚拟状态行：
```
[State] STATE:WAIT_2A_PAUSED\r\n
```

如果当前不在 `WAIT_2A_STABLE` 主状态，固件回复：
```
[State] STATE:PAUSE_FAILED\r\n
```

### 恢复命令
```
DebugResume2A\r\n
```

固件在收到恢复命令后，如果当前处于挂起状态：
- 追加挂起期间时长到状态起始时间戳
- 从挂起点继续计算剩余 2A 等待窗口
- 向上位机回送恢复确认：
```
[State] STATE:WAIT_2A_STABLE_10S\r\n
```

如果当前未处于挂起状态，固件回复：
```
[State] STATE:RESUME_FAILED\r\n
```

### 虚拟状态映射说明
`WAIT_2A_PAUSED` 是虚拟状态，不修改 C 语言状态机枚举。它仅用于：
- 上位机 GUI 识别 2A 等待已挂起
- 更新挂起按钮的交替状态样式

---

## 7. 内阻计算报告帧 (`[Calc]`)

内阻状态机完成 `STATE_CALC_RESISTANCE` 后，固件通过独立 UART 帧发布计算报告：

```
[Calc] R:<resistance>R U1:<u1>V I1:<i1>A U2:<u2>V I2:<i2>A IOC:<ioc>A
```

示例：

```
[Calc] R:2.000R U1:400.00V I1:3.00A U2:398.00V I2:2.00A IOC:3.00A
```

字段含义：

| 字段 | 单位 | 含义 |
|---|---:|---|
| `R` | Ω | 由 `(U1 - U2) / (I1 - I2)` 计算得到的直流等效内阻 |
| `U1` | V | 3A 稳定后锁存的 `VO_OUT` |
| `I1` | A | 3A 稳定后锁存的 `CO_OUT` |
| `U2` | V | 2A 稳定后锁存的 `VO_OUT` |
| `I2` | A | 2A 稳定后锁存的 `CO_OUT` |
| `IOC` | A | 当前实现中的 IOC 观测/占位值，暂以 3A 锁存电流 `I1` 上报；尚未表示基于内阻动态计算出的最大安全限流值 |

GUI 必须使用 `CALC_PATTERN` 独立解析该帧，不得从终端日志文本中二次推断字段。

---

## 8. 原始 ADC 码诊断帧 (`[Code]`)

固件在 `[Measure]` 遥测帧之后，使用独立物理 DMA 帧输出五路原始码诊断信息：

```
[Code] 0:<raw0>,1:<raw1>,2:<raw2>,3:<raw3>,4:<raw4>
```

字段含义如下：
- `raw0`：`V1_IN` 交流电压通道 1 原始码
- `raw1`：`V2_IN` 交流电压通道 2 原始码
- `raw2`：`CO_OUT` 直流输出电流原始码
- `raw3`：`VO_OUT` 直流输出电压原始码
- `raw4`：`TEMPSENSOR` 芯片温度通道原始码

该帧用于底层 ADC/校准调试。GUI 可以将其显示在终端日志中，但不得把它作为主仪表盘物理量来源；主仪表盘只能消费 `[Measure]` 帧。

---

## 9. 状态帧全集 (`[State]`)

固件通过 `[State] STATE:<state>` 统一上报保护状态、输出控制状态、内阻计算状态与调试命令回执。

### 9.1 输出保护状态

| 状态值 | 含义 |
|---|---|
| `NORMAL` | 输出保护状态机处于正常运行，允许安全门控通过 |
| `TRIPPED` | 发生过温/过压/过流跳闸，输出被强制关闭 |
| `RECOVERY_WAIT` | 越限已解除，正在进行 2s 恢复观察 |

### 9.2 输出控制状态

| 状态值 | 含义 |
|---|---|
| `OF_ENABLED` | `OF_EN` 已拉高，物理输出通道已使能 |
| `OF_DISABLED` | `OF_EN` 已拉低，物理输出通道已关闭，DAC 目标电流归零 |
| `CONTROL_REJECTED` | 控制命令被测量就绪/保护状态/输出控制层安全门控拒绝 |

### 9.3 内阻计算状态

| 状态值 | 对应 C 状态或视图 | 含义 |
|---|---|---|
| `WAIT_SAFE` | `CALC_CONTROL_WAIT_SAFE` | 等待保护状态进入安全可运行条件 |
| `SET_3A` | `CALC_CONTROL_SET_3A` | 下发 3A 阶跃目标 |
| `WAIT_3A_STABLE` | `CALC_CONTROL_WAIT_3A_STABLE` | 以 100ms 采样节拍进行 3A 动态稳定判定 |
| `LATCH_3A` | `CALC_CONTROL_LATCH_3A` | 锁存第一组 `U1/I1` |
| `SET_2A` | `CALC_CONTROL_SET_2A` | 下发 2A 阶跃目标 |
| `WAIT_2A_STABLE_10S` | `CALC_CONTROL_WAIT_2A_STABLE` | 等待 2A 稳态观察窗口；当前窗口为 10s |
| `WAIT_2A_PAUSED` | 虚拟状态视图 | 仍处于 `WAIT_2A_STABLE`，但等待计时被调试命令冻结 |
| `LATCH_2A` | `CALC_CONTROL_LATCH_2A` | 锁存第二组 `U2/I2` |
| `CALC_RESISTANCE` | `CALC_CONTROL_CALC_RESISTANCE` | 执行内阻公式计算 |
| `MONITOR` | `CALC_CONTROL_MONITOR` | 内阻计算完成，进入静态监控 |

### 9.4 调试命令结果状态

| 状态值 | 含义 |
|---|---|
| `PAUSE_FAILED` | `DebugPause2A` 未生效，通常表示当前不在 2A 等待状态或已经挂起 |
| `RESUME_FAILED` | `DebugResume2A` 未生效，通常表示当前未处于挂起状态 |

---

## 10. 状态机诊断行协议 (Stable / Error Diagnostics)

除 `[Measure]` 与 `[Calc]` 主业务数据帧外，固件允许输出以下诊断行，用于终端观测 3A 判稳过程与异常分支：

稳定判定过程行：
```
[Stable] Count:<stable_count>/<stable_target> Delta:<delta_mA>mA Wait:<elapsed_ms>ms
```

其中：
- `stable_count`：当前连续稳定次数，必须上报为**本次采样更新后的计数值**
- `stable_target`：稳定目标次数，当前固定为 `5`
- `delta_mA`：本次采样与上次采样的电流差值，单位 mA
- `elapsed_ms`：自进入 `WAIT_3A_STABLE` 起累计等待时长，单位 ms

开路异常行：
```
[Error] OPEN_CIRCUIT_DETECTED
```

该异常表示 3A 判稳流程已经达到连续稳定次数要求，但当前 `CO_OUT` 幅值仍低于开路阈值，状态机会锁存异常并退回 `WAIT_SAFE`，不会在安全状态保持不变时自动重新发起 3A 探测。

---

## 11. 协议边界限制与闭环校验标准

为保障在 115200bps 甚至更低物理波特率下传输不发生粘包与 DMA 搬运溢出错误，串口交互格式受到以下强约束限制：

- **单包物理长度限制**：遥测包长度在最恶劣极限状况下（负温度、四位电压数），也严禁超过固件的 UART 应用层消息长度限制（128 字节）。
- **帧独立分离原则**：遥测主数据帧 `[Measure]`、诊断码帧 `[Code]`、内阻计算帧 `[Calc]` 与状态帧 `[State]` 必须作为可独立解析的文本行输出，禁止依赖跨帧字段拼接。
- **GUI 主仪表盘数据源限制**：ADC 实时数值只能来自 `[Measure]`；内阻诊断只能来自 `[Calc]`；状态按钮与保护状态只能来自 `[State]`。

此通信协议与上位机界面的闭环一致性由以下自动化测试脚本强制保证：

1. **通信格式与状态机行为校验**：运行 [../tests/check_uart_gui_protocol.ps1](../tests/check_uart_gui_protocol.ps1)。它会检查固件输出格式是否包含 GUI 兼容字符串，并调用 [../tests/check_state_machines_behavior.py](../tests/check_state_machines_behavior.py) 来校验状态机在各种情况（例如开路检测、2A 挂起/恢复）下的逻辑流转与算法表现。
2. **GUI 动态布局与交互行为校验**：运行 [../tests/check_gui_runtime_behavior.ps1](../tests/check_gui_runtime_behavior.ps1)。它会调用 [../tests/check_gui_runtime_behavior.py](../tests/check_gui_runtime_behavior.py)，在无头模式（offscreen）下实例化上位机 Console，检查所有的必需标签（如 `lbl_dcr_val`、`lbl_protect_state` 等）以及 2A 挂起/恢复按钮是否正确呈现在布局中，并模拟串口接收确认帧以验证 UI 的解析和显示。
