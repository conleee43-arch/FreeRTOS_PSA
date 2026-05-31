/**
 * ******************************************************************************
 * @file    adc_calib.h
 * @author  Antigravity (精密仪器测量与控制算法专家)
 * @brief   高精度动态基准电压自校准与物理量解算驱动模块头文件。
 *          用于替代传统硬编码写死 3.3V 采样的落后方案，解决电源纹波与
 *          片上温度传感器温漂对精密采样的干扰。
 *          本模块符合 MISRA-C:2012 工业级规范。
 * @note    本模块为“纯计算算法库”，无任何具体的硬件及寄存器依赖，高移植性。
 * ******************************************************************************
 */

#ifndef ADC_CALIB_H
#define ADC_CALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ==========================================
 * 1. MCU 系列选择及出厂标定参数宏定义
 * ========================================== */

/* 
 * 移植说明：
 * 根据目标芯片系列，开启以下对应的 MCU_SERIES_xxx 宏定义。
 * 默认在当前 workspace 中激活 MCU_SERIES_STM32G4 宏。
 */
#ifndef MCU_SERIES_STM32G4
#define MCU_SERIES_STM32G4
#endif
/* #define MCU_SERIES_STM32F4 */
/* #define MCU_SERIES_STM32H7 */
/* #define MCU_SERIES_STM32L4 */

#if defined(MCU_SERIES_STM32G4)
    /* STM32G4 系列出厂标定值参数 (根据特定芯片型号校正) */
    #define ADC_CALIB_VREFINT_CAL_ADDR  (0x1FFF75AAUL)  /* 内部参考电压出厂标定值地址 */
    #define ADC_CALIB_TS_CAL1_ADDR      (0x1FFF75A8UL)  /* 30℃ 时温度传感器标定值地址 */
    #define ADC_CALIB_TS_CAL2_ADDR      (0x1FFF75CAUL)  /* 130℃ 时温度传感器标定值地址 */
    #define ADC_CALIB_VREFINT_CAL_V     (3.00f)         /* 内部参考电压标定时的工作电压 VDDA (V) */
    #define ADC_CALIB_TS_CAL_V          (3.00f)         /* 温度传感器标定时的工作电压 VDDA (V) */
    #define ADC_CALIB_TS_CAL1_TEMP      (30.0f)         /* TS_CAL1 对应的温度标定点 (℃) */
    #define ADC_CALIB_TS_CAL2_TEMP      (130.0f)        /* TS_CAL2 对应的温度标定点 (℃) */

#elif defined(MCU_SERIES_STM32F4)
    /* STM32F4 系列出厂标定值参数 */
    #define ADC_CALIB_VREFINT_CAL_ADDR  (0x1FFF7A2AUL)
    #define ADC_CALIB_TS_CAL1_ADDR      (0x1FFF7A2CUL)
    #define ADC_CALIB_TS_CAL2_ADDR      (0x1FFF7A2EUL)
    #define ADC_CALIB_VREFINT_CAL_V     (3.30f)
    #define ADC_CALIB_TS_CAL_V          (3.30f)
    #define ADC_CALIB_TS_CAL1_TEMP      (30.0f)
    #define ADC_CALIB_TS_CAL2_TEMP      (110.0f)

#elif defined(MCU_SERIES_STM32H7)
    /* STM32H7 系列出厂标定值参数 */
    #define ADC_CALIB_VREFINT_CAL_ADDR  (0x1FF1E860UL)
    #define ADC_CALIB_TS_CAL1_ADDR      (0x1FF1E820UL)
    #define ADC_CALIB_TS_CAL2_ADDR      (0x1FF1E840UL)
    #define ADC_CALIB_VREFINT_CAL_V     (3.30f)
    #define ADC_CALIB_TS_CAL_V          (3.30f)
    #define ADC_CALIB_TS_CAL1_TEMP      (30.0f)
    #define ADC_CALIB_TS_CAL2_TEMP      (110.0f)

#elif defined(MCU_SERIES_STM32L4)
    /* STM32L4 系列出厂标定值参数 */
    #define ADC_CALIB_VREFINT_CAL_ADDR  (0x1FFF75AAUL)
    #define ADC_CALIB_TS_CAL1_ADDR      (0x1FFF75A8UL)
    #define ADC_CALIB_TS_CAL2_ADDR      (0x1FFF75CAUL)
    #define ADC_CALIB_VREFINT_CAL_V     (3.00f)
    #define ADC_CALIB_TS_CAL_V          (3.00f)
    #define ADC_CALIB_TS_CAL1_TEMP      (30.0f)
    #define ADC_CALIB_TS_CAL2_TEMP      (110.0f)  /* 注意：部分 L4 芯片此处为 130.0f，请根据特定数据手册微调 */

#else
    #error "ADC_Calib: MCU Series must be defined for calibration registers addresses!"
#endif

/* ==========================================
 * 2. 状态与警报标志定义 (位掩码)
 * ========================================== */
#define ADC_CALIB_STATUS_OK             (0x00000000UL) /* 正常运行状态 */
#define ADC_CALIB_ERR_CONFIG            (0x00000001UL) /* 配置参数错误 (例如 alpha 范围或默认电压越界) */
#define ADC_CALIB_ERR_VREF_RAW_ZERO     (0x00000002UL) /* 输入的 VREFINT 原始采样值为 0 (可能通道配置错或短路) */
#define ADC_CALIB_ERR_VREF_CAL_INVALID  (0x00000004UL) /* ROM 中的 VREFINT 标定码无效 (如 0 或 0xFFFF) */
#define ADC_CALIB_ERR_TS_CAL_INVALID    (0x00000008UL) /* ROM 中的温度传感器标定码无效 (如 CAL1 >= CAL2 或全0/0xFFFF) */
#define ADC_CALIB_WARN_VREF_LIMIT       (0x00000010UL) /* 动态计算的 VREF+ 超出安全物理阈值，已执行强制安全限幅 */

/* ==========================================
 * 3. 核心数据结构定义
 * ========================================== */

/**
 * @brief 动态自校准模块配置参数结构体
 */
typedef struct {
    float alpha;            /**< EMA (指数移动平均) 滤波平滑系数，推荐范围: [0.01f, 0.2f]，默认 0.05f */
    float default_vref;     /**< 异常保护情况下的默认典型参考电压 (V)，例如 3.30f */
    float adc_max_code;     /**< ADC 的最大满量程数值精度等级分度 (例如 12 位 ADC 为 4095.0f, 16 位 ADC 为 65535.0f) */
    float vref_max_limit;   /**< 动态参考电压物理上限警戒值 (V)，如 3.60f，超出则触发报警并限幅 */
    float vref_min_limit;   /**< 动态参考电压物理下限警戒值 (V)，如 1.80f，低于则触发报警并限幅 */
} ADC_Calib_Config_t;

/**
 * @brief 动态自校准模块全局只读状态/输出数据结构体
 */
typedef struct {
    float vref_inst;        /**< 瞬时解算的 VREF+ 参考电压 (V) */
    float vref_ema;         /**< 指数平滑滤波后的 VREF_EMA 参考电压 (V) */
    float chip_temp;        /**< 解算出的芯片片上结温 (℃) */
    uint32_t status_flags;  /**< 模块状态与诊断警报位掩码 */
    uint8_t is_initialized; /**< 模块初始化就绪标志 (1:已就绪, 0:未初始化) */
} ADC_Calib_Data_t;

/* ==========================================
 * 4. 高精度自校准与物理量解算核心 API 声明
 * ========================================== */

/**
 * @brief       初始化高精度动态自校准模块
 * @param[in]   p_config: 指向配置参数结构体的常指针
 * @return      初始化结果状态位掩码 (ADC_CALIB_STATUS_OK 表示成功，其他表示有错误)
 */
uint32_t ADC_Calib_Init(const ADC_Calib_Config_t *p_config);

/**
 * @brief       周期性更新参考电压自校准与芯片结温解算
 * @details     此函数应由上层周期任务调度（如 10ms - 50ms 频率），传入最新的 VREFINT 
 *              和温度传感器原始 ADC 码值。该函数内部负责执行阶段一、阶段二及阶段四。
 * @param[in]   vrefint_raw: 当前周期采样的 VREFINT 通道 12位/16位 原始码值
 * @param[in]   temp_raw:    当前周期采样的芯片内部 TEMPSENSOR 通道原始码值
 * @return      当前模块最新诊断状态位掩码
 */
uint32_t ADC_Calib_Update(uint16_t vrefint_raw, uint16_t temp_raw);

/**
 * @brief       动态带入平滑后的 VREF_EMA 求解常规通道引脚真实模拟电压（阶段三）
 * @details     该函数支持任意常规通道。传入外部通道的稳定采样值即可得出精确物理电压。
 * @param[in]   pin_raw_adc: 外部常规模拟引脚采样得到的 ADC 码值
 * @return      解算后的引脚真实模拟电压值，单位：伏特 (V)
 */
float ADC_Calib_ConvertPin(uint16_t pin_raw_adc);

/**
 * @brief       安全获取最新的校准状态与解算结果
 * @param[out]  p_dest_data: 接收校准输出的数据结构体指针
 * @note        防止外部代码直接修改模块内部关键全局状态，通过只读保护接口提供副本
 */
void ADC_Calib_GetResults(ADC_Calib_Data_t *p_dest_data);

#ifdef __cplusplus
}
#endif

#endif /* ADC_CALIB_H */
