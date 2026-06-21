<a id="ac-line-limit-implementation-plan"></a>
# AC Line Limit Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 在现有内阻九段测量状态机基础上，实现一个“基于交流线路内阻（等效线阻）动态限制系统充电功率”的闭环安全控制函数，并以最小改动接入当前固件输出链路。

**Architecture:** 保持 `calc_control.c` 对九段测量时序、锁存与状态发布的所有权不变；将新的交流线阻限功率算法下沉为一个独立纯计算模块，通过结构体传递输入、配置和输出，避免文件级全局变量污染。算法模块仅输出状态码、物理限流值和 DAC 量化结果，最终仍通过 `Output_Control_SetCurrent()` 进入现有安全门控与 DAC 控制链路。

**Tech Stack:** C (STM32G4xx HAL / FreeRTOS / CMSIS-OS V2), Python behavior harness, PowerShell validation scripts.

---

## 1. 问题具体描述

当前固件在 [`calc_control.c`](../../Core/Src/calc_control.c) 中已经具备内阻九段测量与闭环接管能力：系统先通过 3A / 2A 阶跃锁存 `U1/I1/U2/I2`，再由 `R = (U1 - U2) / (I1 - I2)` 计算线路等效阻抗，并在 `CALC_CONTROL_CALC_RESISTANCE` 阶段生成一个简化的 `IOC = K / R` 目标值。该实现能完成基础闭环交接，但它仍存在三个工程缺口。

第一，现有闭环目标值直接把 `K / R` 视作直流侧限流值，没有把“交流侧热约束”“单/双通道额定限制”“交流到直流的功率折算”和“DAC 量化”完整串起来，物理意义不够闭环。第二，当前实现未显式校验新计算阻抗与上次锁定阻抗之间的稳定性，也没有在 `Vac` / `Vdc` 异常过低时主动拒绝本次结果并要求重新探测。第三，现有代码把闭环交接逻辑直接揉在 `calc_control.c` 的状态分支里，不利于后续复用，也不符合本次“通过结构体传参、避免全局变量污染”的移植要求。

因此，本次开发目标不是重写九段测量状态机，而是用一个新的纯算法模块替换当前简化的 `IOC = K / R` 逻辑，构建如下 5 阶段安全链路：阻抗合理性与稳定性校验、理论交流最大电流计算、基于在线通道数的硬件限幅、交流功率到直流侧电流的折算，以及 DAC 数字量线性映射与饱和保护。根据本轮确认，双通道在线时 `Vac_Sample` 取两路平均值；当交流采样电压大于 `50.0V` 时认为该通道在线。

## 2. 设计约束与落地原则

为了和项目当前结构保持一致，本次实现必须遵守以下落地原则。

1. 不重写 [`calc_control.c`](../../Core/Src/calc_control.c) 现有九段状态机，不改变 `WAIT_SAFE -> MONITOR` 的阶段定义，不引入新的任务或新的长期运行状态。
2. 新算法必须作为独立模块存在，建议文件为 [`Core/Inc/line_limit.h`](../../Core/Inc) 与 [`Core/Src/line_limit.c`](../../Core/Src)，由 `Process_AcLine_Limit()` 作为唯一核心入口。
3. 新模块不得直接访问 HAL、GPIO、RTOS 线程标志或 UART；它只接收结构体输入并回写结构体输出。
4. 最终的直流限流值仍通过 [`Output_Control_SetCurrent()`](../../Core/Src/output_control.c) 下发，保留现有输出保护状态机和 DAC 控制模块的安全边界。
5. 所有新增常量必须先在配置结构体中定义默认值，避免把控制阈值散落在 `calc_control.c` 或 `app_freertos.c` 中。
6. 按项目 TDD 规则，必须先扩展 [`tests/check_state_machines_behavior.py`](../../tests/check_state_machines_behavior.py) 的宿主机模型和断言，再修改 C 实现。

## 3. 拟新增的数据结构与接口

建议新增以下状态码和结构体，使算法模块既能携带输入，又能保留阶段中间量供上层诊断和测试读取。

```c
typedef enum
{
    LINE_LIMIT_STATUS_OK = 0,
    LINE_LIMIT_STATUS_NEED_RETEST = 1,
    LINE_LIMIT_STATUS_INVALID_ARG = 2
} LineLimit_Status_t;

typedef struct
{
    float ik_const;
    float r_min_ohm;
    float r_max_ohm;
    float r_delta_max_ohm;
    float single_ch_max_a;
    float dual_ch_max_a;
    float ac_online_threshold_v;
    float min_valid_vac_v;
    float min_valid_vdc_v;
    float idc_fullscale_a;
    uint16_t dac_fullscale_code;
} LineLimit_Config_t;

typedef struct
{
    float r_calc_ohm;
    float r_last_locked_ohm;
    float vac_ch1_sample_v;
    float vac_ch2_sample_v;
    float vdc_sample_v;

    uint8_t online_channel_count;
    float r_new_locked_ohm;
    float iac_theoretical_a;
    float iac_limited_a;
    float vac_sample_v;
    float idc_max_limit_a;
    uint16_t dac_code;
} LineLimit_State_t;

LineLimit_Status_t Process_AcLine_Limit(const LineLimit_Config_t *cfg,
                                        LineLimit_State_t *state);
```

其中 `online_channel_count` 推荐由算法函数内部根据 `Vac > 50.0f` 自动统计，这样上层只需传入实时采样值，不需要重复拷贝同一套在线判定逻辑。`r_last_locked_ohm` 由 `calc_control.c` 维护为上一次成功闭环接管时的锁定阻值；`r_new_locked_ohm` 只在本次校验通过时更新。这样能把“是否允许本次结果接管”与“上一次成功结果是什么”干净分开。

## 4. 五阶段实现过程

### Task 1: 先在宿主机模型中锁定新闭环规则

**Files:**
- Modify: `tests/check_state_machines_behavior.py`
- Test: `tests/check_state_machines_behavior.py`

**Step 1: 写失败用例**

在现有 `CalcControlModel` 之外，增加一个与 C 版接口同构的 `LineLimitModel`，并补充如下断言：

```python
assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "out-of-range resistance forces retest")
assert_equal(status, LINE_LIMIT_STATUS_NEED_RETEST, "resistance jump above 0.5R forces retest")
assert_close(vac_sample_v, (vac1 + vac2) / 2.0, "dual-channel uses average Vac")
assert_close(iac_limited_a, 20.0, "dual-channel current caps at 20A")
assert_close(iac_limited_a, 10.0, "single-channel current caps at 10A")
assert_equal(dac_code, 4095, "Idc above 10A saturates DAC")
```

**Step 2: 运行测试确认失败**

Run: `python tests/check_state_machines_behavior.py`  
Expected: FAIL，因为宿主机模型尚未实现新的 5 阶段交流线阻限功率逻辑。

**Step 3: 用最小模型实现参考行为**

在同一测试文件中加入 `LineLimitModel.process()`，它只做 5 阶段数学推演，不接触状态机计时，作为后续 C 实现的宿主机真值参考。

**Step 4: 重新运行测试确认通过**

Run: `python tests/check_state_machines_behavior.py`  
Expected: PASS

### Task 2: 新增纯算法模块 `line_limit`

**Files:**
- Create: `Core/Inc/line_limit.h`
- Create: `Core/Src/line_limit.c`
- Test: `tests/check_state_machines_behavior.py`

**Step 1: 编写最小头文件**

在 `line_limit.h` 中定义状态码、配置结构体、状态结构体和 `Process_AcLine_Limit()` 声明，保持 `const` 指针、显式尺寸整数和 `float` 单精度约束。

**Step 2: 编写最小 C 实现**

在 `line_limit.c` 中按以下 5 个阶段实现：

1. 阻抗范围和波动校验  
   - 若 `r_calc_ohm < 0.2f` 或 `r_calc_ohm > 15.0f`，返回 `LINE_LIMIT_STATUS_NEED_RETEST`
   - 若 `fabsf(r_calc_ohm - r_last_locked_ohm) > 0.5f`，返回 `LINE_LIMIT_STATUS_NEED_RETEST`
   - 否则 `r_new_locked_ohm = r_calc_ohm`

2. 理论交流最大电流  
   - 若 `r_new_locked_ohm <= 0.0f`，返回 `LINE_LIMIT_STATUS_NEED_RETEST`
   - `iac_theoretical_a = ik_const / r_new_locked_ohm`

3. 单/双通道在线统计与硬件限幅  
   - `vac_ch1_sample_v > 50.0f` 记为 ch1 在线
   - `vac_ch2_sample_v > 50.0f` 记为 ch2 在线
   - `online_channel_count == 0` 时返回 `LINE_LIMIT_STATUS_NEED_RETEST`
   - 双在线取 `dual_ch_max_a = 20.0f`
   - 单在线取 `single_ch_max_a = 10.0f`
   - `iac_limited_a = min(iac_theoretical_a, hw_limit_a)`

4. 交流到直流折算  
   - 双在线：`vac_sample_v = (vac1 + vac2) * 0.5f`
   - 单在线：取在线那一路
   - 若 `vac_sample_v < min_valid_vac_v` 或 `vdc_sample_v < min_valid_vdc_v`，返回 `LINE_LIMIT_STATUS_NEED_RETEST`
   - `idc_max_limit_a = iac_limited_a * vac_sample_v / vdc_sample_v`

5. DAC 量化  
   - 若 `idc_max_limit_a >= 10.0f`，`dac_code = 4095`
   - 否则 `dac_code = (uint16_t)((idc_max_limit_a * 4095.0f / 10.0f) + 0.5f)`

**Step 3: 运行状态机与模型测试**

Run: `python tests/check_state_machines_behavior.py`  
Expected: PASS

### Task 3: 将新模块接入现有 `calc_control` 状态机

**Files:**
- Modify: `Core/Inc/calc_control.h`
- Modify: `Core/Src/calc_control.c`
- Modify: `Core/Src/app_freertos.c`
- Test: `tests/check_state_machines_behavior.py`

**Step 1: 扩展 `Calc_Control_Input_t`**

在 [`calc_control.h`](../../Core/Inc/calc_control.h) 中补充：

```c
float vac_ch1_v;
float vac_ch2_v;
float vdc_sample_v;
```

其中 `vdc_sample_v` 可以直接沿用当前 `Measure_GetVoOut()` 的值；`vac_ch1_v` / `vac_ch2_v` 分别来自 `Measure_GetV1In()` / `Measure_GetV2In()`。

**Step 2: 扩展 `Calc_Control_Output_t`**

在输出结构中增加这些诊断字段：

```c
float iac_limit_a;
float idc_limit_a;
uint16_t dac_code;
uint8_t need_retest;
```

`ioc` 字段继续保留，但其物理语义从“简化 K/R 目标值”改为“最终可下发的直流限流值”。

**Step 3: 替换 `CALC_CONTROL_CALC_RESISTANCE` 中的旧闭环公式**

当前代码中存在这类逻辑：

```c
ioc_target = CALC_CONTROL_IOC_GAIN_K / s_resistance;
```

本次改为：

1. 先按原有 `R = (U1 - U2) / (I1 - I2)` 逻辑得出 `s_resistance`
2. 构造 `LineLimit_Config_t` 和 `LineLimit_State_t`
3. 调用 `Process_AcLine_Limit()`
4. 若返回 `LINE_LIMIT_STATUS_OK`，将：
   - `s_ioc_target_a = idc_max_limit_a`
   - `output->ioc = idc_max_limit_a`
   - `output->set_current_a = idc_max_limit_a`
   - `output->dac_code = dac_code`
5. 若返回 `LINE_LIMIT_STATUS_NEED_RETEST`，则：
   - `output->need_retest = 1U`
   - 状态机回退到重新探测路径
   - 本次不发布有效闭环 IOC 目标

**Step 4: 在 `StartCalcControlTask()` 中补足实时输入**

在 [`app_freertos.c`](../../Core/Src/app_freertos.c) 的 `StartCalcControlTask()` 中填充：

```c
calc_in.vac_ch1_v = Measure_GetV1In();
calc_in.vac_ch2_v = Measure_GetV2In();
calc_in.vdc_sample_v = Measure_GetVoOut();
```

其余对 `Output_Control_SetCurrent()` 的调用方式保持不变，以保证输出控制层仍然掌握最终硬件安全门控。

### Task 4: 更新诊断输出并完成回归

**Files:**
- Modify: `Core/Src/app_freertos.c`
- Test: `tests/check_adc_config.ps1`
- Test: `tests/check_uart_gui_protocol.ps1`

**Step 1: 维持现有 `[Calc]` 帧兼容性**

现有 `[Calc]` 文本帧仍保留：

```text
[Calc] R:... U1:... I1:... U2:... I2:... IOC:...
```

其中 `IOC` 字段继续复用，但其值改为“基于交流线阻、在线通道数和交直流折算得到的直流侧最大允许电流”。这样可以最大程度避免破坏 GUI 正则和现有回归脚本。

**Step 2: 若本次结果被拒绝，增加状态或错误上报**

当 `need_retest == 1U` 时，建议通过 `MSG_TYPE_STATE` 上报一条明确的诊断线，例如：

```text
[State] STATE:NEED_RETEST
```

是否纳入协议标准，需在实现会话中同步检查 [`STANDARDS_interface.md`](../STANDARDS_interface.md#standards-interface) 和现有正则校验脚本。如果不扩展协议，则至少在本地状态机行为测试中覆盖“拒绝本次闭环接管并重新探测”的分支。

**Step 3: 跑项目强制回归**

Run: `powershell -File tests/check_adc_config.ps1`  
Expected: PASS

Run: `powershell -File tests/check_uart_gui_protocol.ps1`  
Expected: PASS

## 5. 验收标准

以下标准全部满足，才视为本次交流线阻闭环限功率功能交付完成。

### 5.1 功能验收

1. 系统在完成 3A / 2A 探测并算出 `R` 后，不再使用旧的简化 `IOC = K / R` 公式直接接管。
2. 闭环限流值必须按“阻抗校验 -> 交流限流 -> 单/双通道限幅 -> 交直流折算 -> DAC 量化”五阶段顺序产生。
3. 双通道在线时，`Vac_Sample` 必须严格取 `(Vac1 + Vac2) / 2`。
4. 交流采样电压严格按 `Vac > 50.0V` 判定在线；零在线场景必须拒绝本次闭环接管。
5. 单通道在线时交流电流上限不得超过 `10.0A`；双通道在线时不得超过 `20.0A`。
6. `R_Calc` 不在 `[0.2, 15.0]` 内，或相对 `R_Last` 的波动大于 `0.5Ω` 时，必须返回 `NEED_RETEST`。
7. `Vac` 或 `Vdc` 低于有效阈值时，必须拒绝本次换算，不能出现除零或极低分母放大结果。
8. DAC 量化必须满足 `10.0A -> 4095`，并在超量程时饱和满码。

### 5.2 结构验收

1. 新算法存在于独立的 `line_limit.[ch]` 模块中，而不是散落到多个任务文件里。
2. 核心算法入口必须为结构体传参接口，不得依赖文件级外部全局变量。
3. `calc_control.c` 仅负责状态机、锁存和闭环交接，不承担大段新的物理换算细节。
4. `Output_Control_SetCurrent()` 仍然是最终写入 DAC 控制链路的上层入口。

### 5.3 测试验收

1. `python tests/check_state_machines_behavior.py` 全量通过。
2. `powershell -File tests/check_adc_config.ps1` 通过。
3. `powershell -File tests/check_uart_gui_protocol.ps1` 通过。
4. 若扩展了新的状态帧或错误帧，GUI 正则和协议校验必须同步更新并通过回归。

### 5.4 Diff 验收

1. 本次改动应集中在：
   - `Core/Inc/calc_control.h`
   - `Core/Src/calc_control.c`
   - `Core/Inc/line_limit.h`
   - `Core/Src/line_limit.c`
   - `Core/Src/app_freertos.c`
   - `tests/check_state_machines_behavior.py`
   - 必要时的协议/测试文档
2. 不应顺带重构 UART、ADC、GUI 或输出保护状态机的无关逻辑。

## 6. 交付说明

这份文档的定位是“实现前的正式施工说明书”，不是新的状态机真理源。真正的状态机行为定义仍以 [`LOGIC_calc-control.md`](../LOGIC_calc-control.md#logic-calc-control) 为准；本文件只描述如何在不破坏该真理源的前提下，把新的交流线阻闭环限功率算法安全接入当前固件。
