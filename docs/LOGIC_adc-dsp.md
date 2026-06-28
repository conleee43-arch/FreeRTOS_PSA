<a id="logic-adc-dsp"></a>
# LOGIC_adc-dsp — 核心信号滤波与动态自校准算法说明书

---

## 1. 5点自适应滑动中值滤波算法 (Adaptive Median Filter)

为彻底清除强电网环境下的脉冲尖峰干扰并防范相位失真，项目在 `adc_dma_driver.c` 中实现了一套自适应滑动中值滤波器。在冷启动时，样本数不足 5 时的排序宽度自适应收缩爬升，实现了无死锁、无等待的瞬态响应：

```c
static float Filter_AdaptiveMedian(Median_Filter_t *p_filt, uint16_t new_val)
{
    p_filt->raw_buffer[p_filt->write_idx] = new_val;
    p_filt->write_idx = (p_filt->write_idx + 1U) % MEDIAN_WINDOW;

    if (p_filt->sample_cnt < MEDIAN_WINDOW)
    {
        p_filt->sample_cnt++;
    }

    uint16_t temp_arr[MEDIAN_WINDOW];
    for (uint8_t i = 0U; i < p_filt->sample_cnt; i++)
    {
        temp_arr[i] = p_filt->raw_buffer[i];
    }

    if (p_filt->sample_cnt > 1U)
    {
        for (uint8_t i = 0U; i < (p_filt->sample_cnt - 1U); i++)
        {
            for (uint8_t j = 0U; j < (p_filt->sample_cnt - i - 1U); j++)
            {
                if (temp_arr[j] > temp_arr[j + 1U])
                {
                    const uint16_t swap = temp_arr[j];
                    temp_arr[j] = temp_arr[j + 1U];
                    temp_arr[j + 1U] = swap;
                }
            }
        }
    }

    float median_val;
    if ((p_filt->sample_cnt % 2U) != 0U)
    {
        median_val = (float)temp_arr[p_filt->sample_cnt / 2U];
    }
    else
    {
        const uint8_t idx_high = p_filt->sample_cnt / 2U;
        const uint8_t idx_low = idx_high - 1U;
        median_val = ((float)temp_arr[idx_low] + (float)temp_arr[idx_high]) / 2.0f;
    }

    p_filt->filtered_val = median_val;
    return median_val;
}
```

---

## 2. 外部供电基准与自校准计算降级处理

在当前 STM32G431KB 硬件平台下，ADC 参考基准已切换为外部精密稳定的 2.5V 电源直供，并且在 CubeMX/C 固件层面移除了对内部 `VREFINT` 通道的扫描与采样。

### 2.1 静态工作基准配置与 EMA 降级
由于不再对 `VREFINT` 进行动态采集，原有的 `VREFINT` 逆向解算和动态 EMA 滤波已被**完全停用**。在 `adc_calib.c` 驱动层，`vref_inst` 和 `vref_ema` 被直接强制赋值为固定的外部参考常量 `2.5V`：

$$V_{REF+, inst} = 2.5\text{V}$$
$$V_{REF+, EMA} = 2.5\text{V}$$

```c
gs_calib_data.vref_inst = ADC_CALIB_FIXED_VREF_V; // 2.5f
gs_calib_data.vref_ema  = ADC_CALIB_FIXED_VREF_V; // 2.5f
```

### 2.2 兼容性解算保留
为保持测量解算框架和上位机/诊断协议格式的不变性，温度传感器的 3.0V 等效标定（下文 3.1 节）和常规测量通道 of 二阶解算链路（下文 4.1 节）依然保留了带入 `vref_ema` 进行增益补偿的公式计算。因为 `vref_ema` 恒为 `2.5V`，这些运算等效为带入静态 2.5V 基准进行恒定折算，不仅消除了动态除法开销，也确保了对已有软硬件接口规范的完全兼容。

---

## 3. 片上温度传感器 3.0V 等效校准与双点线性插值

芯片结温测量的准确性很大程度上依赖于供电电压，温度传感器的原始读数随 $V_{REF+}$ 波动而波动。

### 3.1 3.0V 标准等效电压转换
温度传感器原始码的工厂标定是在 3.0V ($V_{REF, CAL\_V}$) 供电基准下烧录的。因此，需要把高频采集到的温度传感器原始码 $Temp_{RAW}$ 统一等效转换到 3.0V 标定基准下：

$$Temp_{CAL\_CODE} = Temp_{RAW} \times \frac{V_{REF+, EMA}}{3.0}$$

```c
const float ts_data_cal = (float)temp_raw * (gs_calib_data.vref_ema / ADC_CALIB_TS_CAL_V);
```

### 3.2 双点线性插值结温解算
根据芯片内部 ROM 固化存储的 $30^\circ\text{C}$ (TS_CAL1) 和 $130^\circ\text{C}$ (TS_CAL2) 标定码，使用线性插值高精度解出当前摄氏结温：

$$T = \frac{130 - 30}{TS\_CAL2 - TS\_CAL1} \times (Temp_{CAL\_CODE} - TS\_CAL1) + 30$$

```c
const float temp_diff = ADC_CALIB_TS_CAL2_TEMP - ADC_CALIB_TS_CAL1_TEMP;
const float cal_diff  = (float)ts_cal2 - (float)ts_cal1;

const float temp_value = (temp_diff / cal_diff) * (ts_data_cal - (float)ts_cal1) + ADC_CALIB_TS_CAL1_TEMP;
```

---

## 4. 常规物理测量通道二阶解算链路

对于普通的模拟传感器采样（如交流电压输入、母线电流等），物理量转化必须流经严密的二阶运算链路以抵消供电波动的增益误差：

### 4.1 一阶解算：数码量还原引脚模拟电压 ($V_{pin}$)
$$V_{pin} = \frac{ADC_{raw, median}}{4095.0} \times V_{REF+, EMA}$$

```c
p_ch->pin_voltage = ((float)p_ch->adc_raw_median / ADC_PHYS_MAX_CODE) * current_vref_ema;
```

### 4.2 二阶解算：模拟引脚电压换算成真实物理量 ($P_{val}$)
利用前级调理电阻分压或互感器的比例衰减因子，还原传感器前级的原始真实物理量，并补偿直流传感器双向或单向偏置：

$$P_{val} = V_{pin} \times \frac{Physical_{max}}{Pin_{max\_voltage}} - Offset_{value}$$

```c
const float scale_factor = p_ch->config.physical_max / p_ch->config.pin_max_voltage;
p_ch->physical_value = (p_ch->pin_voltage * scale_factor) - p_ch->config.offset_value;
```
