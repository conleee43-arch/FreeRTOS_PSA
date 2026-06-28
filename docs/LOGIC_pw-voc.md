<a id="logic-pw-voc"></a>
# LOGIC_pw-voc — PW 与 VOC 协作控制设计说明书

---

## 1. 核心设计目的

为支持平滑的模拟输出调节，并防止外部控制阶跃对系统造成剧烈电磁与电气冲击，系统设计了基于 **PW** (脉宽/电平输入) 与 **VOC** (模拟输出控制) 的协作调节机制。该机制通过设定三层递减的控制优先级，将电气故障保护、内阻阶跃测量及外部实时控制有机结合，成为控制级电压控制的核心真理。

本说明书作为 PW-VOC 协作控制策略的唯一真理源，约束 `app_freertos.c` 中对应任务的业务实现与回归校验。

---

## 2. 物理硬件与驱动绑定

控制链包含一组离散输入与一组模拟输出，其硬件与底层驱动接口约束如下：

### 2.1 PW (GPIO 输入)
- **引脚通道**：`GPIOB_PIN_0` (对应 EXTI_PB0)。
- **驱动配置**：双边沿中断触发模式。在 `HAL_GPIO_EXTI_Callback` 中断回调中实时采集物理引脚电平，非阻塞地存入影子缓存变量。
- **软件接口**：[PW_Input_GetLevel(void)](../Core/Src/gpio.c#L35) 提供应用层无阻塞状态获取。

### 2.2 VOC (DAC 模拟输出)
- **引脚通道**：`GPIOA_PIN_5` (对应 DAC1_OUT2)。
- **驱动接口**：[DAC_Control_SetVocVoltage(float voltage_V)](../Core/Src/dac_control.c#L180) 接受物理电压值，限幅在 `[0.0V, 2.5V]`，并通过因子计算公式输出：
  $$\text{DAC寄存器值} = \text{物理电压}(V) \times \frac{4095}{2.5}$$

---

## 3. 三级优先级控制逻辑 (Control Priority Hierarchy)

在应用层控制主任务中，PA5 (VOC) 的输出控制权按以下优先级由高到低依次流转：

| 优先级 | 控制模式 | 触发判定条件 | VOC 物理行为 |
| :--- | :--- | :--- | :--- |
| **第一优先级 (最高)** | 内阻测量强拉模式 | `Calc_Control_IsVocForceActive() != 0` | 强制输出最大电压恒定为 `VOC_MAX_V` (2.5V)，忽略 PW 电平。 |
| **第二优先级 (中等)** | 安全跳闸保护模式 | `Output_Protection_GetState() != NORMAL` | 强制输出最小电压恒定为 `VOC_MIN_V` (0.0V)，忽略 PW 电平。 |
| **第三优先级 (最低)** | PW 斜坡跟随模式 | 系统正常且内阻状态机处于闲置阶段 | 以 5ms 定时周期读取 PW (PB0) 的引脚电平，计算并输出斜坡电压。 |

---

## 4. 斜坡跟随算法设计 (Ramp Follower Algorithm)

处于最低优先级（正常跟随）时，VOC 绝不进行电压的瞬时跳变，而是采用步进累加器实现平滑过渡：

### 4.1 算法核心常数
| 常数名称 | 物理意义 | 默认值 | 约束单位 |
| :--- | :--- | :--- | :--- |
| `VOC_MAX_V` | VOC 输出上限 | 2.5 | V |
| `VOC_MIN_V` | VOC 输出下限 | 0.0 | V |
| `VOC_RAMP_STEP_V` | 单次调节电压步长 | 0.0025 | V |
| `VOC_RAMP_INTERVAL_MS` | 调节执行周期时间窗 | 5 | ms |

### 4.2 状态转移公式
在满足时间周期约束（$\Delta t \ge 5\text{ms}$）时，若 `PW_Input_GetLevel() == 1`（高电平），目标电压递增：
$$V_{\text{target}}(t) = \min(V_{\text{target}}(t-\Delta t) + 0.0025\text{V},\, 2.5\text{V})$$

若 `PW_Input_GetLevel() == 0`（低电平），目标电压递减：
$$V_{\text{target}}(t) = \max(V_{\text{target}}(t-\Delta t) - 0.0025\text{V},\, 0.0\text{V})$$

该设计能实现平缓的电平过渡（摆率约为 $0.5\text{V/ms}$），有效抑制突变信号引起的母线振荡与过载风险。

---

## 5. 与内阻状态机的冷启动联动

重新上电后，如果输出电流一直小于 $1.0\text{A}$，则整体控制流转满足以下时序逻辑：
1. **启动自检期**：由于上电有 2秒观察期，系统优先触发第二优先级强拉逻辑，VOC 恒定输出 $0.0\text{V}$。
2. **状态机自启动**：2 秒后系统转入 `NORMAL`，内阻状态机自动切换至阶跃电流设定状态。此时第一优先级接管，VOC 被强拉至 $2.5\text{V}$，忽略 PW。
3. **开路锁定退回**：状态机在 `WAIT_3A_STABLE` 阶段判定物理电流持续小于最小阈值限制（$1.0\text{A}$），系统判定为**开路异常**，状态机回退并卡在 `WAIT_SAFE`。
4. **PW 控制权接管**：退回后，第一、第二优先级失效，VOC 的控制权平滑过渡至第三优先级的斜坡跟随器。由此，VOC 电压的后续升降重新完全由 PW (PB0) 引脚的电平状态主导。

---

## 6. 回归校验测试

任何针对 PW-VOC 协同控制逻辑的修改，必须在测试前和测试后通过如下脚本校验，确认控制流转移关系符合状态规范：

```powershell
powershell -File tests/check_uart_gui_protocol.ps1
```
该脚本包含专门的 `check_voc_pw_ramp` 段落，用以验证控制层代码中 `Calc_Control_IsVocForceActive()`、`PW_Input_GetLevel()` 及 `DAC_Control_SetVocVoltage` 的条件覆盖与逻辑链闭环。
