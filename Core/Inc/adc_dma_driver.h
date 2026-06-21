/**
 * ******************************************************************************
 * @file    adc_dma_driver.h
 * @author  Antigravity (精密仪器测量与控制算法专家)
 * @brief   高精度多通道 ADC DMA 物理量还原与控制驱动模块头文件。
 *          提供完整的 Measure_ 前缀安全无锁 API 声明，支持乒乓双缓冲中断处理。
 *          本模块符合 MISRA-C:2012 工业级规范。
 * ******************************************************************************
 */

#ifndef ADC_DMA_DRIVER_H
#define ADC_DMA_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ==========================================
 * 1. 核心控制与配置宏定义
 * ========================================== */
#define ADC_DRV_CHANNEL_CNT         5U      /* 规则扫描通道总数 */
#define ADC_DRV_BUFFER_SLOTS        25U     /* 环形缓冲区 Slot 组数 */
#define ADC_DRV_DMA_BUF_SIZE        (ADC_DRV_CHANNEL_CNT * ADC_DRV_BUFFER_SLOTS) /* 缓冲区半字总长度: 150 */

/* ==========================================
 * 2. 核心 API 接口声明
 * ========================================== */

/**
 * @brief       初始化物理测量模块与启动 ADC DMA 传输 (阶段一)
 * @details     校验 ADC 句柄和 DMA 关联性。在启动 DMA 搬运前，先调用 
 *              HAL_ADCEx_Calibration_Start 进行硬件自校准，然后开启 DMA 循环模式转换。
 * @param[in]   hadc: 绑定的 ADC 实例句柄指针 (必须指向已使能的外设地址)
 * @return      HAL_OK: 初始化及自校准成功; HAL_ERROR/HAL_BUSY: 硬件配置或自检失败
 */
HAL_StatusTypeDef Measure_Init(ADC_HandleTypeDef *hadc);

/**
 * @brief       免锁、免中断的 DMA 最新数据写指针追踪与物理量解算函数 (阶段二)
 * @details     此函数既可由周期性大循环任务高频 Poll 调用，也可作为乒乓中断内的
 *              核心数据还原引擎，能够自动追踪最新写入的 Slot 并驱动二阶算法解算链。
 */
void Measure_Update(void);

/**
 * @brief       安全获取交流输入电压 1 的物理还原值
 * @return      V1 交流电压值，单位：伏特 (V)
 */
float Measure_GetV1In(void);

/**
 * @brief       安全获取交流输入电压 2 的物理还原值
 * @return      V2 交流电压值，单位：伏特 (V)
 */
float Measure_GetV2In(void);

/**
 * @brief       安全获取直流输出电流的物理还原值
 * @return      CO 直流电流值，单位：安培 (A)
 */
float Measure_GetCoOut(void);

/**
 * @brief       安全获取直流母线输出电压的物理还原值
 * @return      VO 直流电压值，单位：伏特 (V)
 */
float Measure_GetVoOut(void);

/**
 * @brief       安全获取最新动态校准且经过 EMA 滤波平滑的真实供电参考基准电压 VREF+
 * @return      供电基准电压，单位：伏特 (V)
 */
float Measure_GetVref(void);

/**
 * @brief       安全获取经过出厂双点插值与 VREF 波动补偿后的芯片实时结温
 * @return      芯片结温，单位：摄氏度 (℃)
 */
float Measure_GetTemp(void);

/**
 * @brief       安全查询测量驱动是否已完成冷启动并处于正常工作状态
 * @retval      true: 数据首包填充完成且算法引擎已就绪; false: 数据尚未填充就绪
 */
bool Measure_IsReady(void);

/**
 * @brief       安全获取指定通道最新经过中值滤波后的原始 ADC 采样码 (12位)
 * @param[in]   channel_idx: 通道索引 (0 -> Rank 1, ..., 5 -> Rank 6)
 * @return      12位原始 ADC 采样码值，若通道索引越界则返回 0
 */
uint16_t Measure_GetRawCode(uint8_t channel_idx);

#ifdef __cplusplus
}
#endif

#endif /* ADC_DMA_DRIVER_H */
