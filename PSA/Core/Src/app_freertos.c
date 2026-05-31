/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : app_freertos.c
  * @description    : FreeRTOS application - 任务迁移自裸机 main.c
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "usart.h"
#include "adc.h"
#include "dac.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uart_safe.h"
#include "adc_measure.h"
#include "dac_control.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* ============================================================================
 * 全局运行时状态（从 main.c 迁移）
 * ============================================================================ */
volatile uint16_t g_report_period_ms = MEASURE_REPORT_PERIOD_MS;
volatile uint8_t  g_protection_enabled = 0U;  /* 保护已禁用:0 / 已启用:1 */

/* ============================================================================
 * FreeRTOS 内核对象定义
 * ============================================================================ */
osThreadId_t Task_UART_Handle;
osThreadId_t Task_Measure_Handle;
osThreadId_t Task_Report_Handle;
osThreadId_t Task_Heartbeat_Handle;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Task_UART(void *argument);
void Task_Measure(void *argument);
void Task_Report(void *argument);
void Task_Heartbeat(void *argument);
/* USER CODE END FunctionPrototypes */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void)
{
  /* 如果需要运行时间统计，在此配置硬件定时器 */
}

__weak unsigned long getRunTimeCounterValue(void)
{
  return 0;
}
/* USER CODE END 1 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  /* Run time stack overflow checking is performed if
     configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. */
  (void)xTask;
  (void)pcTaskName;
}
/* USER CODE END 4 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* USER CODE BEGIN RTOS_THREADS */
  /* 创建任务 - 优先级从高到低 */

  /* Task_UART: 高优先级，UART 命令处理（事件驱动） */
  osThreadAttr_t uart_task_attr = {
    .name = "Task_UART",
    .priority = osPriorityHigh,      /* 优先级 4 */
    .stack_size = 256 * 4           /* 256 Words = 1KB */
  };
  Task_UART_Handle = osThreadNew(Task_UART, NULL, &uart_task_attr);

  /* Task_Measure: 中高优先级，ADC 采样 + DAC 控制 */
  osThreadAttr_t measure_task_attr = {
    .name = "Task_Measure",
    .priority = osPriorityAboveNormal,  /* 优先级 3 */
    .stack_size = 256 * 4
  };
  Task_Measure_Handle = osThreadNew(Task_Measure, NULL, &measure_task_attr);

  /* Task_Report: 中优先级，数据上报 */
  osThreadAttr_t report_task_attr = {
    .name = "Task_Report",
    .priority = osPriorityNormal,      /* 优先级 2 */
    .stack_size = 256 * 4
  };
  Task_Report_Handle = osThreadNew(Task_Report, NULL, &report_task_attr);

  /* Task_Heartbeat: 低优先级，心跳灯 + 系统诊断 */
  osThreadAttr_t heartbeat_task_attr = {
    .name = "Task_Heartbeat",
    .priority = osPriorityBelowNormal,  /* 优先级 1 */
    .stack_size = 128 * 4               /* 128 Words = 512B */
  };
  Task_Heartbeat_Handle = osThreadNew(Task_Heartbeat, NULL, &heartbeat_task_attr);

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_Task_UART */
/**
  * @brief  UART 命令处理任务（从裸机代码迁移）
  * @param  argument: Not used
  * @retval None
  * @note   优先级最高，持续轮询 UART 命令
  */
/* USER CODE END Header_Task_UART */
void Task_UART(void *argument)
{
  /* USER CODE BEGIN Task_UART */
  (void)argument;

  for(;;)
  {
    /* 高实时性命令轮询（无延时，全速运行） */
    Process_UART_Commands();

    /* 让出 CPU，但立即重新调度（高优先级保证及时响应） */
    osDelay(1);  /* 1ms 延时 */
  }
  /* USER CODE END Task_UART */
}

/* USER CODE BEGIN Header_Task_Measure */
/**
  * @brief  ADC 测量与 DAC 控制任务（从裸机代码迁移）
  * @param  argument: Not used
  * @retval None
  * @note   100ms 周期任务
  */
/* USER CODE END Header_Task_Measure */
void Task_Measure(void *argument)
{
  /* USER CODE BEGIN Task_Measure */
  (void)argument;

  for(;;)
  {
    /* DAC 持续输出设定电流 */
    DAC_Control_SetPfcCurrent(DAC_Control_GetPfcTargetCurrent());

    /* 延时 100ms */
    osDelay(100);
  }
  /* USER CODE END Task_Measure */
}

/* USER CODE BEGIN Header_Task_Report */
/**
  * @brief  数据上报任务（从裸机代码迁移）
  * @param  argument: Not used
  * @retval None
  * @note   动态周期上报（g_report_period_ms）
  */
/* USER CODE END Header_Task_Report */
void Task_Report(void *argument)
{
  /* USER CODE BEGIN Task_Report */
  (void)argument;

  for(;;)
  {
    /* 等待 g_report_period_ms（可通过命令动态修改） */
    osDelay(g_report_period_ms);

    /* 检查数据有效性 */
    if (Measure_IsDataValid() != 0U)
    {
      const Measure_Data_t *p_data = Measure_GetDataPtr();
      char log_buf[128];

#if LOG_LEVEL == 0U  /* DEBUG 模式: 输出完整信息 */
      snprintf(log_buf, sizeof(log_buf),
               "[Measure] V1:%.2fV V2:%.2fV CO:%.2fA VO:%.2fV T:%.1fC Vref:%.3fV",
               p_data->v1_in,
               p_data->v2_in,
               p_data->co_out,
               p_data->vo_out,
               p_data->temp_c,
               p_data->vref_plus);
      UART_Safe_Transmit(log_buf);

#if SHOW_ADC_RAW_DATA
      snprintf(log_buf, sizeof(log_buf),
               "[Raw] %u %u %u %u %u %u",
               p_data->filtered_raw[ADC_CH_INDEX_V1_IN],
               p_data->filtered_raw[ADC_CH_INDEX_V2_IN],
               p_data->filtered_raw[ADC_CH_INDEX_CO_OUT],
               p_data->filtered_raw[ADC_CH_INDEX_VO_OUT],
               p_data->filtered_raw[ADC_CH_INDEX_TEMP_SENSOR],
               p_data->filtered_raw[ADC_CH_INDEX_VREFINT]);
      UART_Safe_Transmit(log_buf);
#endif

#elif LOG_LEVEL == 1U  /* INFO 模式: 仅输出物理量 */
      snprintf(log_buf, sizeof(log_buf),
               "[M] %.1fV %.1fV %.2fA %.1fV %.1fC",
               p_data->v1_in,
               p_data->v2_in,
               p_data->co_out,
               p_data->vo_out,
               p_data->temp_c);
      UART_Safe_Transmit(log_buf);

#elif LOG_LEVEL == 2U  /* WARN 模式: 无自动输出 */
      (void)p_data;
#endif
    }
  }
  /* USER CODE END Task_Report */
}

/* USER CODE BEGIN Header_Task_Heartbeat */
/**
  * @brief  心跳灯 + 系统诊断任务（从裸机代码迁移）
  * @param  argument: Not used
  * @retval None
  * @note   1000ms 周期任务，优先级最低
  */
/* USER CODE END Header_Task_Heartbeat */
void Task_Heartbeat(void *argument)
{
  /* USER CODE BEGIN Task_Heartbeat */
  (void)argument;

  for(;;)
  {
#if HEARTBEAT_PERIOD_MS > 0
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_5);  /* 心跳灯 PB5 */
#endif

#if LOG_LEVEL <= 1U
    /* 每秒输出系统状态摘要 */
    char stat_buf[64];
    snprintf(stat_buf, sizeof(stat_buf),
             "[Sys] BufUse:%u%% DAC:%.2fA",
             UART_Safe_GetBufferUsage(),
             DAC_Control_GetPfcTargetCurrent());
    UART_Safe_Transmit(stat_buf);
#endif

    osDelay(1000);
  }
  /* USER CODE END Task_Heartbeat */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */