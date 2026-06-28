/**
  ******************************************************************************
  * @file           : dac_control.h
  * @brief          : Header for dac_control.c file.
  *                   This file contains the common defines and function
  *                   prototypes for controlling DAC1 for PFC current setting.
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

#ifndef __DAC_CONTROL_H__
#define __DAC_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dac.h" /* 继承 CubeMX 生成的 hdac1 句柄与 HAL 声明 */

/* Exported constants --------------------------------------------------------*/

/* DAC 外部参考电压 2.5V */
#define DAC_VREF_V                  2.5f

/* PFC电流与DAC值换算公式常量：DAC数字值 = PFC电流(A) * (4095 / 15) */
#define DAC_CURRENT_FACTOR          273.0f

/* 预设电流档位对应的 DAC 数字值 */
#define DAC_PFC_CURRENT_4A          1092U  /* 4A  -> 4 * 273.0 = 1092 */
#define DAC_PFC_CURRENT_3A          819U   /* 3A  -> 3 * 273.0 = 819 */
#define DAC_PFC_CURRENT_2A          546U   /* 2A  -> 2 * 273.0 = 546 */
#define DAC_PFC_CURRENT_05A         137U   /* 0.5A步进 -> 0.5 * 273.0 = 136.5 ≈ 137 */
#define DAC_PFC_CURRENT_005A        14U    /* 0.05A步进 -> 0.05 * 273.0 = 13.65 ≈ 14 */

/* 最大限制值 (12位DAC最大值 4095) */
#define DAC_MAX_DIGITAL_VALUE       4095U

/* VOC 模拟输出电压转换因子：DAC数字值 = 输出电压(V) * (4095 / 2.5) */
#define DAC_VOLTAGE_FACTOR          1638.0f

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief 启动 DAC1 Channel 1 并设置初始输出值
  * @param initial_value: 12位右对齐初始DAC数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_Start(uint32_t initial_value);

/**
  * @brief 停止 DAC1 Channel 1 的输出
  * @retval None
  */
void DAC_Control_Stop(void);

/**
  * @brief 直接通过 12 位数字值设置 DAC1 Channel 1 输出
  * @param value: 12位右对齐数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_SetValue(uint32_t value);

/**
  * @brief 启动 DAC1 Channel 2 并设置 VOC 初始输出值
  * @param initial_value: 12位右对齐初始DAC数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_StartVOC(uint32_t initial_value);

/**
  * @brief 停止 DAC1 Channel 2 的输出
  * @retval None
  */
void DAC_Control_StopVOC(void);

/**
  * @brief 直接通过 12 位数字值设置 DAC1 Channel 2 输出
  * @param value: 12位右对齐数字值 (0 ~ 4095)
  * @retval None
  */
void DAC_Control_SetVOCValue(uint32_t value);

/**
  * @brief 通过目标 PFC 电流 (安培) 来设置 DAC 输出电压
  * @param current_A: 目标PFC电流值，单位为安培 (A)
  * @retval None
  */
void DAC_Control_SetPfcCurrent(float current_A);

/**
  * @brief 获取当前保存在内存中的 PFC 设定目标电流 (A)
  * @retval 设定电流值，单位为安培 (A)
  */
float DAC_Control_GetPfcTargetCurrent(void);

/**
  * @brief 动态更新内存中的 PFC 设定目标电流 (A)
  * @param current_A: 设定目标电流，单位为安培 (A)，内部限制范围 0.0 ~ 15.0A
  * @retval None
  */
void DAC_Control_UpdatePfcTargetCurrent(float current_A);

/* VOC (PA5) 控制相关函数原型 */
void DAC_Control_VocStart(uint32_t initial_value);
void DAC_Control_VocStop(void);
void DAC_Control_VocSetValue(uint32_t value);
void DAC_Control_SetVocVoltage(float voltage_V);

#ifdef __cplusplus
}
#endif

#endif /* __DAC_CONTROL_H__ */
