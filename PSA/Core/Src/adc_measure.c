/**
 ******************************************************************************
 * @file           : adc_measure.c
 * @brief          : 基于 STM32 HAL 库的高精度 ADC 采集与中值滤波处理算法模块实现
 * @author         : 资深驱动与算法工程师
 * ****************************************************************************
 */

/* 头文件包含 ----------------------------------------------------------------*/
#include "adc_measure.h"
#include "uart_safe.h"
#include <string.h>
#include <stdio.h>

/* 私有变量与内存分配 --------------------------------------------------------*/

/**
 * @brief 内部保存的 ADC 句柄只读指针
 */
static ADC_HandleTypeDef *gp_hadc = NULL;

/**
 * @brief  DMA 循环缓冲区 (按 32 字节对齐，有助于防硬件级总线存取冲突并确保数据高速存取)
 */
#if defined(__clang__) || defined(__GNUC__)
  /* GCC、Clang 以及 Keil AC6 完美兼容 */
  static uint16_t g_adc_dma_buffer[ADC_DMA_BUFFER_SIZE] __attribute__((aligned(32)));
#elif defined(__CC_ARM)
  /* 旧版 Keil MDK AC5 编译器对齐定义 */
  __align(32) static uint16_t g_adc_dma_buffer[ADC_DMA_BUFFER_SIZE];
#else
  /* 默认常规声明 */
  static uint16_t g_adc_dma_buffer[ADC_DMA_BUFFER_SIZE];
#endif

/**
 * @brief  各常规扫描通道的独立中值滤波器状态
 */
static MedianFilter_t g_filters[ADC_CHANNEL_NUM];

/**
 * @brief  高精度 ADC 全局状态与计算数据块
 */
static Measure_Data_t g_measure_data;

/* 私有函数声明 --------------------------------------------------------------*/
static void MedianFilter_Init(MedianFilter_t *filter);
static void MedianFilter_Insert(MedianFilter_t *filter, uint16_t val);
static uint16_t MedianFilter_GetMedian(MedianFilter_t *filter);

/* 外部接口函数实现 ----------------------------------------------------------*/

/**
 * @brief  初始化 ADC 高精度测量模块并启动 DMA 循环传输
 * @param  hadc ADC 句柄指针，传入配置完毕的 ADC1 句柄
 * @retval HAL_StatusTypeDef 返回 HAL_OK 表示成功，HAL_ERROR 表示指针异常或启动失败
 */
HAL_StatusTypeDef Measure_Init(ADC_HandleTypeDef *hadc)
{
    /* 1. 防御性编程：强制校验 ADC 句柄以及关联的 DMA 句柄 */
    if (hadc == NULL)
    {
        return HAL_ERROR;
    }
    if (hadc->DMA_Handle == NULL)
    {
        return HAL_ERROR;
    }

    /* 2. 缓存外设句柄 */
    gp_hadc = hadc;

    /* 3. 彻底清空 DMA 缓存和全局计算数据结构体，防止开机产生随机电压脉冲或杂波数据 */
    memset((void *)g_adc_dma_buffer, 0, sizeof(g_adc_dma_buffer));
    memset(&g_measure_data, 0, sizeof(Measure_Data_t));

    /* 4. 初始化所有通道的 5点中值滤波器 */
    for (uint8_t i = 0; i < ADC_CHANNEL_NUM; i++)
    {
        MedianFilter_Init(&g_filters[i]);
    }

    /* 5. 设置初始的状态默认参数，保证在数据未就绪时的安全性 */
    g_measure_data.vref_plus = 3.30f; /* 默认参考电压设定为典型值 3.3V */
    g_measure_data.vref_plus_ema = 3.30f; /* EMA 滤波器初始化为典型值 */
    g_measure_data.last_processed_group = 0xFFU; /* 初始为无效索引，强制第一次读取执行更新 */
    g_measure_data.data_valid = 0U;   /* 标识数据尚未就绪 */
    g_measure_data.update_cnt = 0U;

    /* 5.5. 启动常规通道转换前，必须执行一次硬件自校准以消除偏置误差(Offset Error) */
    /* 这是硬件级的源头治本之策，能够将偏置误差彻底校平，大幅提升内部通道采样精度 */
    if (HAL_ADCEx_Calibration_Start(gp_hadc, ADC_SINGLE_ENDED) != HAL_OK)
    {
        /* 校准失败防御性处理，确保不因校准卡死而导致系统彻底停机 */
        UART_Safe_Transmit("[System] WARNING: ADC hardware calibration timeout");
    }

    /* 6. 调用 HAL 启动常规通道的 DMA 循环缓冲采集 */
    HAL_StatusTypeDef status = HAL_ADC_Start_DMA(gp_hadc, (uint32_t *)g_adc_dma_buffer, ADC_DMA_BUFFER_SIZE);
    if (status != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  在主循环中周期性调用的模块核心状态自动机
 * @note   本函数会自动查询 DMA 的传输计数器，若有新数据写入，将立即提取并触发中值滤波和校准算法。
 */
void Measure_Update(void)
{
    uint8_t latest_completed_group = 0;
    
    /* 1. 防御性检查：确保 ADC 已正常初始化启动 */
    if (gp_hadc == NULL)
    {
        return;
    }

    /* 2. 获取最新完整写入的 DMA 组索引 */
    if (Measure_GetLatestCompletedGroup(&latest_completed_group) != HAL_OK)
    {
        /* 若第一组数据都尚未传输完毕，或者发生其他定位异常，直接安全返回 */
        return;
    }

    /* 3. 防御性重复过滤：若最新写入组已经被处理过，无需消耗 CPU 资源进行重复滤波计算 */
    if (latest_completed_group == g_measure_data.last_processed_group)
    {
        return;
    }

    /* 4. 数据更新与滤波计算 */
    /* 提取最新被 DMA 覆写的完整数据组偏移地址 */
    uint32_t group_offset = (uint32_t)latest_completed_group * ADC_CHANNEL_NUM;

    /* 将该数据组内各通道最新的 ADC 原始值依次压入各自的滑动中值滤波器窗口 */
    for (uint8_t i = 0; i < ADC_CHANNEL_NUM; i++)
    {
        MedianFilter_Insert(&g_filters[i], g_adc_dma_buffer[group_offset + i]);
    }

    /* 执行 5点中值滤波算法，提取抗噪声后的稳定通道原始 ADC 码值 */
    for (uint8_t i = 0; i < ADC_CHANNEL_NUM; i++)
    {
        g_measure_data.filtered_raw[i] = MedianFilter_GetMedian(&g_filters[i]);
    }

    /* 5. 实时 VREF+ 校准与 EMA 平滑 */
    uint16_t vrefint_raw = g_measure_data.filtered_raw[ADC_CH_INDEX_VREFINT];
    uint16_t vrefint_cal = *VREFINT_CAL_ADDR;

    /* 防御性编程：防止出现异常硬件下的除零异常错误 */
    if (vrefint_raw > 0)
    {
        /* 实时算得真实的 VREF+ (单位 V) */
        /* 计算公式: VREF+ = 3000mV * VREFINT_CAL / VREFINT_DATA */
        g_measure_data.vref_plus = 3.0f * ((float)vrefint_cal) / ((float)vrefint_raw);

        /* EMA 平滑滤波，减少噪声抖动 */
        g_measure_data.vref_plus_ema = VREF_EMA_ALPHA * g_measure_data.vref_plus
                                     + (1.0f - VREF_EMA_ALPHA) * g_measure_data.vref_plus_ema;
    }
    else
    {
        g_measure_data.vref_plus = 3.30f; /* 出现极端硬件异常时回退到 3.3V 典型值 */
        g_measure_data.vref_plus_ema = 3.30f;
    }

    /* 6. 高精度物理量转换与缩放映射 (使用 EMA 平滑后的 VREF+) */
    /*
     * 通道模拟输入电压公式: V_raw_V = (ADC_raw / 4095.0f) * vref_ema
     * 物理量计算规则：
     * - V1_IN 交流电压 (Rank 1): 0~2.5V 对应 0~311V.  物理电压 = V_raw_V * (311.0f / 2.5f) = V_raw_V * 124.4f
     * - V2_IN 交流电压 (Rank 2): 0~2.5V 对应 0~311V.  物理电压 = V_raw_V * (311.0f / 2.5f) = V_raw_V * 124.4f
     * - CO_OUT 直流电流 (Rank 3): 0~2.5V(2500mV) 对应 0~15A. 物理电流 = V_raw_V * (15.0f / 2.5f) = V_raw_V * 6.0f
     * - VO_OUT 直流电压 (Rank 4): 0~2.5V 对应 0~500V.  物理电压 = V_raw_V * (500.0f / 2.5f) = V_raw_V * 200.0f
     */
    float vref_ema = g_measure_data.vref_plus_ema; /* 使用 EMA 平滑后的 VREF */
    float v1_in_raw_v  = ((float)g_measure_data.filtered_raw[ADC_CH_INDEX_V1_IN] / 4095.0f) * vref_ema;
    float v2_in_raw_v  = ((float)g_measure_data.filtered_raw[ADC_CH_INDEX_V2_IN] / 4095.0f) * vref_ema;
    float co_out_raw_v = ((float)g_measure_data.filtered_raw[ADC_CH_INDEX_CO_OUT] / 4095.0f) * vref_ema;
    float vo_out_raw_v = ((float)g_measure_data.filtered_raw[ADC_CH_INDEX_VO_OUT] / 4095.0f) * vref_ema;

    g_measure_data.v1_in  = v1_in_raw_v * 124.4f;
    g_measure_data.v2_in  = v2_in_raw_v * 124.4f;
    g_measure_data.co_out = co_out_raw_v * 6.0f;
    g_measure_data.vo_out = vo_out_raw_v * 200.0f;

    /* 7. 片上温度传感器高精度计算 (使用 EMA 平滑后的 VREF) */
    uint16_t temp_raw = g_measure_data.filtered_raw[ADC_CH_INDEX_TEMP_SENSOR];
    float ts_cal1 = (float)(*TEMPSENSOR_CAL1_ADDR);
    float ts_cal2 = (float)(*TEMPSENSOR_CAL2_ADDR);
    float ts_diff = ts_cal2 - ts_cal1;

    /* 防御性编程：防止 ROM 标定码损坏造成的除零错误或计算异常 */
    if (ts_diff > 0.001f)
    {
        /* 将温度采样值等效校准到工厂标定时的 3.0V 下 */
        float ts_data_cal = (float)temp_raw * vref_ema / 3.0f;
        
        /* 采用官方温度换算公式，并跟随供应商头文件中的工厂标定温度端点。 */
        g_measure_data.temp_c = ((float)(TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) / ts_diff)
                              * (ts_data_cal - ts_cal1)
                              + (float)TEMPSENSOR_CAL1_TEMP;
    }
    else
    {
        g_measure_data.temp_c = -273.15f; /* 标定数据无效，返回绝对零度表示硬件/ROM异常 */
    }

    /* 8. 更新诊断指标与状态 */
    g_measure_data.last_processed_group = latest_completed_group;
    g_measure_data.update_cnt++;
    g_measure_data.data_valid = 1U; /* 数据就绪，可以安全供上层引用 */

}

/**
 * @brief  基于 DMA 计数器的免中断最新完成组无损定位函数
 * @param  group_index 输出参数，用于接收最新写入完整的组索引指针 (0 ~ 24)
 * @retval HAL_StatusTypeDef 返回 HAL_OK 表示成功定位，HAL_BUSY 表示第一组数据尚未传输完成，HAL_ERROR 表示句柄异常
 */
HAL_StatusTypeDef Measure_GetLatestCompletedGroup(uint8_t *group_index)
{
    /* 1. 防御性编程：强制指针校验 */
    if (group_index == NULL)
    {
        return HAL_ERROR;
    }
    if (gp_hadc == NULL || gp_hadc->DMA_Handle == NULL)
    {
        return HAL_ERROR;
    }

    /* 2. 读取 DMA 剩余传输寄存器 (CNDTR) */
    uint32_t remaining = __HAL_DMA_GET_COUNTER(gp_hadc->DMA_Handle);

    /* 防御性保护：防止意外越界 */
    if (remaining > ADC_DMA_BUFFER_SIZE)
    {
        remaining = ADC_DMA_BUFFER_SIZE;
    }

    /* 3. 精确计算当前 DMA 覆写的写指针偏移量 (0 ~ 149) */
    uint32_t write_ptr = (ADC_DMA_BUFFER_SIZE - remaining) % ADC_DMA_BUFFER_SIZE;

    /* 4. 计算最新完整写入的数据组索引 */
    /*
     * 每一组数据占用 6 个通道 (ADC_CHANNEL_NUM)
     * 当 write_ptr 在 0~5 时，表示正在写入第一组 (第 0 组)，那么上一次写满的完整组是最后一组 (第 24 组)。
     * 当 write_ptr 在 6~11 时，表示正在写入第二组 (第 1 组)，那么上一次写满的完整组是第一组 (第 0 组)。
     * 依此类推，公式为: latest_group = (write_ptr / 6 - 1 + 25) % 25
     */
    int32_t latest_group = ((int32_t)(write_ptr / ADC_CHANNEL_NUM) - 1 + (int32_t)ADC_SAMPLE_GROUPS) % (int32_t)ADC_SAMPLE_GROUPS;

    /* 5. 防御性初始化保护：防止开机未传输满第一组 (前 6 个数据) 时提取垃圾数据 */
    static uint8_t is_buf_ready = 0U;
    if (is_buf_ready == 0U)
    {
        if (write_ptr < ADC_CHANNEL_NUM)
        {
            *group_index = 0U;
            return HAL_BUSY; /* 缓冲区尚未就绪 */
        }
        else
        {
            is_buf_ready = 1U; /* 首次数据越过第一组阈值，后续判定数据一直有效 */
        }
    }

    /* 6. 成功返回最新完整组索引 */
    *group_index = (uint8_t)latest_group;
    return HAL_OK;
}

/**
 * @brief  获取最新测量的 V1_IN 交流电压值 (0~300V)
 */
float Measure_GetV1In(void)
{
    return g_measure_data.v1_in;
}

/**
 * @brief  获取最新测量的 V2_IN 交流电压值 (0~300V)
 */
float Measure_GetV2In(void)
{
    return g_measure_data.v2_in;
}

/**
 * @brief  获取最新测量的 CO_OUT 直流电流值 (0~10A)
 */
float Measure_GetCoOut(void)
{
    return g_measure_data.co_out;
}

/**
 * @brief  获取最新测量的 VO_OUT 直流电压值 (0~500V)
 */
float Measure_GetVoOut(void)
{
    return g_measure_data.vo_out;
}

/**
 * @brief  获取 MCU 内部芯片的实时温度 (°C)
 */
float Measure_GetTemp(void)
{
    return g_measure_data.temp_c;
}

/**
 * @brief  获取实时经过 VREFINT 校准后的 VREF+ 真实参考电压 (V)
 */
float Measure_GetVref(void)
{
    return g_measure_data.vref_plus;
}

/**
 * @brief  获取经过 EMA 平滑滤波后的 VREF+ 参考电压 (V)
 */
float Measure_GetVrefEma(void)
{
    return g_measure_data.vref_plus_ema;
}

/**
 * @brief  查询模块数据是否已经就绪有效 (是否已经经过首次滤波计算)
 */
uint8_t Measure_IsDataValid(void)
{
    return g_measure_data.data_valid;
}

/**
 * @brief  获取测量数据的全局只读指针
 */
const Measure_Data_t* Measure_GetDataPtr(void)
{
    return &g_measure_data;
}

/* 私有辅助函数实现 ----------------------------------------------------------*/

/**
 * @brief  初始化单个通道的中值滑动窗口状态
 */
static void MedianFilter_Init(MedianFilter_t *filter)
{
    if (filter != NULL)
    {
        memset(filter->window, 0, sizeof(filter->window));
        filter->index = 0U;
        filter->count = 0U;
    }
}

/**
 * @brief  向单个通道的滑动窗口插入最新的 ADC 原始数据
 */
static void MedianFilter_Insert(MedianFilter_t *filter, uint16_t val)
{
    if (filter != NULL)
    {
        filter->window[filter->index] = val;
        filter->index = (filter->index + 1) % MEDIAN_FILTER_WINDOW_SIZE;
        
        /* 维护滑动窗口内的有效采样个数，用于冷启动稳健计算 */
        if (filter->count < MEDIAN_FILTER_WINDOW_SIZE)
        {
            filter->count++;
        }
    }
}

/**
 * @brief  获取滑动窗口内历史数据进行排序后的中位数
 * @note   支持冷启动自适应排序。在数据还未装满 5 个时，以实际装入数量 N 排序取中值，大幅缩减系统初始化等待时间。
 *         5点冒泡排序速度极快，不额外增加 CPU 负担。
 */
static uint16_t MedianFilter_GetMedian(MedianFilter_t *filter)
{
    if (filter == NULL || filter->count == 0U)
    {
        return 0U;
    }

    uint16_t temp_buf[MEDIAN_FILTER_WINDOW_SIZE];
    uint8_t size = filter->count;

    /* 1. 拷贝当前有效的数据副本，防止滑动更新产生并发撕裂 */
    for (uint8_t i = 0; i < size; i++)
    {
        temp_buf[i] = filter->window[i];
    }

    /* 2. 轻量级冒泡排序 (针对 5 点以下的数据极度高效) */
    for (uint8_t i = 0; i < size - 1U; i++)
    {
        for (uint8_t j = 0; j < size - 1U - i; j++)
        {
            if (temp_buf[j] > temp_buf[j + 1U])
            {
                uint16_t swap = temp_buf[j];
                temp_buf[j] = temp_buf[j + 1U];
                temp_buf[j + 1U] = swap;
            }
        }
    }

    /* 3. 返回排好序的数组中位数 */
    return temp_buf[size / 2U];
}

/**
 * @brief  DMA半传输完成（Half Transfer, HT）中断回调函数
 * @param  hadc ADC句柄指针
 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        Measure_Update();
    }
}

/**
 * @brief  DMA传输完成（Transfer Complete, TC）中断回调函数
 * @param  hadc ADC句柄指针
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
    {
        Measure_Update();
    }
}

/**
 * @brief  高精度 ADC 6 通道功能测试函数
 * @note   采集并解析 6 个通道的最新数据，分别计算其在 MCU 引脚端的实际模拟电压值 (基于实时自校准的 VREF+ 参考电压)，
 *         并通过安全串口传输层 (uart_safe) 格式化打印输出，以便工程师进行硬件引脚级验证。
 */
void Measure_ADC_FunctionalTest(void)
{
    /* 1. 防御性检查：确保 ADC 数据模块已准备就绪且有最新数据 */
    if (gp_hadc == NULL)
    {
        UART_Safe_Transmit("[ADC_Test] ERROR: ADC hardware not initialized");
        return;
    }
    
    if (Measure_IsDataValid() == 0U)
    {
        UART_Safe_Transmit("[ADC_Test] WARNING: ADC data buffer not ready, waiting for first scan...");
        /* 尝试进行一次紧急更新，以防由于没有开启中断或其他原因无数据 */
        Measure_Update();
        if (Measure_IsDataValid() == 0U)
        {
            UART_Safe_Transmit("[ADC_Test] ERROR: ADC data still invalid after force update");
            return;
        }
    }

    /* 2. 获取实时高精度基准电压 VREF+ */
    float vref = g_measure_data.vref_plus;
    
    /* 3. 准备打印缓冲区 */
    char tx_buf[128];
    UART_Safe_Transmit("=================== ADC 6-Channel Functional Test ===================");
    
    /* 打印系统当前的真实 VREF+ 参考电压 */
    snprintf(tx_buf, sizeof(tx_buf), "System Base VREF+: %.4f V", vref);
    UART_Safe_Transmit(tx_buf);
    UART_Safe_Transmit("---------------------------------------------------------------------");

    /* 通道名称对照表 */
    const char* ch_names[ADC_CHANNEL_NUM] = {
        "V1_IN (AC Voltage 1)",
        "V2_IN (AC Voltage 2)",
        "CO_OUT (DC Current)",
        "VO_OUT (DC Voltage)",
        "TEMP_SENSOR (On-Chip)",
        "VREFINT (Int Ref Volt)"
    };

    /* 4. 遍历 6 个通道，计算原始值与引脚实际模拟电压值 */
    for (uint8_t i = 0; i < ADC_CHANNEL_NUM; i++)
    {
        uint16_t raw_val = g_measure_data.filtered_raw[i];
        
        /* 计算物理引脚处的模拟电压值 */
        /* 计算公式: V_pin = (Raw / 4095.0f) * VREF+ */
        float pin_volt = ((float)raw_val / 4095.0f) * vref;

        /* 对于 Rank 5 (内部温度) 和 Rank 6 (内部Vrefint) 可以附加物理换算结果，便于交叉验证 */
        if (i == ADC_CH_INDEX_TEMP_SENSOR)
        {
            snprintf(tx_buf, sizeof(tx_buf),
                     "Rank %u | ChIdx %u | %-22s | Raw: %4u | PinVolt: %.4f V (CalcTemp: %.2f C)",
                     (uint32_t)(i + 1U), (uint32_t)i, ch_names[i], raw_val, pin_volt, g_measure_data.temp_c);
        }
        else if (i == ADC_CH_INDEX_VREFINT)
        {
            snprintf(tx_buf, sizeof(tx_buf),
                     "Rank %u | ChIdx %u | %-22s | Raw: %4u | PinVolt: %.4f V (Theory: %.4f V)",
                     (uint32_t)(i + 1U), (uint32_t)i, ch_names[i], raw_val, pin_volt, 
                     ((float)(*VREFINT_CAL_ADDR) / 4095.0f) * 3.0f);
        }
        else
        {
            snprintf(tx_buf, sizeof(tx_buf),
                     "Rank %u | ChIdx %u | %-22s | Raw: %4u | PinVolt: %.4f V",
                     (uint32_t)(i + 1U), (uint32_t)i, ch_names[i], raw_val, pin_volt);
        }
        UART_Safe_Transmit(tx_buf);
    }
    
    UART_Safe_Transmit("=====================================================================");
}

