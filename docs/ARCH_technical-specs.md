<a id="technical-specs"></a>
# ARCH_technical-specs — 底层系统硬件与架构技术规范

---

## 1. 处理器时钟树配置 (170MHz 主频)

本项目运行于高主频 MCU (STM32G4 系列)，采用 24MHz 外部高速无源晶振 (HSE)，通过 PLL 锁相环倍频至最高的 170MHz 主频以供高性能模拟采样。下面为时钟源的物理配置代码：

```c
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV6;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}
```

---

## 2. 内部基准电压源配置 (VREFBUF SCALE1)

为防范供电网络噪声对 ADC 模拟信号输入造成干扰，芯片开启了内部集成的高精密基准电压缓冲器 (VREFBUF)，将其配置为 `SCALE1` (2.5V 物理参考电压基准)，并直接在 `VREF+` 引脚上物理输出。此特性对于保证模数转换的高一致性至关重要。

### 2.1 硬件自检与断言点
项目采用自动化脚本 [../tests/check_adc_config.ps1](../tests/check_adc_config.ps1) 强制要求固件使能 VREFBUF 硬件并防止被 CubeMX 意外覆写。

---

## 3. 外设 DMA 传输通道分配与优先级

为实现 CPU 的高能效，ADC 数据搬移和串口发送/接收全部交由底层硬件 DMA 控制器负责。

| 硬件外设 | 触发事件 | DMA 资源 | 传输宽度 | 传输模式 | 硬件优先级 |
|---|---|---|---|---|---|
| **ADC1** | 多通道扫描就绪 | DMA1 Channel 3 | 16-bit (HalfWord) | Circular (循环乒乓) | Low (`DMA_PRIORITY_LOW`) |
| **USART1_RX** | 触发 Idle 空闲中断 | DMA1 Channel 1 | 8-bit (Byte) | Normal (不定长自愈) | Low (`DMA_PRIORITY_LOW`) |
| **USART1_TX** | 缓冲区发送为空 | DMA1 Channel 2 | 8-bit (Byte) | Normal (非阻塞链式) | Low (`DMA_PRIORITY_LOW`) |

当前优先级表反映 CubeMX/HAL 初始化代码中的实际配置。若未来要把 ADC 或 UART DMA 改为更高硬件优先级，必须同步修改 `adc.c` / `usart.c` 的 `DMA_PRIORITY_*` 配置、重新运行硬件配置检查，并在本表中记录变更原因。

---

## 4. ADC 驱动仿真/降级模式

`adc_dma_driver.c` 包含一个用于无硬件句柄场景的防御性仿真入口：当 `Measure_Init()` 收到的 ADC 句柄 `Instance == NULL` 时，驱动不会继续启动真实 DMA，而是进入 `g_simulation_mode`，初始化校准与物理解算模块后返回 `HAL_OK`。

仿真模式的目的：

- 支持无真实 ADC 外设实例的主机侧或调试侧运行，避免初始化路径直接挂死。
- 合成 V1/V2、CO、VO、Vref、Temp 等工业信号，使上层 `[Measure]`、保护状态机和 GUI 解析路径可以被观察。
- 作为防御性降级/测试模式存在，不代表真实硬件测量链路的标定真理。

仿真模式中原始 ADC 码由合成物理量反推得到，当前使用的反推比例常量为：

| 仿真通道 | 反推比例 | 说明 |
|---|---:|---|
| `V1_IN` | `124.4f` | 用于把合成交流输入电压换算成近似 ADC 原始码 |
| `V2_IN` | `124.4f` | 同上 |
| `CO_OUT` | `6.0f` | 用于把合成输出电流换算成近似 ADC 原始码 |
| `VO_OUT` | `200.0f` | 用于把合成输出电压换算成近似 ADC 原始码 |

这些仿真常量只服务于合成波形可视化；真实硬件通道的唯一标定真理仍是下文 `adc_physics.c` 表驱动通道属性。

---

## 5. 扩展物理通道与 GPIO 控制资源

为支持闭环 PFC 目标电流调节及保护机制，固件在硬件层配置了以下扩展资源：

| 外设/引脚 | 物理引脚 | 配置模式 | 作用与功能 |
|---|---|---|---|
| **DAC1_OUT1** | PA4 | Analog (模拟模式) | 闭环 PFC 目标反馈电流输出 (0.00 ~ 10.00A) |
| **OF_EN** | PB0 | Output PP (推挽输出) | 过流保护硬件使能控制信号 (使能: 1 / 禁用: 0) |

---

## 6. 表驱动解算通道属性 (Table-Driven Calibration Table)

项目利用“表驱动（Table-Driven）”设计对所有模拟电网和传感器引脚的物理转换因子进行了集中定义。每一物理通道与硬件调理比例（前级满量程物理量、引脚满量程模拟电压、直流偏置量）一一绑定，由 `adc_physics.c` 集中维护，结构如下：

```c
static Physical_Channel_t s_channels[PHYS_CH_NUM] = {
    [PHYS_CH_V1_IN] = {
        .config = {
            .physical_max    = 311.0f,
            .pin_max_voltage = 2.50f,
            .offset_value    = 0.0f
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_V2_IN] = {
        .config = {
            .physical_max    = 311.0f,
            .pin_max_voltage = 2.50f,
            .offset_value    = 0.0f
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_CO_OUT] = {
        .config = {
            .physical_max    = 15.0f,
            .pin_max_voltage = 2.50f,
            .offset_value    = 0.0f
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_VO_OUT] = {
        .config = {
            .physical_max    = 500.0f,
            .pin_max_voltage = 2.50f,
            .offset_value    = 0.0f
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    }
};
```
