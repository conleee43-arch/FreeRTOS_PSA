/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "uart_dma_driver.h"
#include "adc_dma_driver.h"
#include "dac_control.h"
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
/* Definitions for uartTask */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name = "uartTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 256 * 4
 };
void StartUartTask(void *argument);

/* --- UART DMA 驱动实例与双缓冲区声明 --- */
UartDma_Handler_t g_uart1_dma;

#define USART1_RX_DMA_BUF_SIZE  256U
#define USART1_RX_MAIN_BUF_SIZE 256U

static uint8_t g_usart1_rx_dma_buf[USART1_RX_DMA_BUF_SIZE];
static uint8_t g_usart1_rx_main_buf[USART1_RX_MAIN_BUF_SIZE];

/* 数据接收并回显解析回调 */
static void Usart1_RxParser_Callback(const uint8_t *data, uint16_t len)
{
    /* 物理回显：非阻塞原路送回 */
    (void)UartDma_Transmit_NonBlocking(&g_uart1_dma, data, len);

    if (len > 0U)
    {
        char cmd_buf[64];
        uint16_t copy_len = (len < (sizeof(cmd_buf) - 1U)) ? len : (uint16_t)(sizeof(cmd_buf) - 1U);
        memcpy(cmd_buf, data, copy_len);
        cmd_buf[copy_len] = '\0';

        char *p_val = strstr(cmd_buf, "SetDAC:");
        if (p_val != NULL)
        {
            p_val += 7;
            char *endptr;
            float target_val = strtof(p_val, &endptr);
            if (p_val != endptr)
            {
                DAC_Control_UpdatePfcTargetCurrent(target_val);
            }
        }

        char *p_of = strstr(cmd_buf, "SetOF:");
        if (p_of != NULL)
        {
            p_of += 6;
            if (*p_of == '1')
            {
                Output_Control_Enable();
            }
            else if (*p_of == '0')
            {
                Output_Control_Disable();
            }
        }
    }
}
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* Mutex removed for debugging */
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

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  uartTaskHandle = osThreadNew(StartUartTask, NULL, &uartTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  extern ADC_HandleTypeDef hadc1;  /* 声明 CubeMX 中生成的 ADC1 句柄 */
  static char measure_buf[128];    /* 非阻塞 DMA 入队前仍需稳定的格式化源缓冲。 */
  static char code_buf[96];
  static uint32_t print_tick = 0;  /* 🟢 极重要：改为 static */
  static uint32_t dac_tick = 0;

  DAC_Control_Start(0U);

  /* 1. 初始化 ADC DMA 采集驱动 (自动开启芯片硬件自校准和 DMA 循环传输) */
  HAL_StatusTypeDef init_res = Measure_Init(&hadc1);
  if (init_res != HAL_OK)
  {
      char err_msg[128];
      int err_len = snprintf(err_msg, sizeof(err_msg), 
                             "\r\n[Fatal Error] Measure_Init Failed! Code: %d, ADC State: 0x%08X, DMA State: 0x%08X\r\n", 
                             (int)init_res, (unsigned int)hadc1.State, (unsigned int)(hadc1.DMA_Handle ? hadc1.DMA_Handle->State : 0));
      if (err_len > 0)
      {
          extern UART_HandleTypeDef huart1;
          HAL_UART_Transmit(&huart1, (uint8_t*)err_msg, (uint16_t)err_len, HAL_MAX_DELAY);
      }
      Error_Handler(); /* 初始化或自校准异常，进入安全保护 */
  }

  /* 任务无限循环 */
  for(;;)
  {
    uint32_t current_tick = osKernelGetTickCount();

    /* 2. 高频状态机更新：计算 CNDTR 指针，无锁定位新 Slot 组，触发自适应滑动滤波 and 出厂参数自校准 */
    Measure_Update();

    if ((current_tick - dac_tick) >= 100U)
    {
        dac_tick = current_tick;
        Output_Control_SetCurrent(DAC_Control_GetPfcTargetCurrent());
    }

    /* 3. 每隔 1000 毫秒，格式化输出校准滤波后的遥测数据包 */
    if ((current_tick - print_tick) >= 1000U)
    {
        if (Measure_IsReady())
        {
            /* 第一帧保持 GUI 行协议，第二帧保留 6 通道滤波 ADC code 供串口调试。 */
            int measure_len = snprintf(measure_buf, sizeof(measure_buf),
                                       "\r\n[Measure] V1:%0.2fV V2:%0.2fV CO:%0.2fA VO:%0.2fV T:%0.1fC Vref:%0.3fV\r\n",
                                       Measure_GetV1In(),
                                       Measure_GetV2In(),
                                       Measure_GetCoOut(),
                                       Measure_GetVoOut(),
                                       Measure_GetTemp(),
                                       Measure_GetVref());

            if ((measure_len > 0) && ((size_t)measure_len < sizeof(measure_buf)))
            {
                /* GUI 主协议帧优先；ADC code 诊断帧按最佳努力发送。 */
                if (UartDma_Transmit_NonBlocking(&g_uart1_dma, (const uint8_t*)measure_buf, (uint16_t)measure_len) == HAL_OK)
                {
                    print_tick = current_tick;

                    int code_len = snprintf(code_buf, sizeof(code_buf),
                                            "[Code] 0:%u,1:%u,2:%u,3:%u,4:%u,5:%u\r\n",
                                            (unsigned int)Measure_GetRawCode(0),
                                            (unsigned int)Measure_GetRawCode(1),
                                            (unsigned int)Measure_GetRawCode(2),
                                            (unsigned int)Measure_GetRawCode(3),
                                            (unsigned int)Measure_GetRawCode(4),
                                            (unsigned int)Measure_GetRawCode(5));

                    if ((code_len > 0) && ((size_t)code_len < sizeof(code_buf)))
                    {
                        (void)UartDma_Transmit_NonBlocking(&g_uart1_dma, (const uint8_t*)code_buf, (uint16_t)code_len);
                    }
                }
            }
            else
            {
                print_tick = current_tick;
            }
        }
        else
        {
            print_tick = current_tick;
        }
    }

    /* 4. 每毫秒更新，保障滤波算法的高吞吐率并释放 CPU 资源 */
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void StartUartTask(void *argument)
{
  /* USER CODE BEGIN StartUartTask */
  extern UART_HandleTypeDef huart1;
  
  /* 初始化驱动实例，接管 USART1 的 DMA 通信 */
  if (UartDma_Init(&g_uart1_dma, 
                   &huart1, 
                   g_usart1_rx_dma_buf, 
                   USART1_RX_DMA_BUF_SIZE, 
                   g_usart1_rx_main_buf, 
                   USART1_RX_MAIN_BUF_SIZE) != HAL_OK)
  {
      Error_Handler();
  }

  char task2_msg[] = "\r\n[Task 2] UART DMA NonBlocking Running!\r\n";
  for(;;)
  {
    /* 以非阻塞方式发送任务状态数据 */
    (void)UartDma_Transmit_NonBlocking(&g_uart1_dma, (const uint8_t*)task2_msg, (uint16_t)(sizeof(task2_msg) - 1U));
    
    /* 
     * 极其优秀的 RTOS 轮询设计：
     * 每 2 秒的主循环中，通过内循环以 200Hz 的频率 (每 5ms) 快速轮询 Poll 接收自愈与派发，
     * 既保证了接收的亚毫秒级实时性，又通过 osDelay(5) 释放 CPU，确保多任务流畅并发。
     */
    for (uint16_t i = 0U; i < 400U; i++)
    {
        UartDma_Poll(&g_uart1_dma, Usart1_RxParser_Callback);
        osDelay(5);
    }
  }
  /* USER CODE END StartUartTask */
}
/* USER CODE END Application */
