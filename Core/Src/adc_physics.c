/**
 * ******************************************************************************
 * @file    adc_physics.h
 * @author  Antigravity (精密仪器测量与控制算法专家)
 * @brief   高精度多通道 ADC 原始数据与物理量转换算法模块源文件。
 *          建立“数码 -> 模拟引脚电压 -> 真实物理量”的严密二阶解算链路，
 *          支持双向偏置传感器，并将前级电路调理参数与核心解算逻辑彻底解耦。
 *          本模块符合 MISRA-C:2012 工业级规范。
 * ******************************************************************************
 */

#include "adc_physics.h"

/* ==========================================
 * 1. 静态属性表与运行时状态数组设计 (Table-Driven)
 * ========================================== */
static Physical_Channel_t s_channels[PHYS_CH_NUM] = {
    [PHYS_CH_V1_IN] = {
        .config = {
            .physical_max    = 311.0f,  /* 满量程交流电压 V1 峰值为 311.0V */
            .pin_max_voltage = 2.50f,   /* 311.0V 对应单片机引脚输入为 2.5V */
            .offset_value    = 0.0f     /* 单向无偏置传感器 */
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_V2_IN] = {
        .config = {
            .physical_max    = 311.0f,  /* 满量程交流电压 V2 峰值为 311.0V */
            .pin_max_voltage = 2.50f,   /* 311.0V 对应单片机引脚输入为 2.5V */
            .offset_value    = 0.0f     /* 单向无偏置传感器 */
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_CO_OUT] = {
        .config = {
            .physical_max    = 15.0f,   /* 直流满量程电流为 15.0A */
            .pin_max_voltage = 2.50f,   /* 15.0A 对应单片机引脚输入为 2.5V */
            .offset_value    = 0.0f     /* 暂定单向直流传感器 */
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    },
    
    [PHYS_CH_VO_OUT] = {
        .config = {
            .physical_max    = 500.0f,  /* 直流满量程母线电压为 500.0V */
            .pin_max_voltage = 2.50f,   /* 500.0V 对应单片机引脚输入为 2.5V */
            .offset_value    = 0.0f     /* 单向直流电压分压采集 */
        },
        .adc_raw_median      = 0U,
        .pin_voltage         = 0.0f,
        .physical_value      = 0.0f,
        .is_error            = 0U
    }
};

/* ==========================================
 * 2. API 函数接口实现
 * ========================================== */

/**
 * @brief       初始化物理通道转换模块配置表
 */
void Physics_Init(void)
{
    uint32_t i;
    
    for (i = 0UL; i < (uint32_t)PHYS_CH_NUM; i++)
    {
        s_channels[i].adc_raw_median = 0U;
        s_channels[i].pin_voltage    = 0.0f;
        s_channels[i].physical_value = 0.0f;
        s_channels[i].is_error       = 0U;
        
        /* 
         * 额外做一次静态初始化安全性验证。
         * 如果用户配置的前级满量程电压分量异常 (如配置为负数或0)，
         * 自动将其置位错误，防止系统后续进入异常除法。
         */
        if (s_channels[i].config.pin_max_voltage <= 0.0f)
        {
            s_channels[i].is_error = 1U;
        }
    }
}

/**
 * @brief       周期性更新单个通道的原始 ADC 码值
 */
void Physics_UpdateChannelRaw(Physical_Channel_ID_t ch_id, uint16_t adc_raw_median)
{
    /* 边界防卫检查，符合 MISRA-C 安全数组索引法则 */
    if (ch_id < PHYS_CH_NUM)
    {
        s_channels[ch_id].adc_raw_median = adc_raw_median;
    }
}

/**
 * @brief       批量解算转换所有配置注册的通道物理量 (二阶解算链路核心)
 */
void Physics_ProcessAll(float current_vref_ema)
{
    uint32_t i;
    
    for (i = 0UL; i < (uint32_t)PHYS_CH_NUM; i++)
    {
        Physical_Channel_t *const p_ch = &s_channels[i];
        
        /* 
         * ==========================================
         * 2.1 第一防零除屏障：实时工作参考电压有效性校验
         * ==========================================
         * 如果外界输入的参考电压失真 (<= 0.0f)，引脚输入被视作 0.0f 
         * 并强行关闭二次解算链路，防止由于电源故障级联引起的失控计算。
         */
        if (current_vref_ema <= 0.0f)
        {
            p_ch->pin_voltage    = 0.0f;
            p_ch->physical_value = 0.0f;
            p_ch->is_error       = 1U;
        }
        else
        {
            /* 
             * ==========================================
             * 第一阶解算：数码 -> 模拟引脚电压 (V)
             * 公式：V_pin = (adc_raw_median / ADC_PHYS_MAX_CODE) * VREF_ema 
             * ==========================================
             */
            p_ch->pin_voltage = ((float)p_ch->adc_raw_median / ADC_PHYS_MAX_CODE) * current_vref_ema;
            
            /* 
             * ==========================================
             * 2.2 第二防零除屏障：前级调理比例因子有效性校验
             * ==========================================
             * 强制验证前级引脚满量程模拟电压的分母配置。如果前级满量程模拟电压 pin_max_voltage <= 0.0f，
             * 表明硬件配置数据遭到逻辑错误或破坏，直接归零通道物理值并告警，阻断零除崩溃发生。
             */
            if (p_ch->config.pin_max_voltage <= 0.0f)
            {
                p_ch->physical_value = 0.0f;
                p_ch->is_error       = 1U;
            }
            else
            {
                /* 
                 * ==========================================
                 * 第二阶解算：模拟引脚电压 -> 物理量 (A, V等)
                 * 公式：Physical_Value = V_pin * (physical_max / pin_max_voltage) - offset_value 
                 * ==========================================
                 */
                const float scale_factor = p_ch->config.physical_max / p_ch->config.pin_max_voltage;
                p_ch->physical_value = (p_ch->pin_voltage * scale_factor) - p_ch->config.offset_value;
                p_ch->is_error       = 0U; /* 解算链路完整且正常 */
            }
        }
    }
}

/**
 * @brief       安全获取指定通道的完整运行时校准与解算结果
 */
void Physics_GetChannelResults(Physical_Channel_ID_t ch_id, Physical_Channel_t *p_dest)
{
    /* 防御性空指针与边界隔离检查 */
    if ((p_dest != (Physical_Channel_t *)0) && (ch_id < PHYS_CH_NUM))
    {
        /* 采用只读拷贝，断绝外部直接改写核心状态控制数组的可能 */
        p_dest->config.physical_max    = s_channels[ch_id].config.physical_max;
        p_dest->config.pin_max_voltage = s_channels[ch_id].config.pin_max_voltage;
        p_dest->config.offset_value    = s_channels[ch_id].config.offset_value;
        p_dest->adc_raw_median         = s_channels[ch_id].adc_raw_median;
        p_dest->pin_voltage            = s_channels[ch_id].pin_voltage;
        p_dest->physical_value         = s_channels[ch_id].physical_value;
        p_dest->is_error               = s_channels[ch_id].is_error;
    }
}

/**
 * @brief       安全获取指定通道解算还原后的浮点物理数值
 */
float Physics_GetPhysicalValue(Physical_Channel_ID_t ch_id)
{
    float val = 0.0f;
    
    if (ch_id < PHYS_CH_NUM)
    {
        const Physical_Channel_t *const p_ch = &s_channels[ch_id];
        
        /* 仅在通道状态无故障标志时输出结果，实现自诊断隔离保护 */
        if (p_ch->is_error == 0U)
        {
            val = p_ch->physical_value;
        }
    }
    
    return val;
}
