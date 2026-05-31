/**
 * ******************************************************************************
 * @file    adc_calib.c
 * @author  Antigravity (精密仪器测量与控制算法专家)
 * @brief   高精度动态基准电压自校准与物理量解算驱动模块源文件。
 *          提供完整的 EMA 指数平均、防御性零除验证、温度标定折算和
 *          双点温度插值的高性能计算实现。
 *          本模块符合 MISRA-C:2012 工业级规范。
 * ******************************************************************************
 */

#include "adc_calib.h"

/* 物理内存只读指针安全访问宏，符合 MISRA-C 指针定义规范 */
#ifndef HOST_UNIT_TEST
#define ADC_CALIB_READ_ROM16(addr)      (*(volatile const uint16_t *)(addr))
#else
extern uint16_t mock_rom_read(uint32_t addr);
#define ADC_CALIB_READ_ROM16(addr)      mock_rom_read(addr)
#endif

/* ==========================================
 * 1. 模块内私有静态全局变量 (符合 MISRA-C 封装性规范)
 * ========================================== */
static ADC_Calib_Config_t gs_config;             /* 模块内部私有配置副本 */
static ADC_Calib_Data_t   gs_calib_data;         /* 模块内部全局校准状态与解算结果 */

/* ==========================================
 * 2. 内部安全防御辅助函数声明与实现
 * ========================================== */

/**
 * @brief       校验读取的只读 ROM 标定码是否在合理物理区间
 * @param[in]   cal_val: 读取出来的16位标定码值
 * @param[in]   max_limit: 校验上限码值
 * @return      1: 标定码有效; 0: 标定码失真或无效
 */
static uint8_t ADC_Calib_ValidateCalCode(uint16_t cal_val, uint16_t max_limit)
{
    uint8_t is_valid = 1U;
    
    /* 
     * 在异常情况下 (如芯片损坏、出厂未烧录标定或读取失败)，
     * 读出来的数据可能为全 0 或 0xFFFF (Flash 未擦写状态)。
     * 同时，标定码应该处于合理的测量量程范围内。
     */
    if ((cal_val == 0U) || (cal_val == 0xFFFFU) || (cal_val > max_limit))
    {
        is_valid = 0U;
    }
    
    return is_valid;
}

/* ==========================================
 * 3. 核心 API 函数实现
 * ========================================== */

/**
 * @brief       初始化高精度动态自校准模块
 * @param[in]   p_config: 指向配置参数结构体的常指针
 * @return      初始化结果状态位掩码 (ADC_CALIB_STATUS_OK 表示成功，其他表示有错误)
 */
uint32_t ADC_Calib_Init(const ADC_Calib_Config_t *p_config)
{
    uint32_t init_status = ADC_CALIB_STATUS_OK;

    /* 1. 防御性空指针检查 */
    if (p_config == (const ADC_Calib_Config_t *)0)
    {
        init_status |= ADC_CALIB_ERR_CONFIG;
    }
    else
    {
        /* 2. 防御性配置参数合理性校验 */
        /* EMA 平滑系数必须在 (0.0f, 1.0f] 范围内 */
        if ((p_config->alpha <= 0.0f) || (p_config->alpha > 1.0f))
        {
            init_status |= ADC_CALIB_ERR_CONFIG;
        }
        /* ADC 最大满量程码值必须为正数且合理 */
        if (p_config->adc_max_code <= 0.0f)
        {
            init_status |= ADC_CALIB_ERR_CONFIG;
        }
        /* 默认电压和限制边界合理性判断 */
        if ((p_config->default_vref <= 0.0f) || 
            (p_config->vref_max_limit <= p_config->vref_min_limit) ||
            (p_config->default_vref > p_config->vref_max_limit) ||
            (p_config->default_vref < p_config->vref_min_limit))
        {
            init_status |= ADC_CALIB_ERR_CONFIG;
        }

        if (init_status == ADC_CALIB_STATUS_OK)
        {
            /* 3. 保存内部私有配置副本 */
            gs_config.alpha          = p_config->alpha;
            gs_config.default_vref   = p_config->default_vref;
            gs_config.adc_max_code   = p_config->adc_max_code;
            gs_config.vref_max_limit = p_config->vref_max_limit;
            gs_config.vref_min_limit = p_config->vref_min_limit;

            /* 4. 重置状态与解算结果 */
            gs_calib_data.vref_inst        = p_config->default_vref;
            gs_calib_data.vref_ema         = p_config->default_vref;
            gs_calib_data.chip_temp        = 25.0f; /* 初始默认常温 */
            gs_calib_data.status_flags     = ADC_CALIB_STATUS_OK;
            gs_calib_data.is_initialized   = 1U;    /* 标记初始化成功 */
            
            /* 5. 出厂标定码 ROM 物理存储做一次性校验，检测硬件是否支持标定数据 */
            const uint16_t vrefint_cal = ADC_CALIB_READ_ROM16(ADC_CALIB_VREFINT_CAL_ADDR);
            const uint16_t ts_cal1     = ADC_CALIB_READ_ROM16(ADC_CALIB_TS_CAL1_ADDR);
            const uint16_t ts_cal2     = ADC_CALIB_READ_ROM16(ADC_CALIB_TS_CAL2_ADDR);
            
            /* 对于 12/16 位 ADC，一般标定码物理上限设为 65535U */
            if (ADC_Calib_ValidateCalCode(vrefint_cal, 65535U) == 0U)
            {
                gs_calib_data.status_flags |= ADC_CALIB_ERR_VREF_CAL_INVALID;
            }
            if ((ADC_Calib_ValidateCalCode(ts_cal1, 65535U) == 0U) || 
                (ADC_Calib_ValidateCalCode(ts_cal2, 65535U) == 0U) ||
                (ts_cal2 <= ts_cal1))
            {
                gs_calib_data.status_flags |= ADC_CALIB_ERR_TS_CAL_INVALID;
            }
        }
    }

    return (init_status | gs_calib_data.status_flags);
}

/**
 * @brief       周期性更新参考电压自校准与芯片结温解算
 * @param[in]   vrefint_raw: 当前周期采样的 VREFINT 通道原始码值
 * @param[in]   temp_raw:    当前周期采样的芯片内部 TEMPSENSOR 通道原始码值
 * @return      当前模块最新诊断状态位掩码
 */
uint32_t ADC_Calib_Update(uint16_t vrefint_raw, uint16_t temp_raw)
{
    /* 防御性保护：确保模块已正确初始化 */
    if (gs_calib_data.is_initialized == 0U)
    {
        return (ADC_CALIB_ERR_CONFIG);
    }

    /* 每次周期更新清除上周期的瞬时报警和错误，但保留初始化检测出的 ROM 错误 */
    gs_calib_data.status_flags &= (ADC_CALIB_ERR_VREF_CAL_INVALID | ADC_CALIB_ERR_TS_CAL_INVALID);

    /* ==========================================
     * 阶段一：动态解算瞬时 VREF+ 参考电压
     * ========================================== */
    float vref_new = gs_config.default_vref;
    
    /* 防零除保护与 ROM 标定码合理性校验 */
    if (vrefint_raw == 0U)
    {
        gs_calib_data.status_flags |= ADC_CALIB_ERR_VREF_RAW_ZERO;
        vref_new = gs_config.default_vref;
    }
    else if ((gs_calib_data.status_flags & ADC_CALIB_ERR_VREF_CAL_INVALID) != 0UL)
    {
        /* ROM 标定码损坏，无法进行科学自校准，强退回安全默认电压 */
        vref_new = gs_config.default_vref;
    }
    else
    {
        /* 从只读 ROM 中安全读取标定码并实施浮点强转 */
        const uint16_t vrefint_cal = ADC_CALIB_READ_ROM16(ADC_CALIB_VREFINT_CAL_ADDR);
        
        /* 核心等比例换算公式：Vref_plus = VREFINT_CAL_V * (VREFINT_CAL / vrefint_raw) */
        vref_new = ADC_CALIB_VREFINT_CAL_V * ((float)vrefint_cal / (float)vrefint_raw);
        
        /* 物理阈值防御性保护 (防突发性瞬态剧烈噪声) */
        if (vref_new > gs_config.vref_max_limit)
        {
            vref_new = gs_config.vref_max_limit;
            gs_calib_data.status_flags |= ADC_CALIB_WARN_VREF_LIMIT;
        }
        else if (vref_new < gs_config.vref_min_limit)
        {
            vref_new = gs_config.vref_min_limit;
            gs_calib_data.status_flags |= ADC_CALIB_WARN_VREF_LIMIT;
        }
        else
        {
            /* 正常区间 */
        }
    }

    gs_calib_data.vref_inst = vref_new;

    /* ==========================================
     * 阶段二：基准电压 EMA 指数平滑滤波
     * ========================================== */
    /* 
     * 冷启动特判保护机制：
     * 为避免首次上电时 EMA 平滑滤波需要长时间爬升，
     * 当处于初装或 vref_ema 接近默认零电平时，直接强制同步第一周期数值。
     */
    if (gs_calib_data.vref_ema <= 0.1f)
    {
        gs_calib_data.vref_ema = vref_new;
    }
    else
    {
        /* 核心 EMA 滤波公式：VREF_EMA = α * VREF_new + (1 - α) * VREF_EMA_old */
        gs_calib_data.vref_ema = (gs_config.alpha * vref_new) + 
                                 ((1.0f - gs_config.alpha) * gs_calib_data.vref_ema);
    }

    /* ==========================================
     * 阶段四：片上温度传感器 3.0V 标准等效校准与双点插值
     * ========================================== */
    if ((gs_calib_data.status_flags & ADC_CALIB_ERR_TS_CAL_INVALID) != 0UL)
    {
        /* 硬件标定数据缺失，温度解算直接退回绝对零度故障状态 */
        gs_calib_data.chip_temp = -273.15f;
    }
    else
    {
        const uint16_t ts_cal1 = ADC_CALIB_READ_ROM16(ADC_CALIB_TS_CAL1_ADDR);
        const uint16_t ts_cal2 = ADC_CALIB_READ_ROM16(ADC_CALIB_TS_CAL2_ADDR);

        /* 分母防御性保护：防零除 */
        if (ts_cal2 <= ts_cal1)
        {
            gs_calib_data.status_flags |= ADC_CALIB_ERR_TS_CAL_INVALID;
            gs_calib_data.chip_temp = -273.15f;
        }
        else
        {
            /* 
             * 4.1：将当前温度传感器原始采样值等效转换到标定参考电压下的等效码值
             * ts_data_cal = temp_raw * (VREF_EMA / TS_CAL_V) 
             */
            const float ts_data_cal = (float)temp_raw * (gs_calib_data.vref_ema / ADC_CALIB_TS_CAL_V);

            /* 
             * 4.2：利用双点线性插值，精确解算摄氏度 (℃)
             * Temp = [(CAL2_TEMP - CAL1_TEMP) / (TS_CAL2 - TS_CAL1)] * (ts_data_cal - TS_CAL1) + CAL1_TEMP 
             */
            const float temp_diff = ADC_CALIB_TS_CAL2_TEMP - ADC_CALIB_TS_CAL1_TEMP;
            const float cal_diff  = (float)ts_cal2 - (float)ts_cal1;
            
            const float temp_value = (temp_diff / cal_diff) * (ts_data_cal - (float)ts_cal1) + ADC_CALIB_TS_CAL1_TEMP;
            
            /* 限制解算结温的合理极限范围 [-40℃, 150℃]，防极值跳变异常 */
            if (temp_value > 150.0f)
            {
                gs_calib_data.chip_temp = 150.0f;
            }
            else if (temp_value < -40.0f)
            {
                gs_calib_data.chip_temp = -40.0f;
            }
            else
            {
                gs_calib_data.chip_temp = temp_value;
            }
        }
    }

    return gs_calib_data.status_flags;
}

/**
 * @brief       动态带入平滑后的 VREF_EMA 求解常规通道引脚真实模拟电压（阶段三）
 * @param[in]   pin_raw_adc: 外部常规模拟引脚采样得到的 ADC 码值
 * @return      解算后的引脚真实模拟电压值，单位：伏特 (V)
 */
float ADC_Calib_ConvertPin(uint16_t pin_raw_adc)
{
    float pin_voltage = 0.0f;

    /* 防御性保护：若未初始化，强行按 0V 输出并拒绝转换 */
    if (gs_calib_data.is_initialized != 0U)
    {
        /* 
         * 核心解算公式：Vpin = (pin_raw_adc / adc_max_code) * VREF_EMA 
         * 浮点常量除法确保所有类型强转与精度的严谨。
         */
        pin_voltage = ((float)pin_raw_adc / gs_config.adc_max_code) * gs_calib_data.vref_ema;
    }

    return pin_voltage;
}

/**
 * @brief       安全获取最新的校准状态与解算结果
 * @param[out]  p_dest_data: 接收校准输出的数据结构体指针
 */
void ADC_Calib_GetResults(ADC_Calib_Data_t *p_dest_data)
{
    /* 防御性保护 */
    if (p_dest_data != (ADC_Calib_Data_t *)0)
    {
        /* 拷贝内部私有结构体，完美遵循封装安全法则，保护全局私有变量不被外部篡改 */
        p_dest_data->vref_inst      = gs_calib_data.vref_inst;
        p_dest_data->vref_ema       = gs_calib_data.vref_ema;
        p_dest_data->chip_temp      = gs_calib_data.chip_temp;
        p_dest_data->status_flags   = gs_calib_data.status_flags;
        p_dest_data->is_initialized = gs_calib_data.is_initialized;
    }
}
