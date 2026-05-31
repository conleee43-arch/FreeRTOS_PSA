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
项目采用自动化脚本 [check_adc_config.ps1](file:///d:/zhihai/Software/FreeRTOS_PSA/tests/check_adc_config.ps1) 强制要求固件使能 VREFBUF 硬件并防止被 CubeMX 意外覆写。

---

## 3. 外设 DMA 传输通道分配与优先级

为实现 CPU 的高能效，ADC 数据搬移和串口发送/接收全部交由底层硬件 DMA 控制器负责。

| 硬件外设 | 触发事件 | DMA 资源 | 传输宽度 | 传输模式 | 硬件优先级 |
|---|---|---|---|---|---|
| **ADC1** | 多通道扫描就绪 | DMA1 Channel 1 | 16-bit (HalfWord) | Circular (循环乒乓) | High (高优先级) |
| **USART1_RX** | 触发 Idle 空闲中断 | DMA1 Channel 2 | 8-bit (Byte) | Normal (不定长自愈) | Medium (中优先级) |
| **USART1_TX** | 缓冲区发送为空 | DMA1 Channel 3 | 8-bit (Byte) | Normal (非阻塞链式) | Medium (中优先级) |

---

## 4. 表驱动解算通道属性 (Table-Driven Calibration Table)

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
