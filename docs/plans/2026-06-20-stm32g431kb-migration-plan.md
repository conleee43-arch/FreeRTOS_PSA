<a id="stm32g431kb-migration-plan"></a>
# STM32G431KB Migration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将当前工程从 `STM32G431CB` 48 引脚平台迁移到 `STM32G431KB` 32 引脚平台，并把 ADC 工作参考从“内部 `VREFBUF` 输出 2.5V”切换为“外部稳定 2.5V 直供 `VDDA`”，同时保持现有引脚功能、UART 协议、FreeRTOS 任务拓扑和 DAC 输出链路尽量不变。

**Architecture:** 本次迁移只处理三类事实变更：器件/package 元数据、ADC 规则扫描与 DMA 数据布局、以及测量链路中的参考电压来源。保持 `adc_physics.c` 的表驱动物理解算所有权不变，保留 `HAL_ADCEx_Calibration_Start()` 硬件校准，避免自发重写稳定工作的 RTOS、UART、保护状态机和 DAC 控制模块。对 `adc_calib.c` 采用“固定 2.5V 参考 + 温度换算助手”策略，而不是直接删除模块，以维持 `Measure_GetVref()` / `Measure_GetTemp()` 与 GUI 协议兼容。

**Tech Stack:** STM32G4xx HAL, FreeRTOS (CMSIS-OS V2), STM32CubeMX `.ioc`, Keil MDK-ARM, Python behavior harness, PowerShell validation scripts.

---

## 1. 迁移边界与必须保持不变的内容

本次迁移的已知输入边界如下：

1. MCU 目标从 `STM32G431CBT6` 切换到同系列 `STM32G431KB` 32 引脚封装。
2. 板级参考基准不再由 MCU 内部 `VREFBUF` 驱动 `VREF+`，而是由外部稳定 `2.5V` 直接供给 `VDDA`。
3. 用户已确认“其他引脚和功能没有发生任何变化”，因此本次迁移不应主动重构 UART、DAC、GPIO 控制语义、FreeRTOS 任务关系或 GUI 交互协议。

迁移时必须保持以下行为不变：

- `PA0/PA1/PA2/PA3` 继续作为 `ADC1_IN1..IN4`。
- `PA4` 继续作为 `DAC1_OUT1` 对应的 `IOC` 输出。
- `PB0` 继续作为 `OF_EN`。
- `PA9/PA10` 继续作为 `USART1`。
- `[Measure]` 遥测帧保持 `V1/V2/CO/VO/T/Vref` 六字段结构不变。
- `HAL_ADCEx_Calibration_Start()` 必须继续保留在 DMA 启动前。

同时，本计划明确将以下内容排除在本次实施之外：

- 不在本次迁移会话中更新长期架构文档真理；代码通过并上板确认后，再单独执行 `doc sweep`。
- 不在本次迁移中引入新的 ADC 通道、额外滤波器、新任务或协议字段。
- 不因平台迁移顺手重写 `adc_physics.c`、`app_freertos.c` 或 GUI 布局。

## 2. 当前代码中的真实冲突点

实施前必须先承认当前仓库里存在的旧真理绑定，否则迁移会出现“代码改了但测试和工程生成链还在反向拉回”的情况。

1. 旧工程身份仍是 `STM32G431CBTx + LQFP48`：`FreeRTOS_PSA.ioc` 和 `MDK-ARM/FreeRTOS_PSA.uvprojx` 还绑定在 `CB` 目标器件。
2. `VREFBUF` 当前由 `Core/Src/stm32g4xx_hal_msp.c` 使能，而不是 `gpio.c`；同时 `FreeRTOS_PSA.ioc` 还把 `VREF+` 设为 `VREFBUF_OUT`。
3. `ADC1` 规则序列当前为 6 路，最后两路分别是 `TEMPSENSOR` 和 `VREFINT`。
4. `adc_dma_driver.h` / `adc_dma_driver.c` 的 DMA 布局、滤波数组、原始码索引和仿真模式都按 6 路设计。
5. `adc_calib.c` 当前通过 `VREFINT` 动态解算 `vref_ema`，`adc_physics.c` 再把该 `vref_ema` 注入四路物理解算。
6. `app_freertos.c` 的 `[Code]` 诊断帧当前固定输出 `0..5` 六个 raw code。
7. `tests/check_adc_config.ps1` 目前显式要求 `VREFBUF` 已开启且 `VREF+` 连接到 `VREFBUF_OUT`，这与新硬件真理直接冲突。

因此，本次迁移必须先改测试和生成链路，再改实现，避免落入“局部 C 代码暂时能编译，但 `.ioc` / 脚本 / GUI / 工程文件彼此不一致”的状态。

## 3. 实施原则

1. **测试先行**：凡是会改变 ADC 配置、DMA 通道布局、温度换算或 UART 诊断输出的地方，都必须先在 `tests/` 中写出新期望，再动 C 代码。
2. **最小兼容改法**：保留 `[Measure]` 的 `Vref` 字段，语义改为“ADC 工作参考电压”；不要因为 `VREFINT` 移除就删掉 GUI 字段。
3. **生成链优先**：以 `FreeRTOS_PSA.ioc` 为主入口调整器件与通道配置，重新生成 `adc.c` / `stm32g4xx_hal_msp.c` 等文件，禁止只改生成结果不改 `.ioc`。
4. **硬件校准保留**：`HAL_ADCEx_Calibration_Start()` 不是动态参考补偿的一部分，不能跟着 `VREFINT` 一起被删除。
5. **文档延后**：除本计划和注册表外，本次实现不更新 `ARCH_technical-specs.md`、`STANDARDS_interface.md` 等真理文档，待代码和上板闭环通过后再单独清扫。

## 4. 任务拆解

### Task 1: 先把新平台真理锁进测试

**Files:**
- Modify: `tests/check_adc_config.ps1`
- Modify: `tests/check_uart_gui_protocol.ps1`
- Create: `tests/check_adc_fixed_vref_behavior.py`
- Test: `tests/check_adc_config.ps1`
- Test: `tests/check_uart_gui_protocol.ps1`
- Test: `tests/check_adc_fixed_vref_behavior.py`

**Step 1: Write the failing test**

先更新 `tests/check_adc_config.ps1`，把硬件真理改成：

- `FreeRTOS_PSA.ioc` 目标器件为 `STM32G431KB` 32-pin
- `Core/Src/stm32g4xx_hal_msp.c` 中不再出现 `HAL_SYSCFG_EnableVREFBUF();`
- 若保留显式保护代码，应检查 `HAL_SYSCFG_DisableVREFBUF();`
- `ADC1.NbrOfConversion=5`
- 不再配置 `ADC_CHANNEL_VREFINT`
- `Core/Inc/adc_dma_driver.h` 中 `ADC_DRV_CHANNEL_CNT == 5U`

再更新 `tests/check_uart_gui_protocol.ps1`，把原始诊断帧期望从：

```text
[Code] 0:<raw0>,1:<raw1>,2:<raw2>,3:<raw3>,4:<raw4>,5:<raw5>
```

改为：

```text
[Code] 0:<raw0>,1:<raw1>,2:<raw2>,3:<raw3>,4:<raw4>
```

并新增一个宿主机算法测试 `tests/check_adc_fixed_vref_behavior.py`，至少覆盖：

```python
assert_close(vref_ema, 2.5, "fixed external vref stays at 2.5V")
assert_close(vref_inst, 2.5, "instantaneous vref is pinned to 2.5V")
assert_between(chip_temp_c, -40.0, 150.0, "temperature conversion remains bounded")
```

**Step 2: Run test to verify it fails**

Run: `powershell -File tests/check_adc_config.ps1`  
Expected: FAIL，因为当前仓库仍要求 `VREFBUF` 开启、器件仍是 `CB/LQFP48`、ADC 仍是 6 路。

Run: `powershell -File tests/check_uart_gui_protocol.ps1`  
Expected: FAIL，因为当前固件仍输出 6 路 `[Code]` raw diagnostics。

Run: `python tests/check_adc_fixed_vref_behavior.py`  
Expected: FAIL，因为当前 `adc_calib.c` 仍按 `VREFINT` 动态解算参考电压。

**Step 3: Commit**

```bash
git add tests/check_adc_config.ps1 tests/check_uart_gui_protocol.ps1 tests/check_adc_fixed_vref_behavior.py
git commit -m "test: lock stm32g431kb fixed-vref migration expectations"
```

### Task 2: 迁移 CubeMX 工程身份并移除 `VREFINT` 规则扫描

**Files:**
- Modify: `FreeRTOS_PSA.ioc`
- Modify: `Core/Src/adc.c`
- Modify: `Core/Src/stm32g4xx_hal_msp.c`
- Test: `tests/check_adc_config.ps1`

**Step 1: Re-run the failing hardware/config check**

Run: `powershell -File tests/check_adc_config.ps1`  
Expected: FAIL，作为当前静态基线。

**Step 2: Write minimal implementation**

在 `FreeRTOS_PSA.ioc` 中完成以下调整并重新生成代码：

1. 目标器件从 `STM32G431CBTx` 切换到 `STM32G431KBTx` 或等效的 `KB` 32-pin 目标。
2. 移除 `Mcu.Pin11=VREF+` 对应的 `VREFBUF_OUT` 绑定。
3. 取消 `SYS.VoltageScaling=SYSCFG_VREFBUF_VOLTAGE_SCALE1` 对 `VREFBUF` 输出场景的依赖。
4. 规则通道从 6 路收缩到 5 路，仅保留：
   - `ADC_CHANNEL_1`
   - `ADC_CHANNEL_2`
   - `ADC_CHANNEL_3`
   - `ADC_CHANNEL_4`
   - `ADC_CHANNEL_TEMPSENSOR_ADC1`
5. 从 `ADC1.CommonPathInternal` 中移除 `ADC_CHANNEL_VREFINT`。

在生成结果中确认：

- `Core/Src/adc.c` 的 `hadc1.Init.NbrOfConversion` 变为 `5`
- `Rank 6 / ADC_CHANNEL_VREFINT` 配置块消失
- `Core/Src/stm32g4xx_hal_msp.c` 不再启用 `VREFBUF`

若 CubeMX 未自动显式关闭 `VREFBUF`，可只在 `HAL_MspInit()` 的 USER CODE 区补入：

```c
HAL_SYSCFG_DisableVREFBUF();
```

**Step 3: Run test to verify it passes**

Run: `powershell -File tests/check_adc_config.ps1`  
Expected: PASS for device/package identity, VREFBUF disablement, and 5-channel ADC regular sequence.

**Step 4: Commit**

```bash
git add FreeRTOS_PSA.ioc Core/Src/adc.c Core/Src/stm32g4xx_hal_msp.c
git commit -m "chore: retarget cubemx adc config to stm32g431kb"
```

### Task 3: 将 DMA 测量驱动从 6 路对齐到 5 路

**Files:**
- Modify: `Core/Inc/adc_dma_driver.h`
- Modify: `Core/Src/adc_dma_driver.c`
- Modify: `Core/Src/app_freertos.c`
- Test: `tests/check_uart_gui_protocol.ps1`

**Step 1: Re-run protocol/static regression as the reference**

Run: `powershell -File tests/check_uart_gui_protocol.ps1`  
Expected: FAIL，因为当前 `[Code]` 帧和 DMA 索引仍按 6 路设计。

**Step 2: Write minimal implementation**

在 `Core/Inc/adc_dma_driver.h` 中：

```c
#define ADC_DRV_CHANNEL_CNT 5U
```

在 `Core/Src/adc_dma_driver.c` 中同步修改：

1. `g_filters[ADC_DRV_CHANNEL_CNT]`
2. `ADC_DRV_DMA_BUF_SIZE`
3. `Measure_Update()` 中 `write_offset / ADC_DRV_CHANNEL_CNT`
4. `Measure_UpdateRange()` 中的循环上界与温度索引
5. 删除对 `g_filters[5]` 的 `VREFINT` 访问
6. 仿真模式下不再生成第 6 路 `VREFINT` raw code

在 `Core/Src/app_freertos.c` 中，把诊断输出从：

```c
"[Code] 0:%u,1:%u,2:%u,3:%u,4:%u,5:%u\r\n"
```

改为：

```c
"[Code] 0:%u,1:%u,2:%u,3:%u,4:%u\r\n"
```

并仅调用 `Measure_GetRawCode(0..4)`。

**Step 3: Run test to verify it passes**

Run: `powershell -File tests/check_uart_gui_protocol.ps1`  
Expected: PASS for `[Measure]` frame compatibility and updated 5-channel `[Code]` diagnostics.

**Step 4: Commit**

```bash
git add Core/Inc/adc_dma_driver.h Core/Src/adc_dma_driver.c Core/Src/app_freertos.c
git commit -m "refactor: align adc dma driver with 5-channel kb layout"
```

### Task 4: 将 `adc_calib` 收敛为固定 2.5V 参考助手并保留温度换算

**Files:**
- Modify: `Core/Inc/adc_calib.h`
- Modify: `Core/Src/adc_calib.c`
- Modify: `Core/Src/adc_dma_driver.c`
- Test: `tests/check_adc_fixed_vref_behavior.py`

**Step 1: Re-run the failing fixed-vref behavior test**

Run: `python tests/check_adc_fixed_vref_behavior.py`  
Expected: FAIL，因为当前 `ADC_Calib_Update()` 仍通过 `VREFINT` 解算 `vref_inst/vref_ema`。

**Step 2: Write minimal implementation**

对 `adc_calib` 做兼容式最小修改：

1. 在 `Core/Inc/adc_calib.h` 中引入固定外部参考常量，例如：

```c
#define ADC_CALIB_FIXED_VREF_V (2.5f)
```

2. 在 `ADC_Calib_Init()` 中把默认参考与输出初值统一到 `2.5f`。
3. 在 `ADC_Calib_Update()` 中停止使用 `vrefint_raw` 参与动态解算，改为：

```c
gs_calib_data.vref_inst = ADC_CALIB_FIXED_VREF_V;
gs_calib_data.vref_ema  = ADC_CALIB_FIXED_VREF_V;
```

4. 保留温度换算路径，只让 `temp_raw` 继续通过：

```c
ts_data_cal = temp_raw * (gs_calib_data.vref_ema / ADC_CALIB_TS_CAL_V);
```

完成“2.5V 采样码等效回 3.0V 标定条件”的换算。

5. 在 `Core/Src/adc_dma_driver.c` 的 `Measure_UpdateRange()` 中，改为只把 `TEMPSENSOR` raw code 喂给 `ADC_Calib_Update()`；`vrefint_raw` 参数可传 `0U` 或保留但明确忽略。

**Step 3: Run test to verify it passes**

Run: `python tests/check_adc_fixed_vref_behavior.py`  
Expected: PASS

**Step 4: Commit**

```bash
git add Core/Inc/adc_calib.h Core/Src/adc_calib.c Core/Src/adc_dma_driver.c
git commit -m "refactor: pin adc calibration path to external 2.5v reference"
```

### Task 5: 同步 GUI 命名、Keil 工程身份并完成项目强制回归

**Files:**
- Modify: `gui/main.py`
- Modify: `MDK-ARM/FreeRTOS_PSA.uvprojx`
- Test: `tests/check_adc_config.ps1`
- Test: `tests/check_uart_gui_protocol.ps1`

**Step 1: Update the minimal user-facing label**

把 `gui/main.py` 中：

```python
"VREF+ 内部参考电压"
```

改为：

```python
"ADC 工作参考电压"
```

只改文案，不改 `MEASURE_PATTERN` 和数据显示槽位。

**Step 2: Retarget the Keil project identity**

在 `MDK-ARM/FreeRTOS_PSA.uvprojx` 中同步更新器件字符串到 `STM32G431KBTx` 或实际使用的 KB 目标器件，确保：

- `<Device>` 指向 KB 器件
- `FlashDriverDll` / `RegisterFile` / `SFDFile` 与目标器件一致
- 继续保留 `startup_stm32g431xx.s`
- 继续保留 `USE_HAL_DRIVER,STM32G431xx`

**Step 3: Run the mandatory project regressions**

Run: `powershell -File tests/check_adc_config.ps1`  
Expected: PASS

Run: `powershell -File tests/check_uart_gui_protocol.ps1`  
Expected: PASS

**Step 4: Review the resulting diff**

Run: `git diff -- FreeRTOS_PSA.ioc Core/Inc/adc_calib.h Core/Inc/adc_dma_driver.h Core/Src/adc.c Core/Src/adc_calib.c Core/Src/adc_dma_driver.c Core/Src/app_freertos.c Core/Src/stm32g4xx_hal_msp.c gui/main.py MDK-ARM/FreeRTOS_PSA.uvprojx tests/check_adc_config.ps1 tests/check_uart_gui_protocol.ps1 tests/check_adc_fixed_vref_behavior.py`

Expected: only the KB target migration, VREFBUF disablement, 5-channel ADC layout, fixed-2.5V calibration path, GUI label adjustment, and test updates appear.

**Step 5: Commit**

```bash
git add FreeRTOS_PSA.ioc Core/Inc/adc_calib.h Core/Inc/adc_dma_driver.h Core/Src/adc.c Core/Src/adc_calib.c Core/Src/adc_dma_driver.c Core/Src/app_freertos.c Core/Src/stm32g4xx_hal_msp.c gui/main.py MDK-ARM/FreeRTOS_PSA.uvprojx tests/check_adc_config.ps1 tests/check_uart_gui_protocol.ps1 tests/check_adc_fixed_vref_behavior.py
git commit -m "feat: migrate adc platform to stm32g431kb fixed 2.5v reference"
```

## 5. 验收标准

满足以下条件，才算本次迁移完成：

1. `FreeRTOS_PSA.ioc` 和 `MDK-ARM/FreeRTOS_PSA.uvprojx` 均已指向 `STM32G431KB` 目标器件。
2. `VREFBUF` 在初始化路径中保持禁用，不再把 `VREF+` 作为 `VREFBUF_OUT` 输出脚。
3. `ADC1` 规则序列缩减为 5 路，`VREFINT` 不再进入 DMA 扫描。
4. `HAL_ADCEx_Calibration_Start()` 仍保留且在 `HAL_ADC_Start_DMA()` 前执行。
5. 四路物理通道换算仍以 `2.5V` 为 ADC 工作参考，`Measure_GetVref()` 稳定输出 `2.5V`。
6. `Measure_GetTemp()` 仍能输出合理温度，未退化为固定值或非法极值。
7. GUI 仍能正确解析 `[Measure]` 帧，`Vref` 数值正常显示。
8. 两条项目强制校验命令均通过：
   - `powershell -File tests/check_adc_config.ps1`
   - `powershell -File tests/check_uart_gui_protocol.ps1`

## 6. 实施备注

- 本计划默认“板级除 MCU 封装与参考源接法外无其他差异”这一前提成立；若后续上板发现 `KB` 封装下某个功能脚在物理上不存在或复用冲突，应中止实施并先回到原理图核对。
- 如果 CubeMX 重新生成后改动超出 `adc.c` / `stm32g4xx_hal_msp.c` / `.ioc` 的合理范围，先停止，不要顺手清理无关 diff。
- 本计划故意没有把 `adc_physics.c` 改为“硬编码 2.5V”，因为当前表驱动结构已经足够，且保留 `Measure_GetVref()` 对 GUI 和诊断更稳。

