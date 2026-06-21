<a id="logic-line-limit"></a>
# LOGIC_line-limit — 交流线阻闭环限功率算法与安全链校验说明书

---

## 1. 核心设计目的

为防止充电功率超出动力母线与物理供电通道的安全承载上限，系统在 `line_limit.c` 中实现了一套独立的**交流线阻闭环限功率算法（AC Line Limit Algorithm）**。
该算法通过分析计算得到的线路等效直流电阻（DCR），结合实时采样的交流及直流电压物理量，执行 5 阶段的闭环校验和功率转换，自动推导出直流侧最大允许限流值与对应的 DAC 输出码，从而在底层固件层面实现闭环安全限制。

本说明书作为交流线阻限功率算法和安全链逻辑的唯一真理源，描述其数据格式、公式细节及与内阻状态机之间的控制接口联动。

---

## 2. 核心数据结构与配置参数

为了实现纯计算、无外部依赖和低耦合的算法设计，新算法所有的配置项与内部状态均通过结构体参数显式传递，绝不污染全局命名空间。

### 2.1 物理配置结构体 (`LineLimit_Config_t`)

物理常数和安全阈值通过 `LineLimit_GetDefaultConfig()` 进行标准化初始化。实际固件代码中固化的物理参数值如下：

| 配置字段 | 物理量符号 | 默认固化值 | 物理单位 | 详细说明 |
|---|---|---|---|---|
| `ik_const` | $I_k$ | `20.0f` | A·Ω | 理论交流电流计算常数系数 |
| `efficiency` | $\eta$ | `0.98f` | - | 交流到直流的系统转换效率 (98.0%) |
| `r_min_ohm` | $R_{min}$ | `0.2f` | Ω | 阻抗合理性区间下限值 |
| `r_max_ohm` | $R_{max}$ | `15.0f` | Ω | 阻抗合理性区间上限值 |
| `r_delta_max_ohm` | $\Delta R_{max}$ | `0.5f` | Ω | 相邻两次成功锁定阻抗的容许最大波动差值 |
| `single_ch_max_a` | $I_{single\_max}$ | `10.0f` | A | 单通道在线时的最大硬件保护电流上限值 |
| `dual_ch_max_a` | $I_{dual\_max}$ | `15.0f` | A | 双通道在线时的最大硬件保护电流上限值 |
| `ac_online_threshold_v`| $V_{online\_th}$ | `50.0f` | V | 交流输入通道判定为在线（Active）的有效电压阈值 |
| `min_valid_vac_v` | $V_{ac\_min}$ | `50.0f` | V | 闭环限功率要求交流有效采样电压的最低下限值 |
| `min_valid_vdc_v` | $V_{dc\_min}$ | `10.0f` | V | 闭环限功率要求直流有效采样电压的最低下限值 |
| `idc_fullscale_a` | $I_{dc\_fs}$ | `15.0f` | A | 直流输出电流满量程物理代表值 |
| `dac_fullscale_code` | $DAC_{fs}$ | `4095U` | LSB | 物理 DAC 的满量程控制数字量 (12位) |

### 2.2 算法状态结构体 (`LineLimit_State_t`)

该结构体既负责携带外部测量输入，又负责将算法的 5 个阶段中间计算结果导出，便于上位机捕获以及回归测试脚本读取。

```c
typedef struct
{
    float r_calc_ohm;              /* 输入：当前内阻计算得到的阻值 */
    float r_last_locked_ohm;       /* 输入：上一次成功闭环锁定的物理阻值 */
    uint8_t r_last_valid;          /* 输入：上次锁定阻值是否有效（首次测量为 0，跳过波动校验） */
    float vac_ch1_sample_v;        /* 输入：交流通道 1 (V1In) 实时采样还原物理电压 */
    float vac_ch2_sample_v;        /* 输入：交流通道 2 (V2In) 实时采样还原物理电压 */
    float vdc_sample_v;            /* 输入：直流输出 (VoOut) 采样物理电压 */

    uint8_t online_channel_count;  /* 输出：在线通道统计计数 (0 ~ 2) */
    float r_new_locked_ohm;        /* 输出：校验通过后新锁定的安全线阻阻值 */
    float iac_theoretical_a;       /* 输出：无硬件限制时的理论交流最大电流 */
    float iac_limited_a;           /* 输出：受限于通道的最大交流电流 */
    float vac_sample_v;            /* 输出：参与限功率计算的折算等效交流电压值 */
    float input_power_w;           /* 输出：估算最大输入交流功率 */
    float output_power_w;          /* 输出：估算最大直流输出功率 */
    float idc_max_limit_a;         /* 输出：经功率折算及通道限幅后的直流侧最大电流 */
    uint16_t dac_code;             /* 输出：最终量化出的物理 DAC 控制数字量 */
} LineLimit_State_t;
```

---

## 3. 算法 5 阶段校验与计算详细步骤

当执行核心处理入口 `Process_AcLine_Limit(cfg, state)` 时，算法依次通过以下 5 个独立的安全逻辑阶段：

### 阶段 1：阻抗范围和波动校验

算法首先读取当前由九段状态机计算出来的候选阻抗 $R_{calc}$：
1. **范围越限判定**：如果 $R_{calc} < R_{min}$ 或 $R_{calc} > R_{max}$，算法立刻中止并返回错误码 `LINE_LIMIT_STATUS_NEED_RETEST`。
2. **阻抗阶跃波动判定**：只有当 `state->r_last_valid` 不为 0 且上一次锁定阻值 $R_{last\_locked} > 0.0\text{V}$ 时，才激活波动校验。计算相邻两次阻值的绝对差值：
   $$\Delta R = |R_{calc} - R_{last\_locked}|$$
   如果 $\Delta R > \Delta R_{max}$（即单次内阻变化超出 $0.5\Omega$），算法立刻中止并返回错误码 `LINE_LIMIT_STATUS_NEED_RETEST`。
3. **阻值闭环锁定**：如果上述范围与阶跃校验均通过，则更新锁定阻值：
   $$R_{new\_locked} = R_{calc}$$

### 阶段 2：理论交流最大电流计算

利用通过合理性校验的新锁定阻抗 $R_{new\_locked}$，计算无硬件约束时交流侧线路的热平衡电流能力：
- 若 $R_{new\_locked} \le 0.0\Omega$（双重防卫校验），返回 `LINE_LIMIT_STATUS_NEED_RETEST`。
- 理论交流电流计算公式：
  $$I_{ac\_theoretical} = \frac{I_k}{R_{new\_locked}}$$

### 阶段 3：单/双通道在线统计

系统根据交流输入采样幅值判定物理通道是否在线：
- **通道 1 在线条件**：当 $V_{ac\_ch1} > V_{online\_th}$ ($50.0\text{V}$) 时，判定在线 ($ch_1 = 1$)；否则判定离线 ($ch_1 = 0$)。
- **通道 2 在线条件**：当 $V_{ac\_ch2} > V_{online\_th}$ ($50.0\text{V}$) 时，判定在线 ($ch_2 = 1$)；否则判定离线 ($ch_2 = 0$)。
- **总在线通道数**：
  $$N_{online} = ch_1 + ch_2$$
- **离线安全阻断**：若 $N_{online} == 0$，表明交流侧完全无电压供给或检测线路开路，算法立刻中止并返回 `LINE_LIMIT_STATUS_NEED_RETEST`。
- **受限交流电流初始化**：固件代码中，初始化受限物理交流电流 $I_{ac\_limited}$ 继承理论交流最大电流值：
  $$I_{ac\_limited} = I_{ac\_theoretical}$$
  > [!NOTE]
  > 在当前版本的 C 代码实现中，交流通道的物理限流限幅并没有直接作用在 $I_{ac\_limited}$ 上，而是将通道最大限制直接应用在直流侧物理电流 $I_{dc\_max\_limit}$ 上。

### 阶段 4：交直流功率折算

1. **等效交流采样电压折算**：
   - 若 $N_{online} == 2$（双通道均在线），采用平均值折算：
     $$V_{ac\_sample} = \frac{V_{ac\_ch1} + V_{ac\_ch2}}{2}$$
   - 若仅单通道在线，则 $V_{ac\_sample}$ 取处于在线状态的那一路交流通道采样电压（$V_{ac\_ch1}$ 或 $V_{ac\_ch2}$）。
2. **极低压安全限幅**：
   - 若 $V_{ac\_sample} < V_{ac\_min}$ ($50.0\text{V}$) 或直流采样电压 $V_{dc\_sample} < V_{dc\_min}$ ($10.0\text{V}$)，为防止除数为零或极低电压引发控制环震荡，算法中止并返回 `LINE_LIMIT_STATUS_NEED_RETEST`。
3. **输入功率与输出功率换算**：
   - 估算最大输入功率：
     $$P_{in} = I_{ac\_theoretical} \times V_{ac\_sample}$$
   - 物理输出功率折算（应用效率因子）：
     $$P_{out} = P_{in} \times \eta$$
4. **直流电流物理极限换算**：
   - 换算理论直流输出电流：
     $$I_{dc} = \frac{P_{out}}{V_{dc\_sample}}$$
5. **通道硬件过载保护**：根据当前的在线通道数选取对应的硬件物理能力上限值 $I_{hw\_limit}$：
   - 若 $N_{online} == 2$，则 $I_{hw\_limit} = I_{dual\_max}$ ($15.0\text{A}$)。
   - 若 $N_{online} == 1$，则 $I_{hw\_limit} = I_{single\_max}$ ($10.0\text{A}$)。
   - **最终输出直流电流物理限值**：
     $$I_{dc\_max\_limit} = \min(I_{dc}, I_{hw\_limit})$$

### 阶段 5：DAC 线性量化与饱和保护

将得到的物理直流限制电流 $I_{dc\_max\_limit}$ 量化映射为 12 位 DAC 的控制码值：
- **上饱和防护**：若 $I_{dc\_max\_limit} \ge I_{dc\_fs}$ ($15.0\text{A}$)，则直接饱和输出最大 DAC 码值：
  $$DAC_{code} = DAC_{fs} \quad (4095)$$
- **线性量化换算**（含四舍五入偏移量）：
  $$DAC_{code} = \left\lfloor \frac{I_{dc\_max\_limit} \times DAC_{fs}}{I_{dc\_fs}} + 0.5 \right\rfloor$$

---

## 4. 与内阻计算状态机（FSM）的闭环联动控制

交流线阻限功率算法并非独立存在，它是内阻九段计算状态机的后处理阶段，承担了计算完成到限制执行的“交接关卡”职责。

### 4.1 调用与闭环交接流程

在内阻状态机处于 `CALC_CONTROL_CALC_RESISTANCE` 状态分支下，系统在执行完直流内阻测量计算后，立刻启动安全链校验：

```
                    九段状态机计算得到 R_calc
                              │
                              ▼
                      构造 LineLimit 状态
                              │
                              ▼
                 Process_AcLine_Limit(cfg, state)
                              │
               ┌──────────────┴──────────────┐
     [LINE_LIMIT_STATUS_OK]       [LINE_LIMIT_STATUS_NEED_RETEST]
               │                             │
               ▼                             ▼
   1. 锁定 R_last = R_calc         1. 丢弃本次内阻结果
   2. 限流标靶 ioc = Idc_limit     2. 安全链输出 ioc = 0.0A
   3. 下发电流 set_current = ioc   3. 清空上次锁定与有效标志
   4. 状态机跳转 -> MONITOR       4. 标记 need_retest = 1
                                   5. 状态机回退 -> WAIT_SAFE
```

### 4.2 异常拒绝与恢复原语

- 当 `Process_AcLine_Limit()` 校验失败返回非 OK 码时，固件会清零内部锁定值及有效状态（`s_r_last_valid = 0`，`s_ioc_valid = 0`），并将实际的限流设定值置为 `0.0A`，防止发生错误的限流设定。
- 回退到 `CALC_CONTROL_WAIT_SAFE` 状态后，系统将释放 CPU。若保护状态机依旧处于安全条件（`safe_allowed == true`），且无开路异常锁定，状态机将自动重新启动 3A/2A 探测过程，进行一轮新的测量，实现闭环闭锁自愈。

---

## 5. 接口取值通道与物理引脚绑定

为保证计算数据的真实可靠，输入参数与以下物理测量通道底层接口强绑定：

- `vac_ch1_sample_v` 绑定交流通道 1 输入，底层数据提取源：`Measure_GetV1In()` (物理引脚：`PA0`)。
- `vac_ch2_sample_v` 绑定交流通道 2 输入，底层数据提取源：`Measure_GetV2In()` (物理引脚：`PA1`)。
- `vdc_sample_v` 采用 3A/2A 两阶段锁存直流电压的算术平均值，提取源：`(s_u1 + s_u2) * 0.5f`。
- `vdc_sample_v` 底层对应的实时物理量为：`Measure_GetVoOut()` (物理引脚：`PA3`)。
- 最终 DAC 量化输出码 `dac_code` 经保护状态机确认后，直接写入 DAC1 的通道 1 保持寄存器，用以控制物理限流引脚 (物理引脚：`PA4`)。
