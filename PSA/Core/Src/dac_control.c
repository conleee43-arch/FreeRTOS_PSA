/**
  ******************************************************************************
  * @file           : dac_control.c
  * @brief          : Source for dac_control.h file.
  *                   This file contains the implementation of functions for
  *                   controlling DAC1 for PFC current setting.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026. All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "dac_control.h"

/* Private functions ---------------------------------------------------------*/

/**
  * @brief 启动 DAC1 Channel 1 并设置初始输出值
  * @param initial_value: 12位右对齐初始DAC数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_Start(uint32_t initial_value)
{
    /* 限制最大输入数字值 */
    if (initial_value > DAC_MAX_DIGITAL_VALUE)
    {
        initial_value = DAC_MAX_DIGITAL_VALUE;
    }
    
    /* 1. 设置通道1初始输出数据 */
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, initial_value);
    
    /* 2. 开启 DAC1 的通道 1 */
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
}

/**
  * @brief 停止 DAC1 Channel 1 的输出
  * @retval None
  */
void DAC_Control_Stop(void)
{
    HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
}

/**
  * @brief 直接通过 12 位数字值设置 DAC1 Channel 1 输出
  * @param value: 12位右对齐数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_SetValue(uint32_t value)
{
    /* 软限幅防止超出12位DAC寄存器边界 */
    if (value > DAC_MAX_DIGITAL_VALUE)
    {
        value = DAC_MAX_DIGITAL_VALUE;
    }
    
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, value);
}

static float s_pfc_target_current = 4.00f;

/**
  * @brief 通过目标 PFC 电流 (安培) 来设置 DAC 输出电压
  * @param current_A: 目标PFC电流值，单位为安培 (A)
  * @retval None
  */
void DAC_Control_SetPfcCurrent(float current_A)
{
    /* 如果输入电流小于0，强制截断为0 */
    if (current_A < 0.0f)
    {
        current_A = 0.0f;
    }
    
    /* 
     * 计算寄存器数字值：电流 * 409.5
     * 加上 0.5f 实现类似于四舍五入的浮点到整型转换效果
     */
    float val_f = (current_A * DAC_CURRENT_FACTOR) + 0.5f;
    uint32_t val_u = (uint32_t)val_f;
    
    /* 调用直接写入接口（内部具有最大值边界软限幅） */
    DAC_Control_SetValue(val_u);
}

/**
  * @brief 获取当前保存在内存中的 PFC 设定目标电流 (A)
  * @retval 设定电流值，单位为安培 (A)
  */
float DAC_Control_GetPfcTargetCurrent(void)
{
    return s_pfc_target_current;
}

/**
  * @brief 动态更新内存中的 PFC 设定目标电流 (A)
  * @param current_A: 设定目标电流，单位为安培 (A)，内部限制范围 0.0 ~ 10.0A
  * @retval None
  */
void DAC_Control_UpdatePfcTargetCurrent(float current_A)
{
    if (current_A < 0.0f)
    {
        current_A = 0.0f;
    }
    if (current_A > 10.0f)
    {
        current_A = 10.0f;
    }
    s_pfc_target_current = current_A;
}
