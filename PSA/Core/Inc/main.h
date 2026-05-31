/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define V1_IN_Pin GPIO_PIN_0
#define V1_IN_GPIO_Port GPIOA
#define V2_IN_Pin GPIO_PIN_1
#define V2_IN_GPIO_Port GPIOA
#define CO_OUT_Pin GPIO_PIN_2
#define CO_OUT_GPIO_Port GPIOA
#define VO_OUT_Pin GPIO_PIN_3
#define VO_OUT_GPIO_Port GPIOA
#define IOC_Pin GPIO_PIN_4
#define IOC_GPIO_Port GPIOA
#define OF_EN_Pin GPIO_PIN_0
#define OF_EN_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* ============================================================================
 * 系统配置宏 - 可根据需要调整
 * ============================================================================ */

/**
 * @brief 日志输出级别
 *        0 = DEBUG (详细模式,包含原始ADC数据和调试信息)
 *        1 = INFO  (标准模式,仅输出物理量测量值)
 *        2 = WARN  (精简模式,仅输出警告和错误)
 *        3 = SILENT(静默模式,关闭所有日志输出)
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL  0U
#endif

/**
 * @brief 测量数据报告周期 (毫秒)
 *        可通过 UART 命令动态修改
 */
#ifndef MEASURE_REPORT_PERIOD_MS
#define MEASURE_REPORT_PERIOD_MS  50U
#endif

/**
 * @brief 系统心跳指示灯周期 (毫秒,0=禁用)
 */
#ifndef HEARTBEAT_PERIOD_MS
#define HEARTBEAT_PERIOD_MS  1000U
#endif

/**
 * @brief 原始ADC数据输出开关 (LOG_LEVEL=0时有效)
 *        1 = 输出原始滤波值用于校准调试
 *        0 = 仅输出物理量
 */
#ifndef SHOW_ADC_RAW_DATA
#define SHOW_ADC_RAW_DATA  1U
#endif

/* ============================================================================ */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
