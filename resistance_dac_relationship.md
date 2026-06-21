# 交流线路内阻与 DAC 限流给定的数学关系分析报告

本报告针对 PFC（功率因数校正）控制系统在计算出交流线路等效内阻 $R_{AcLine}$ 后，如何一步步转换为底层 DAC 限流给定的数理关系与逻辑链条进行详细总结。

---

## 一、 核心物理量与符号定义

| 符号 | 代码变量名 | 物理含义与解释 | 备注 |
| :--- | :--- | :--- | :--- |
| $R_{AcLine}$ | `R_AcLine_New` | 计算得到的交流输入供电线路等效内阻 | 单位：$\Omega$，有效区间 $[0.2, 15]$ |
| $I_{AC\_max}$ | `Iac_In_Max` | 根据内阻计算出的最大允许输入交流电流 | 根据系统常数 $I\_K$ 计算得出 |
| $I_{AC\_limit}$ | `Iac_In_Max_Limited` | 经过通道硬件配置限幅后的安全交流电流 | 双通道上限 20A，单通道上限 10A |
| $V_{AC\_high}$ | `Vac_In_Avg_TestHigh_Offset` | 高电流测试点（4A状态）对应的交流输入平均电压 | 已加 $R_{dis}$ 阻抗偏移量补偿 |
| $V_{DC\_high}$ | `Vdc_Pfc_TestHigh` | 高电流测试点对应的 PFC 输出直流电压 | 用于能量守恒换算 |
| $I_{DC\_limit}$ | `Idc_Pfc_MaxLimit` | PFC 直流输出侧的最大安全电流阈值 | 转换成直流侧的物理控制量 |
| $DAC_{given}$ | `Idc_Pfc_MaxLimit_Data` | 最终写入到 DAC 中的 12 位数字量限流值 | 对应 $0 \sim 10\text{A}$ 的直流侧输出电流 |

---

## 二、 数学关系与逻辑链条推导

从计算出的内阻 $R_{AcLine}$ 到最终下发至 DAC 的给定值，系统内部经历以下四个核心的数理步骤：

### 1. 从内阻到交流最大允许电流
系统基于安全发热功率限制（等效于利用一阶常数关系简化微控制器开方运算）计算安全交流电流：
$$I_{AC\_max} = \frac{I\_K}{R_{AcLine}} \quad (\text{其中宏定义 } I\_K = 20)$$

### 2. 交流电流的硬件硬限幅
根据接入的输入通道数，限制交流电流的最大物理上限，避免器件过载：
- **双通道输入**（Vac_In_Ch1 和 Vac_In_Ch2 均在线）：
  $$I_{AC\_limit} = \min(I_{AC\_max},\, 20.0\text{A})$$
- **单通道输入**（仅 Ch1 或 Ch2 在线）：
  $$I_{AC\_limit} = \min(I_{AC\_max},\, 10.0\text{A})$$

### 3. 从交流限制折算至直流侧限制
根据输入与输出的能量守恒（$P_{in} \approx P_{out}$），即 $V_{AC\_in} \times I_{AC\_in} \approx V_{DC\_out} \times I_{DC\_out}$，利用高电流测试点（TestHigh）的数据对电流限值进行比例折算：
$$I_{DC\_limit} = I_{AC\_limit} \times \frac{V_{AC\_high}}{V_{DC\_high}}$$

### 4. 从物理直流限流值转换为 DAC 数字量
控制芯片内置 12 位 DAC，其量程设计为：$0 \sim 4095$ 对应 $0 \sim 10\text{A}$ 直流电流。
- 若 $I_{DC\_limit} > 10\text{A}$（饱和超限）：
  $$DAC_{given} = 4095 \quad (\text{即 } 0\text{xFFF})$$
- 若 $I_{DC\_limit} \leq 10\text{A}$（线性范围）：
  $$DAC_{given} = \text{round}\left( \frac{I_{DC\_limit} \times 4095}{10} \right)$$

---

## 三、 内阻与 DAC 给定的直接代数关系

当系统工作在**未触发硬件限幅**（即 $I_{AC\_max} < I_{limit\_gate}$）且**未超出 DAC 最大量程**的线性工作区间内，我们将上述步骤 1、3、4 联立：

$$DAC_{given} = \left( \frac{I\_K}{R_{AcLine}} \times \frac{V_{AC\_high}}{V_{DC\_high}} \times \frac{4095}{10} \right)$$

将系统参数 $I\_K = 20$ 带入：

$$DAC_{given} = 819 \times \frac{V_{AC\_high}}{V_{DC\_high}} \times \frac{1}{R_{AcLine}}$$

或者写为以 $R_{AcLine}$ 为自变量的函数：
$$DAC_{given}(R_{AcLine}) = \frac{C}{R_{AcLine}}$$
其中，比例系数 $C$ 是在当前探测周期内由电网输入电压与 PFC 输出电压决定的系统常数：
$$C = 819 \times \frac{V_{AC\_high}}{V_{DC\_high}}$$

---

## 四、 核心关系总结

```mermaid
graph LR
    R[交流线路内阻 R_AcLine] -- 反比例递减 --> I_ac[交流最大电流 Iac_In_Max]
    I_ac -- 硬件限幅滤波 --> I_limit[限幅交流电流 Iac_In_Max_Limited]
    I_limit -- 电压变换比折算 --> I_dc[直流最大电流 Idc_Pfc_MaxLimit]
    I_dc -- 12位DAC线性量化 --> DAC[DAC给定值 DAC_given]
```

1. **反比例控制规律**：在正常线性区内，**DAC 写入值与交流线路等效内阻 $R_{AcLine}$ 呈严格的反比例函数关系**。线阻越大，允许的最大充电功率和电流越小，下发的 DAC 限流限值按 $\frac{1}{R}$ 规律下降。
2. **电压调幅效应**：反比例系数受到**变压比 $\frac{V_{AC\_high}}{V_{DC\_high}}$** 的实时调制。在相同内阻下，交流输入电压越高，折算出的直流允许电流越大，下发的 DAC 给定值相应越高。
3. **分段约束（分段函数特性）**：
   - **安全限幅区（小内阻状态）**：当线路情况极好、内阻极小（如双通道下 $R_{AcLine} \leq 1.0\,\Omega$，单通道下 $R_{AcLine} \leq 2.0\,\Omega$）时，交流最大电流会触及物理安全边界（20A 或 10A）。此时，DAC 给定值不再随着内阻的降低而无限制增加，而是在上限饱和。
   - **动态限幅降额区（大内阻状态）**：当内阻增大（如老旧电线、接头老化发热等导致电阻变大）时，反比例公式占主导，DAC 值迅速衰减，降低充电器功率，起到精准防热灾、保护电网安全的作用。
