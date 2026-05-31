# REF 系统配置与优化参考

## 概述

本文档记录 STM32G431 固件系统的可配置参数和优化选项，便于快速调整系统行为。

## 系统配置宏

### 配置文件
- 位置: [Core/Inc/main.h](Core/Inc/main.h) (USER CODE BEGIN Private defines 区域)

### 日志级别配置

| 级别 | 值 | 说明 |
|------|-----|------|
| DEBUG | 0 | 详细模式，包含原始ADC数据和调试信息 |
| INFO | 1 | 标准模式，仅输出物理量测量值 |
| WARN | 2 | 精简模式，仅输出警告和错误 |
| SILENT | 3 | 静默模式，关闭所有自动日志输出 |

```c
#ifndef LOG_LEVEL
#define LOG_LEVEL  0U   // 默认DEBUG模式
#endif
```

### 测量报告周期

```c
#ifndef MEASURE_REPORT_PERIOD_MS
#define MEASURE_REPORT_PERIOD_MS  50U   // 默认50ms
#endif
```

可通过 UART 命令 `SetPeriod:<ms>` 动态修改（范围：20~5000ms）

### 心跳指示灯周期

```c
#ifndef HEARTBEAT_PERIOD_MS
#define HEARTBEAT_PERIOD_MS  1000U   // 默认1秒，0=禁用
#endif
```

### 原始ADC数据输出开关

```c
#ifndef SHOW_ADC_RAW_DATA
#define SHOW_ADC_RAW_DATA  1U   // 1=输出原始滤波值，0=仅输出物理量
#endif
```

## ADC 测量模块配置

### 配置文件
- 位置: [Core/Inc/adc_measure.h](Core/Inc/adc_measure.h)

### EMA 平滑滤波配置

VREF+ 参考电压使用 EMA（指数移动平均）滤波器进行平滑：

```c
#define VREF_EMA_ALPHA     0.10f   // EMA 系数: 0.01~0.20 之间较合适
```

- 系数越小，滤波越平滑但响应越慢
- 系数越大，响应越快但噪声抑制能力下降

### 中值滤波配置

```c
#define MEDIAN_FILTER_WINDOW_SIZE  (5U)   // 滑动窗口大小
```

### DMA 缓冲区配置

```c
#define ADC_CHANNEL_NUM            (6U)   // 通道数
#define ADC_SAMPLE_GROUPS          (2U)    // 采样组数
#define ADC_DMA_BUFFER_SIZE        (12U)   // DMA缓冲区大小 = 通道数 × 组数
```

## UART 安全层配置

### 配置文件
- 位置: [Core/Inc/uart_safe.h](Core/Inc/uart_safe.h)

### 缓冲区大小

| 参数 | 默认值 | 说明 |
|------|--------|------|
| MSG_QUEUE_SIZE | 32 | 环形队列深度 |
| MSG_MAX_LEN | 200 | 最大消息长度 |
| UART_SAFE_DMA_BUFFER_SIZE | 256 | DMA发送缓冲区 |
| UART_SAFE_RX_BUFFER_SIZE | 32 | RX缓冲区 |
| UART_SAFE_MAIN_BUFFER_SIZE | 33 | 主解析缓冲区 |

### 复位延迟

```c
#define UART_SAFE_RESET_DELAY_MS    100U   // 复位命令延迟执行时间
```

## 新增 UART 命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `SetPeriod:<ms>` | 设置报告周期 | `SetPeriod:100` |
| `Status` | 查询系统状态 | `Status` |

## 日志输出格式

### DEBUG 模式 (LOG_LEVEL=0)
```
[Measure] V1:xx.xV V2:xx.xV CO:xx.xxA VO:xxx.xV T:xx.xC Vref:x.xxxV
[Raw] V1_raw V2_raw CO_raw VO_raw Temp_raw Vrefint_raw
[Sys] BufUse:xx% DAC:xx.xxA
```

### INFO 模式 (LOG_LEVEL=1)
```
[M] xx.xV xx.xV xx.xxA xxx.xV xx.xC
[Sys] BufUse:xx% DAC:xx.xxA
```

### WARN 模式 (LOG_LEVEL=2)
```
(无自动输出，等待异常触发)
```

## 性能优化说明

1. **编译时条件编译**: 使用 `#if LOG_LEVEL` 减少不必要的字符串生成和 UART 传输
2. **EMA 平滑**: VREF+ 使用指数移动平均滤波，减少测量值抖动
3. **动态周期**: 报告周期可通过命令动态调整，适应不同测试场景
4. **简化输出**: INFO/WARN 模式使用更紧凑的输出格式，减少 UART 带宽占用