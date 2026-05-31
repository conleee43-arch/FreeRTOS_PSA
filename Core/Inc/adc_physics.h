/**
 * ******************************************************************************
 * @file    adc_physics.h
 * @author  Antigravity (精密仪器测量与控制算法专家)
 * @brief   高精度多通道 ADC 原始数据与物理量转换算法模块头文件。
 *          建立“数码 -> 模拟引脚电压 -> 真实物理量”的严密二阶解算链路，
 *          支持双向偏置传感器，并将前级电路调理参数与核心解算逻辑彻底解耦。
 *          本模块符合 MISRA-C:2012 工业级规范。
 * ******************************************************************************
 */

#ifndef ADC_PHYSICS_H
#define ADC_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ADC 精度配置宏，默认支持 12 位精度等级 */
#ifndef ADC_PHYS_MAX_CODE
#define ADC_PHYS_MAX_CODE               (4095.0f) /* 12位最大码值 */
#endif

/* ==========================================
 * 1. 模拟通道 ID 枚举定义 (高扩展性设计)
 * ========================================== */
typedef enum {
    PHYS_CH_V1_IN = 0,                  /**< 交流电压 V1 输入通道 (满量程：311V) */
    PHYS_CH_V2_IN,                      /**< 交流电压 V2 输入通道 (满量程：311V) */
    PHYS_CH_CO_OUT,                     /**< 直流电流 CO 输出通道 (满量程：15A) */
    PHYS_CH_VO_OUT,                     /**< 直流电压 VO 输出通道 (满量程：500V) */
    
    /* 用户可在此行之上随意插入或追加新的物理量通道，实现零代码侵入式的通道扩展 */
    
    PHYS_CH_NUM                         /**< 系统注册的物理量通道总数 */
} Physical_Channel_ID_t;

/* ==========================================
 * 2. 核心数据结构定义 (符合 MISRA-C:2012 封装规范)
 * ========================================== */

/**
 * @brief 模拟通道前级调理硬件电路配置参数结构体
 */
typedef struct {
    float physical_max;       /**< 满量程物理量值 (如：311.0f V, 15.0f A) */
    float pin_max_voltage;    /**< 满量程物理量对应的 MCU 引脚端模拟电压 (V)，例如 2.50f V */
    float offset_value;       /**< 传感器物理零点偏置量 (用于支持偏置式双向传感器，如零点引脚输出为 1.25V 的情况) */
} Physical_Channel_Cfg_t;

/**
 * @brief 模拟通道运行时状态与解算输出结构体
 */
typedef struct {
    Physical_Channel_Cfg_t config;      /**< 通道硬件前级调理配置 */
    volatile uint16_t adc_raw_median;   /**< 经过滤波处理后的最新 12 位原始采样码 */
    float pin_voltage;                  /**< 运行中解算还原的引脚端真实模拟电压值 (V) */
    float physical_value;               /**< 运行中解算还原的真实物理量值 (带物理单位 V, A等) */
    uint8_t is_error;                   /**< 该通道诊断状态标志 (1:发生防零除错误/配置错误, 0:运行正常) */
} Physical_Channel_t;

/* ==========================================
 * 3. 核心 API 接口声明
 * ========================================== */

/**
 * @brief       初始化物理通道转换模块配置表
 * @details     该函数内部负责加载 static 静态硬件属性表，并复位所有通道运行时数据。
 */
void Physics_Init(void);

/**
 * @brief       周期性更新单个通道的原始 ADC 码值
 * @param[in]   ch_id: 目标物理量通道 ID
 * @param[in]   adc_raw_median: 该通道经过滑动中值等滤波处理后的 12 位稳定采样码
 */
void Physics_UpdateChannelRaw(Physical_Channel_ID_t ch_id, uint16_t adc_raw_median);

/**
 * @brief       批量解算转换所有配置注册的通道物理量 (二阶解算链路核心)
 * @details     此函数应周期性在主控制循环或 FreeRTOS 任务中被调用，
 *              动态带入最新经 EMA 滤波后的基准电压，驱动所有注册通道的高精度二阶转换。
 * @param[in]   current_vref_ema: 最新经自校准平滑的 VREF_EMA 参考电压 (V)
 */
void Physics_ProcessAll(float current_vref_ema);

/**
 * @brief       安全获取指定通道的完整运行时校准与解算结果
 * @param[in]   ch_id: 目标物理量通道 ID
 * @param[out]  p_dest: 接收运行时状态的结构体指针
 * @note        采用只读拷贝机制，完美隔离内部静态全局数据，防外部非法篡改
 */
void Physics_GetChannelResults(Physical_Channel_ID_t ch_id, Physical_Channel_t *p_dest);

/**
 * @brief       安全获取指定通道解算还原后的浮点物理数值
 * @param[in]   ch_id: 目标物理量通道 ID
 * @return      真实物理量值 (带物理单位，如 220.5f V, 8.2f A等)，若通道错误则输出 0.0f
 */
float Physics_GetPhysicalValue(Physical_Channel_ID_t ch_id);

#ifdef __cplusplus
}
#endif

#endif /* ADC_PHYSICS_H */
