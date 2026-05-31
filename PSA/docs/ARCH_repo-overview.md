# ARCH 代码库概览

## 代码库特征

本代码库是一个小型的 STM32G431 裸机固件项目，使用 STM32CubeMX 生成，并主要通过签入（提交）的 Keil MDK-ARM 工程进行构建。

当前的高级特征：
- 单一 MCU 目标：`STM32G431CBTx`
- 基于 STM32 HAL/CMSIS 的固件
- CubeMX 管理的外设配置
- 将 Keil µVision / ArmClang 作为实际签入的构建/调试工作流
- 主要的手动维护子系统：`uart_safe`、`adc_measure`、`dac_control` 与 `output_protection`

## 顶层结构

- `Core/`
  - 应用程序源码和头文件
  - 包含 CubeMX 生成的外设初始化和用户代码区域
- `Drivers/`
  - 供应商 CMSIS 和 STM32 HAL 代码
- `MDK-ARM/`
  - Keil 工程文件、调试设置和构建输出
- `PSA.ioc`
  - STM32CubeMX 工程以及硬件/外设源配置
- `.mxproject`
  - CubeMX 元数据
- `docs/`
  - 活文档 (living-docs) 风格的共享文档

## 代码所有权划分

### 大部分由生成工具 / 框架所有
- `Core/Src/adc.c`
- `Core/Src/dac.c`
- `Core/Src/dma.c`
- `Core/Src/gpio.c`
- `Core/Src/tim.c`
- `Core/Src/usart.c`
- `Core/Src/stm32g4xx_it.c`
- `Core/Src/stm32g4xx_hal_msp.c`
- `Drivers/*`

### 主要的手动维护逻辑
- `Core/Src/uart_safe.c`
- `Core/Inc/uart_safe.h`
- `Core/Src/adc_measure.c` 与 `Core/Inc/adc_measure.h` (高精度ADC滤波算法)
- `Core/Src/dac_control.c` 与 `Core/Inc/dac_control.h` (PFC电流DAC输出控制)
- `Core/Src/output_protection.c` 与 `Core/Inc/output_protection.h` (1秒节拍的输出保护状态机)
- 生成文件中的用户代码区域 (User code regions)

## 运行时概览

观察到及推断的运行时流程：

1. HAL 复位和系统时钟设置
2. 通过 CubeMX 生成的初始化函数调用进行外设初始化
3. 通过 `UART_Safe_Init(&huart1)` 启动 UART 安全层
4. 主循环轮询 `Process_UART_Commands()`
5. `adc_measure` 持续通过 DMA + 滤波更新电压、电流和片上温度
6. 主循环每 1 秒执行一次输出保护判断，并在故障时强制关闭 `OF_EN` 与 DAC 输出
7. 当无保护故障且 `task_running != 0` 时，定期翻转 `OF_EN` 引脚
8. 当任务未运行或保护有效时，`OF_EN` 被拉低

主要的自定义行为是使用基于 DMA 的 RX（接收）和 TX（发送）在 `USART1` 上进行命令和日志传输。

## 主要子系统

### 硬件配置来源
- `PSA.ioc`
- 引脚、外设、DMA 映射、时钟树和 NVIC 配置的主要来源

### 构建/调试来源
- `MDK-ARM/PSA.uvprojx`
- 实际构建目标、内存布局和调试/下载行为的主要签入来源

### UART 行为来源
- `Core/Src/uart_safe.c`
- `Core/Inc/uart_safe.h`
- 负责命令解析、接收至空闲 (receive-to-idle) 处理、队列化 DMA 日志记录以及复位调度

### ADC 测量行为来源
- `Core/Src/adc_measure.c`
- `Core/Inc/adc_measure.h`
- 负责高精度多通道 ADC 滤波处理与电压温度校准计算

### DAC 行为来源
- `Core/Src/dac_control.c`
- `Core/Inc/dac_control.h`
- 负责高精度 PFC 电流与 DAC 寄存器数字值转换、安全溢出截断保护及外设启停控制

### 输出保护行为来源
- `Core/Src/output_protection.c`
- `Core/Inc/output_protection.h`
- `Core/Src/main.c`
- 负责 1 秒节拍下的过温/过压自动恢复保护以及过流锁存保护

## 重要的代码库事实

- ADC1 使用多个常规转换通道，包括内部温度传感器
- DAC1 通道 1 已启用（PA4/IOC 模拟输出引脚，2.5V外部参考，用于稳定输出 PFC 4A 电流控制电压）
- TIM7 已配置
- `USART1` 是串行命令/日志接口
- DMA 用于 ADC1 以及 USART1 的 RX/TX
- `PB0` (`OF_EN`) 在正常运行时由任务节拍控制，在保护故障时被强制拉低

## 实用指南

对于大多数任务，请从以下文档开始阅读：
1. `ARCH_documentation-governance.md`
2. `STANDARDS_generated-code-boundaries.md`
3. 特定子系统的文档之一 (`GUIDE_`, `REF_`, `LOGIC_`)

这个代码库足够小，安全进行开发的关键在于理解边界和事实来源，而不是阅读大量的说明文字。
