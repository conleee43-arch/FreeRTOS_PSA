/**
 ******************************************************************************
 * @file           : adc_measure.h
 * @brief          : 基于 STM32 HAL 库的高精度 ADC 采集与中值滤波处理算法模块
 * @author         : 资深驱动与算法工程师
 * @note           : 本模块专为 STM32G431 设计，结合定时器触发 + DMA 循环缓冲架构，
 *                   实现了免中断数据同步、5点中值滤波、实时 VREF+ 校准与物理量线性转换。
 ******************************************************************************
 */

#ifndef __ADC_MEASURE_H
#define __ADC_MEASURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件包含 ----------------------------------------------------------------*/
#include "stm32g4xx_hal.h"
#include "stm32g4xx_ll_adc.h"

/* 宏定义与硬件参数配置 --------------------------------------------------------*/

/**
 * @brief ADC 采集配置参数
 */
#define ADC_CHANNEL_NUM            (6U)   /* ADC常规转换通道总数 (Rank 1 ~ Rank 6) */
#define ADC_SAMPLE_GROUPS          (2U)   /* DMA双缓冲区中缓存的数据组数 */
#define ADC_DMA_BUFFER_SIZE        (ADC_CHANNEL_NUM * ADC_SAMPLE_GROUPS) /* DMA双缓冲区总大小 (12 个半字) */
#define MEDIAN_FILTER_WINDOW_SIZE  (5U)   /* 滑动中值滤波的窗口大小 (5点中值滤波) */

/**
 * @brief 各扫描通道 (Rank) 在 DMA 数据组内的索引定义
 */
#define ADC_CH_INDEX_V1_IN         (0U)   /* Rank 1: V1_IN 交流电压输入通道索引 */
#define ADC_CH_INDEX_V2_IN         (1U)   /* Rank 2: V2_IN 交流电压输入通道索引 */
#define ADC_CH_INDEX_CO_OUT        (2U)   /* Rank 3: CO_OUT 直流电流输入通道索引 */
#define ADC_CH_INDEX_VO_OUT        (3U)   /* Rank 4: VO_OUT 直流电压输入通道索引 */
#define ADC_CH_INDEX_TEMP_SENSOR   (4U)   /* Rank 5: MCU 内部温度传感器通道索引 */
#define ADC_CH_INDEX_VREFINT       (5U)   /* Rank 6: 内部 Vrefint 参考电压通道索引 */

/* 数据类型定义 --------------------------------------------------------------*/

/**
 * @brief 5点滑动中值滤波状态结构体
 */
typedef struct {
    uint16_t window[MEDIAN_FILTER_WINDOW_SIZE]; /* 滑动窗口数组 */
    uint8_t index;                              /* 当前即将写入的数组索引 */
    uint8_t count;                              /* 当前滑动窗口内有效的数据个数 (冷启动处理) */
} MedianFilter_t;

/**
 * @brief 高精度 ADC 测量模块状态与数据结构体
 */
typedef struct {
    uint16_t filtered_raw[ADC_CHANNEL_NUM];   /* 经过 5 点中值滤波后的各通道 ADC 原始值 */
    float vref_plus;                          /* 实时校准算得的真实 VREF+ 参考电压 (单位: V) */
    float vref_plus_ema;                      /* VREF+ EMA 平滑滤波后的值 */

    /* 物理量测量结果 */
    float v1_in;                              /* V1_IN 交流电压测量值 (单位: V) */
    float v2_in;                              /* V2_IN 交流电压测量值 (单位: V) */
    float co_out;                             /* CO_OUT 直流电流测量值 (单位: A) */
    float vo_out;                             /* VO_OUT 直流电压测量值 (单位: V) */
    float temp_c;                             /* MCU 内部片上温度 (单位: °C) */

    /* 诊断与同步参数 */
    uint32_t update_cnt;                      /* 数据包成功更新及解析计数器 */
    uint8_t last_processed_group;             /* 上一次成功处理的 DMA 组索引 (0 ~ 24) */
    uint8_t data_valid;                       /* 数据有效就绪标志 (1: 有效, 0: 未就绪) */
} Measure_Data_t;

/**
 * @brief VREF+ EMA 平滑滤波器配置
 *        EMA系数越小，滤波越平滑但响应越慢
 */
#define VREF_EMA_ALPHA     0.10f   /* EMA 系数: 0.01~0.20 之间较合适 */

/* 外部接口函数声明 ----------------------------------------------------------*/

/**
 * @brief  初始化 ADC 高精度测量模块并启动 DMA 循环传输
 * @param  hadc ADC 句柄指针，传入配置完毕的 ADC1 句柄
 * @retval HAL_StatusTypeDef 返回 HAL_OK 表示成功，HAL_ERROR 表示指针异常或启动失败
 */
HAL_StatusTypeDef Measure_Init(ADC_HandleTypeDef *hadc);

/**
 * @brief  在主循环或高频任务中周期性调用的模块核心状态自动机
 * @note   本函数会自动查询 DMA 的传输计数器，若有新数据写入，将立即提取并触发中值滤波和校准算法。
 */
void Measure_Update(void);

/**
 * @brief  基于 DMA 计数器的免中断最新完成组无损定位函数
 * @param  group_index 输出参数，用于接收最新写入完整的组索引指针 (0 ~ 24)
 * @retval HAL_StatusTypeDef 返回 HAL_OK 表示成功定位，HAL_BUSY 表示第一组数据尚未传输完成，HAL_ERROR 表示句柄异常
 */
HAL_StatusTypeDef Measure_GetLatestCompletedGroup(uint8_t *group_index);

/**
 * @brief  获取最新测量的 V1_IN 交流电压值 (0~311V)
 * @retval float 物理交流电压 (V)
 */
float Measure_GetV1In(void);

/**
 * @brief  获取最新测量的 V2_IN 交流电压值 (0~311V)
 * @retval float 物理交流电压 (V)
 */
float Measure_GetV2In(void);

/**
 * @brief  获取最新测量的 CO_OUT 直流电流值 (0~15A)
 * @retval float 物理直流电流 (A)
 */
float Measure_GetCoOut(void);

/**
 * @brief  获取最新测量的 VO_OUT 直流电压值 (0~500V)
 * @retval float 物理直流电压 (V)
 */
float Measure_GetVoOut(void);

/**
 * @brief  获取 MCU 内部芯片的实时温度 (°C)
 * @retval float 温度值 (°C)
 */
float Measure_GetTemp(void);

/**
 * @brief  获取实时经过 VREFINT 校准后的 VREF+ 真实参考电压 (V)
 * @retval float 真实 VREF+ 电压 (V)
 */
float Measure_GetVref(void);

/**
 * @brief  获取经过 EMA 平滑滤波后的 VREF+ 参考电压 (V)
 * @retval float EMA 平滑后的 VREF+ 电压 (V)
 */
float Measure_GetVrefEma(void);

/**
 * @brief  查询模块数据是否已经就绪有效 (是否已经经过首次滤波计算)
 * @retval uint8_t 1 表示数据就绪可用，0 表示数据尚不可用
 */
uint8_t Measure_IsDataValid(void);

/**
 * @brief  获取测量数据的全局只读指针
 * @retval const Measure_Data_t* 指向内部状态与物理量数据的只读指针
 */
const Measure_Data_t* Measure_GetDataPtr(void);

/**
 * @brief  高精度 ADC 6 通道功能测试函数
 * @note   采集并解析 6 个通道的最新数据，分别计算其在 MCU 引脚端的实际模拟电压值 (基于实时自校准的 VREF+ 参考电压)，
 *         并通过安全串口传输层 (uart_safe) 格式化打印输出，以便工程师进行硬件引脚级验证。
 */
void Measure_ADC_FunctionalTest(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_MEASURE_H */
