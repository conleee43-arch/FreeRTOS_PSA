# REF 硬件与软件映射

## MCU 与封装

- MCU 型号: `STM32G431CBT6`
- 项目文件中使用的设备系列名称: `STM32G431CBTx`
- 系列 (Family): `STM32G4`
- 封装 (Package): `LQFP48`

主要事实来源:
- `PSA.ioc`

## 内存映射

来自 `MDK-ARM/PSA.uvprojx`:
- Flash / IROM 起始地址: `0x08000000`
- Flash / IROM 大小: `0x20000` (128 KiB)
- SRAM / IRAM 起始地址: `0x20000000`
- SRAM / IRAM 大小: `0x8000` (32 KiB)

## 引脚映射

| 引脚 (Pin) | 标签 (Label) | 角色 (Role) | 来源 (Source) |
| --- | --- | --- | --- |
| `PA0` | `V1_IN` | `ADC1_IN1` | `PSA.ioc` |
| `PA1` | `V2_IN` | `ADC1_IN2` | `PSA.ioc` |
| `PA2` | `CO_OUT` | `ADC1_IN3` | `PSA.ioc` |
| `PA3` | `VO_OUT` | `ADC1_IN4` | `PSA.ioc` |
| `PA4` | `IOC` | `DAC1_OUT1` / `COMP_DAC11_group` | `PSA.ioc` |
| `PB0` | `OF_EN` | 由主循环任务节拍与输出保护逻辑共同控制的 GPIO 输出/关断信号 | `PSA.ioc`, `main.h` |
| `PA9` | — | `USART1_TX` | `PSA.ioc`, `usart.c` |
| `PA10` | — | `USART1_RX` | `PSA.ioc`, `usart.c` |
| `PA13` | — | SWDIO | `PSA.ioc` |
| `PA14` | — | SWCLK | `PSA.ioc` |

## 外设映射

### ADC1
- 已启用多个常规转换通道
- 通道包括:
  - `ADC_CHANNEL_1`
  - `ADC_CHANNEL_2`
  - `ADC_CHANNEL_3`
  - `ADC_CHANNEL_4`
  - 内部温度传感器 (internal temperature sensor)
  - 内部参考电压通道 (Vrefint)
- 已启用 DMA 连续请求 (continuous requests)
- ADC DMA 模式: 循环 (circular)

### DAC1
- `PA4` 用于 DAC 相关的输出路径

### TIM7
- 作为基本定时器启用
- 在 CubeMX 中配置并在 NVIC 中启用了中断

### USART1
- 已启用 TX/RX 模式
- 波特率: `115200`
- GPIO:
  - `PA9` TX
  - `PA10` RX
- 由自定义的 `uart_safe` 传输层使用

## DMA 映射

| 外设路径 | DMA 通道 | 方向 | 模式 |
| --- | --- | --- | --- |
| `ADC1` | `DMA1_Channel1` | 外设到内存 (Peripheral to memory) | 循环 (Circular) |
| `USART1_RX` | `DMA1_Channel2` | 外设到内存 (Peripheral to memory) | 正常 (Normal) |
| `USART1_TX` | `DMA1_Channel3` | 内存到外设 (Memory to peripheral) | 正常 (Normal) |

## 中断映射

已配置及相关的中断包括:
- `DMA1_Channel1_IRQn`
- `DMA1_Channel2_IRQn`
- `DMA1_Channel3_IRQn`
- `ADC1_2_IRQn`
- `USART1_IRQn`
- `TIM7_IRQn`
- `SysTick_IRQn`

从 `PSA.ioc` 和生成的初始化代码中观察到的优先级事实:
- DMA1 Channel1/2/3: 优先级 `(0, 0)`
- `USART1_IRQn`: 优先级 `(0, 0)`
- `TIM7_IRQn`: 优先级 `(3, 0)`
- `SysTick_IRQn`: 优先级 `(3, 3)`
- 优先级分组 (Priority group): `NVIC_PRIORITYGROUP_2`

## 软件所有权说明

- 硬件事实来源: `PSA.ioc`
- 生成的外设初始化来源: `Core/Src/*.c`
- 运行时命令/日志传输逻辑: `Core/Src/uart_safe.c`
- 高精度 ADC 采集与中值滤波处理算法逻辑: `Core/Src/adc_measure.c` 与 `Core/Inc/adc_measure.h` (基于 DMA 计数的免中断定位、5点滑动窗口中值滤波、实时 VREF+ 自校准与片上温度传感器校准；工厂校准地址与温度端点使用 `stm32g4xx_ll_adc.h` 中的供应商宏)
- DAC PFC 电流控制与换算驱动逻辑: `Core/Src/dac_control.c` 与 `Core/Inc/dac_control.h` (基于 PA4/IOC 模拟通道与 2.5V 外部参考，提供直接 12 位寄存器设置以及带有溢出上限软保护的 PFC 浮点电流四舍五入精确转换接口)
- 输出保护状态机逻辑: `Core/Src/output_protection.c` 与 `Core/Inc/output_protection.h` (基于 `CO_OUT`、`VO_OUT` 与片上温度的 1 秒节拍故障判定，执行过温/过压回差恢复以及过流锁存)

## 何时更新此文档

在发生以下更改后进行更新:
- `PSA.ioc`
- 引脚分配
- DMA 通道分配
- 中断优先级
- 外设所有权或路由
