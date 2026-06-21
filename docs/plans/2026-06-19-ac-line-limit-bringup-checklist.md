<a id="ac-line-limit-bringup-checklist"></a>
# AC Line Limit Bring-Up Checklist

**适用范围：** 适用于当前 `FreeRTOS_PSA` 固件中“基于交流线路等效线阻动态限制系统充电功率”功能的上板联调、现场验收和故障复盘。

**命名约定：**
- `VIN1`：对应固件 `[Measure]` 帧中的 `V1`，物理来源为 `V1_IN`（注：物理上为交流输入通道 1 整流后的直流侧电压，非交流 RMS 直通值。如交流 220V 输入时，整流后直流典型值约为 311V，请勿混淆）
- `VIN2`：对应固件 `[Measure]` 帧中的 `V2`，物理来源为 `V2_IN`（注：物理上为交流输入通道 2 整流后的直流侧电压，如交流 220V 输入时，整流后直流典型值约为 311V，请勿混淆）
- `VOC`：本检查单中按当前固件 `VO_OUT` 直流输出电压理解，即 `[Measure]` 帧中的 `VO`
- `IOC`：当前闭环限流目标观测值，即 `[Calc]` 帧中的 `IOC`
- `CO`：直流输出电流 `CO_OUT`，即 `[Measure]` 帧中的 `CO`


**目标：** 在不破坏保护状态机、九段测量状态机和串口协议的前提下，确认交流线阻闭环限功率链路在真实硬件上满足“能测、能算、能限、能回退、能观测”五项要求。

---

## 1. 联调前准备

### 1.1 设备清单

- STM32G4 目标板 1 套
- 可调交流输入源或两路可控交流输入工装
- 可调直流负载或受控功率级
- 四通道及以上示波器
- 差分高压探头至少 2 支
- 电流探头至少 1 支
- USB 转串口链路或板载 CDC UART
- 上位机 GUI 或串口终端

### 1.2 固件与回归前置确认

上板前必须先在宿主机确认以下命令通过：

```powershell
powershell -File tests/check_adc_config.ps1
powershell -File tests/check_uart_gui_protocol.ps1
```

### 1.3 示波器建议接线

| 通道 | 建议观察点 | 目的 |
|---|---|---|
| CH1 | `VIN1` 前级采样点或对应高压分压输出 | 观察交流输入 1 是否在线、幅值是否稳定 |
| CH2 | `VIN2` 前级采样点或对应高压分压输出 | 观察交流输入 2 是否在线、双通道平均是否合理 |
| CH3 | `VOC` / `VO_OUT` 直流母线电压 | 观察 3A/2A 阶跃前后母线压降 |
| CH4 | `IOC` / PA4 `DAC1_OUT1` | 观察 3A、2A、闭环限流接管时 DAC 轨迹 |
| 可选 CH5 | `CO_OUT` 电流探头 | 观察 3A 判稳、2A 观察窗和闭环接管后的电流响应 |
| 可选 CH6 | `OF_EN` 数字使能脚 | 观察保护链与输出使能时序 |

---

## 2. 关键参数记录表

每次上板联调建议至少记录下表参数。若现场有多工况，建议按“单通道 1 / 单通道 2 / 双通道 / 重测回退 / 保护打断”分别建表。

| 参数 | 单位 | 来源 | 记录要求 |
|---|---:|---|---|
| `VIN1` | V | `[Measure] V1` / 示波器 CH1 | 记录稳态均值、峰峰值、是否 > 50V |
| `VIN2` | V | `[Measure] V2` / 示波器 CH2 | 记录稳态均值、峰峰值、是否 > 50V |
| `VOC` | V | `[Measure] VO` / 示波器 CH3 | 记录 3A 锁存点、2A 锁存点、闭环稳态值 |
| `CO` | A | `[Measure] CO` / 电流探头 | 记录 3A 判稳时电流、2A 判稳时电流、闭环接管后电流 |
| `IOC` | A | `[Calc] IOC` | 记录闭环接管目标值 |
| `R` | Ohm | `[Calc] R` | 记录本次测得线阻 |
| `Iac_limit` | A | `[LineLimit] Iac` | 记录交流侧限流值 |
| `Idc_limit` | A | `[LineLimit] Idc` | 记录直流折算限流值 |
| `DAC_code` | code | `[LineLimit] DAC` | 记录 DAC 量化结果 |
| `Retest` | 0/1 | `[LineLimit] Retest` | 记录本次是否要求重测 |
| `OF_EN` 状态 | bool | `[State] OF_ENABLED/OF_DISABLED` | 记录输出使能状态 |
| `Calc State` | text | `[State]` | 记录状态帧顺序是否完整 |

---

## 3. VIN1 / VIN2 / VOC / IOC 应提供的参数

### 3.1 VIN1 应提供的参数

- 实时电压值，单位 V
- 是否在线判定结果：`VIN1 > 50.0V`
- 波动范围或峰峰值
- 在双通道场景中的参与方式：平均值的一部分
- 对应测量时刻：
  - 3A 判稳阶段
  - 2A 观察阶段
  - 闭环接管瞬间

### 3.2 VIN2 应提供的参数

- 实时电压值，单位 V
- 是否在线判定结果：`VIN2 > 50.0V`
- 波动范围或峰峰值
- 在双通道场景中的参与方式：平均值的一部分
- 对应测量时刻：
  - 3A 判稳阶段
  - 2A 观察阶段
  - 闭环接管瞬间

### 3.3 VOC 应提供的参数

- `VO_OUT` 直流母线电压实时值，单位 V
- 3A 锁存点 `U1`
- 2A 锁存点 `U2`
- 2A 观察窗内的压降曲线
- 闭环接管后稳态值
- 是否低于直流有效电压阈值 `10.0V`

### 3.4 IOC 应提供的参数

- 当前闭环目标限流值，单位 A
- 计算完成后的首个有效值
- `MONITOR` 状态下保持值
- 与 `Idc_limit` 是否一致
- 与 `DAC_code` 是否满足线性量化关系

### 3.5 典型联调假设参数与理论计算匹配示例

为方便联调时使用外部电源与电子负载模拟输出电压 $VOC$ 和电流 $CO$ 并与理论值匹配校验，推荐使用以下两组典型假设参数进行比对：

#### 示例 1：线阻 $R = 2.0\ \Omega$（非饱和限流工况）
* **外部提供及模拟测量输入：**
  * `VIN1`（整流直流）：$311.0\text{ V}$（单通道 1 在线，通道 2 在线检测为 $0.0\text{ V}$）
  * 3A 锁存值：$I_1 = 3.0\text{ A}$，$U_1 (VOC) = 400.0\text{ V}$
  * 2A 锁存值：$I_2 = 2.0\text{ A}$，$U_2 (VOC) = 398.0\text{ V}$
* **理论计算结果：**
  * 线阻阻值：$R = \frac{400.0 - 398.0}{3.0 - 2.0} = 2.0\ \Omega$ (校验通过，波动在限制内)
  * 理论交流最大电流：$Iac\_theoretical = \frac{20.0}{2.0} = 10.0\text{ A}$
  * 交流侧限幅（单通道）：$Iac\_limited = \min(10.0\text{ A}, 10.0\text{ A}) = 10.0\text{ A}$
  * 直流折算限流值：$Idc\_limit = 10.0\text{ A} \times \frac{311.0\text{ V}}{400.0\text{ V}} = 7.775\text{ A}$
  * 直流限流目标 `IOC`：$7.775\text{ A}$
  * DAC 码值：$DAC\_code = \text{round}(7.775\text{ A} \times \frac{4095}{10.0\text{ A}}) = 3184$
* **串口输出期望：** `[Calc] R:2.000R U1:400.00V I1:3.00A U2:398.00V I2:2.00A IOC:7.78A`

#### 示例 2：线阻 $R = 1.0\ \Omega$（触发双通道硬件上限饱和工况）
* **外部提供及模拟测量输入：**
  * `VIN1` / `VIN2`（整流直流）：$311.0\text{ V}$ / $311.0\text{ V}$（双通道在线，平均输入 $Vac\_sample = 311.0\text{ V}$）
  * 3A 锁存值：$I_1 = 3.0\text{ A}$，$U_1 (VOC) = 400.0\text{ V}$
  * 2A 锁存值：$I_2 = 2.0\text{ A}$，$U_2 (VOC) = 399.0\text{ V}$
* **理论计算结果：**
  * 线阻阻值：$R = \frac{400.0 - 399.0}{3.0 - 2.0} = 1.0\ \Omega$ (校验通过)
  * 理论交流最大电流：$Iac\_theoretical = \frac{20.0}{1.0} = 20.0\text{ A}$
  * 交流侧限幅（双通道）：$Iac\_limited = \min(20.0\text{ A}, 20.0\text{ A}) = 20.0\text{ A}$
  * 直流折算限流值：$Idc\_limit = 20.0\text{ A} \times \frac{311.0\text{ V}}{400.0\text{ V}} = 15.55\text{ A}$
  * 直流限流目标 `IOC`：$10.0\text{ A}$（直流侧满量程限幅为 $10.0\text{ A}$）
  * DAC 码值：$DAC\_code = 4095$（满量程饱和）
* **串口输出期望：** `[Calc] R:1.000R U1:400.00V I1:3.00A U2:399.00V I2:2.00A IOC:10.00A`

---


## 4. 串口观察点与期望状态帧顺序

### 4.1 上电空载正常链路

**期望顺序：**

1. `[State] STATE:RECOVERY_WAIT`
2. `[State] STATE:OF_ENABLED`
3. `[State] STATE:NORMAL`
4. `[State] STATE:WAIT_SAFE`
5. `[State] STATE:SET_3A`
6. `[State] STATE:WAIT_3A_STABLE`
7. 多条 `[Stable] ...`
8. `[State] STATE:LATCH_3A`
9. `[State] STATE:SET_2A`
10. `[State] STATE:WAIT_2A_STABLE_10S`
11. `[State] STATE:LATCH_2A`
12. `[State] STATE:CALC_RESISTANCE`
13. `[Calc] R:... U1:... I1:... U2:... I2:... IOC:...`
14. `[LineLimit] Iac:... Idc:... DAC:... Retest:0`
15. `[State] STATE:MONITOR`

### 4.2 重测回退链路

当满足以下任一条件时，期望出现重测链路：

- `R < 0.2Ω`
- `R > 15Ω`
- `|R_Calc - R_Last| > 0.5Ω`
- `VIN1 <= 50V` 且 `VIN2 <= 50V`
- `VOC < 10V`

**期望顺序：**

1. `[State] STATE:CALC_RESISTANCE`
2. `[Calc] ...`
3. `[LineLimit] Iac:... Idc:... DAC:... Retest:1`
4. `[State] STATE:NEED_RETEST`
5. `[State] STATE:WAIT_SAFE`
6. 条件恢复后重新进入 `SET_3A`

### 4.3 开路异常链路

**期望顺序：**

1. `[State] STATE:WAIT_3A_STABLE`
2. 多条 `[Stable] ...`
3. `[Error] OPEN_CIRCUIT_DETECTED`
4. `[State] STATE:WAIT_SAFE`

### 4.4 2A 挂起/恢复链路

**期望顺序：**

1. `[State] STATE:WAIT_2A_STABLE_10S`
2. 发送 `DebugPause2A`
3. `[State] STATE:WAIT_2A_PAUSED`
4. 发送 `DebugResume2A`
5. `[State] STATE:WAIT_2A_STABLE_10S`
6. 满足 10s 等待后继续进入 `LATCH_2A`

---

## 5. 示波观察点与期望波形

### 5.1 CH1 `VIN1`

**观察目标：**
- 单通道联调时确认 `VIN1 > 50V`
- 双通道联调时确认与 `VIN2` 同步存在
- 确认不会在 3A / 2A 阶跃时发生异常塌陷

**期望波形：**
- 正常在线时保持稳定交流包络
- 幅值大于在线阈值
- 双通道场景下与 `VIN2` 幅值接近，允许存在线路差异

### 5.2 CH2 `VIN2`

**观察目标：**
- 单通道 2 联调时确认 `VIN2 > 50V`
- 双通道联调时确认参与平均值

**期望波形：**
- 正常在线时保持稳定交流包络
- 当故意模拟单通道掉线时，该通道应跌到阈值以下，串口应反映单通道限幅

### 5.3 CH3 `VOC / VO_OUT`

**观察目标：**
- 3A 锁存点 `U1`
- 2A 锁存点 `U2`
- 10s 观察窗内母线回落过程

**期望波形：**
- `SET_3A` 后 `VOC` 稳定在较高负载点
- `SET_2A` 后 `VOC` 出现回升或压降变化
- `LATCH_2A` 前后波形已进入缓慢变化区，便于锁存

### 5.4 CH4 `IOC / PA4 DAC`

**观察目标：**
- 3A 阶跃输出
- 2A 阶跃输出
- 计算完成后闭环接管值

**期望波形：**
- 上电观察期内保持 0A 对应电平
- `SET_3A` 时出现第一次阶跃
- `SET_2A` 时下降到 `I1 - 1A`，并限制在 `[0.1A, 10A]`
- `CALC_RESISTANCE` 成功后切到最终 `IOC` 对应电平
- `MONITOR` 状态下保持不再重复改写

### 5.5 电流探头 `CO_OUT`

**观察目标：**
- 3A 判稳是否真实达到连续稳定
- 2A 观察窗内电流是否进入缓慢变化区
- 闭环接管后电流是否向 `IOC` 收敛

**期望波形：**
- `SET_3A` 后上升并逐步稳定
- `WAIT_3A_STABLE` 期间，100ms 相邻采样差值最终小于 500mA
- `SET_2A` 后下降到第二个平台
- 闭环接管后收敛到 `IOC` 附近

---

## 6. 推荐联调工况

### 工况 A：单通道 1 在线

- `VIN1`：311VDC 级（对应交流 220V 输入整流后的直流侧电压，非交流 RMS）
- `VIN2`：0V 或明显低于 50V
- 期望：
  - `online_channel_count = 1`
  - `Iac_limit <= 10A`
  - `IOC = 10A * VIN1 / VOC` 或更低

### 工况 B：单通道 2 在线

- `VIN1`：0V 或明显低于 50V
- `VIN2`：311VDC 级（对应交流 220V 输入整流后的直流侧电压，非交流 RMS）
- 期望：
  - `online_channel_count = 1`
  - `Vac_sample = VIN2`
  - `Iac_limit <= 10A`

### 工况 C：双通道在线

- `VIN1`：311VDC 级（对应交流 220V 输入整流后的直流侧电压，非交流 RMS）
- `VIN2`：311VDC 级（对应交流 220V 输入整流后的直流侧电压，非交流 RMS）
- 期望：
  - `online_channel_count = 2`
  - `Vac_sample = (VIN1 + VIN2) / 2`
  - `Iac_limit <= 20A`

### 工况 D：无通道在线

- `VIN1 < 50V`
- `VIN2 < 50V`
- 期望：
  - `Retest = 1`
  - 状态机回到 `WAIT_SAFE`

### 工况 E：`VOC` 过低

- `VOC < 10V`
- 期望：
  - `Retest = 1`
  - `IOC` 清零
  - 状态机回到 `WAIT_SAFE`

---

## 7. 现场验收判据

满足以下全部条件，才可判定本次上板联调通过：

1. 上电后保护状态、输出使能和计算状态帧顺序与预期一致。
2. `SET_3A -> WAIT_3A_STABLE -> LATCH_3A -> SET_2A -> WAIT_2A_STABLE_10S -> LATCH_2A -> CALC_RESISTANCE -> MONITOR` 全链路可重复观察。
3. 双通道在线时，`Vac_sample` 实际按 `VIN1` 与 `VIN2` 平均值参与折算。
4. 单通道在线时，`Iac_limit` 不超过 `10A`；双通道在线时不超过 `20A`。
5. `IOC` 与 `[LineLimit] Idc` 一致，且与 `DAC_code` 的量化关系匹配。
6. `Retest = 1` 时，状态机必须退回 `WAIT_SAFE`，而不是停留在 `MONITOR`。
7. 开路、无交流输入、低 `VOC`、异常线阻等故障分支都能观测到明确回退。

---

## 8. 联调记录模板

| 工况 | VIN1 | VIN2 | VOC(U1/U2/稳态) | CO(I1/I2/稳态) | R | Iac | Idc | IOC | DAC | Retest | 结论 |
|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---|
| 单通道 1 |  |  |  |  |  |  |  |  |  |  |  |
| 单通道 2 |  |  |  |  |  |  |  |  |  |  |  |
| 双通道 |  |  |  |  |  |  |  |  |  |  |  |
| 无通道在线 |  |  |  |  |  |  |  |  |  |  |  |
| 低 VOC |  |  |  |  |  |  |  |  |  |  |  |
| 开路异常 |  |  |  |  |  |  |  |  |  |  |  |
