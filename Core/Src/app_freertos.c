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
#include <stdint.h>
#include "uart_dma_driver.h"
#include "adc_dma_driver.h"
#include "dac_control.h"
#include "output_control.h"
#include "output_protection.h"
#include "calc_control.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    MSG_TYPE_MEASURE,
    MSG_TYPE_CALC,
    MSG_TYPE_STATE,
    MSG_TYPE_ECHO,
    MSG_TYPE_BOOT
} UartMsgType_t;

typedef struct {
    uint16_t len;
    uint8_t payload[128];
} UartMsg_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART_APP_MSG_MAX_LEN        128U
#define CALC_CONTROL_RESET_FLAG     0x00000001U
#define UART_BACKLOG_RETRY_MAX      20U
#define UART_BACKLOG_BACKOFF_MS     20U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 任务句柄和属性定义 */
osThreadId_t sampleFilterTaskHandle;
const osThreadAttr_t sampleFilterTask_attributes = {
  .name = "sampleFilterTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};

osThreadId_t emergencyTaskHandle;
const osThreadAttr_t emergencyTask_attributes = {
  .name = "emergencyTask",
  .priority = (osPriority_t) osPriorityRealtime,
  .stack_size = 512 * 4
};

osThreadId_t calcControlTaskHandle;
const osThreadAttr_t calcControlTask_attributes = {
  .name = "calcControlTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};

osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name = "uartTask",
  .priority = (osPriority_t) osPriorityAboveNormal,
  .stack_size = 512 * 4
};

/* 串口消息队列句柄 */
osMessageQueueId_t uartMsgQueueHandle;

/* --- UART DMA 驱动实例与双缓冲区声明 --- */
UartDma_Handler_t g_uart1_dma;

#define USART1_RX_DMA_BUF_SIZE  256U
#define USART1_RX_MAIN_BUF_SIZE 256U

static uint8_t g_usart1_rx_dma_buf[USART1_RX_DMA_BUF_SIZE];
static uint8_t g_usart1_rx_main_buf[USART1_RX_MAIN_BUF_SIZE];
static volatile uint8_t s_pause_state_ack_pending = 0U;
static volatile uint8_t s_resume_state_ack_pending = 0U;

void StartSampleFilterTask(void *argument);
void StartEmergencyTask(void *argument);
void StartCalcControlTask(void *argument);
void StartUartTask(void *argument);
static void UartQueue_PostBytes(UartMsgType_t type, const uint8_t *data, uint16_t len);
static void PostCalcStateLine(const char *state_str);

/* 数据接收并回显解析回调 */
static void Usart1_RxParser_Callback(const uint8_t *data, uint16_t len)
{
    UartQueue_PostBytes(MSG_TYPE_ECHO, data, len);

    if (len > 0U)
    {
        char cmd_buf[64];
        uint16_t copy_len = (len < (sizeof(cmd_buf) - 1U)) ? len : (uint16_t)(sizeof(cmd_buf) - 1U);
        memcpy(cmd_buf, data, copy_len);
        cmd_buf[copy_len] = '\0';

        Output_Control_Status_t control_status = OUTPUT_CONTROL_OK;
        uint8_t controls_safe = (Measure_IsReady() && (Output_Protection_GetState() == OUTPUT_PROTECTION_NORMAL)) ? 1U : 0U;
        static const uint8_t rejected_msg[] = "\r\n[State] STATE:CONTROL_REJECTED\r\n";

        char *p_val = strstr(cmd_buf, "SetDAC:");
        if (p_val != NULL)
        {
            p_val += 7;
            char *endptr;
            float target_val = strtof(p_val, &endptr);
            if (p_val != endptr)
            {
                if (Calc_Control_IsClosedLoopActive() != 0U)
                {
                    UartQueue_PostBytes(MSG_TYPE_STATE, rejected_msg, (uint16_t)(sizeof(rejected_msg) - 1U));
                }
                else if (controls_safe != 0U)
                {
                    control_status = Output_Control_SetCurrent(target_val);
                    if (control_status != OUTPUT_CONTROL_OK)
                    {
                        UartQueue_PostBytes(MSG_TYPE_STATE, rejected_msg, (uint16_t)(sizeof(rejected_msg) - 1U));
                    }
                }
                else
                {
                    UartQueue_PostBytes(MSG_TYPE_STATE, rejected_msg, (uint16_t)(sizeof(rejected_msg) - 1U));
                }
            }
        }

        char *p_of = strstr(cmd_buf, "SetOF:");
        if (p_of != NULL)
        {
            p_of += 6;
            if (*p_of == '1')
            {
                if (controls_safe != 0U)
                {
                    control_status = Output_Control_Enable();
                    if (control_status != OUTPUT_CONTROL_OK)
                    {
                        UartQueue_PostBytes(MSG_TYPE_STATE, rejected_msg, (uint16_t)(sizeof(rejected_msg) - 1U));
                    }
                }
                else
                {
                    UartQueue_PostBytes(MSG_TYPE_STATE, rejected_msg, (uint16_t)(sizeof(rejected_msg) - 1U));
                }
            }
            else if (*p_of == '0')
            {
                Output_Control_Disable();
            }
        }

        char *p_reset = strstr(cmd_buf, "SystemReset");
        if (p_reset != NULL)
        {
            UartMsg_t reset_msg;
            int reset_len = snprintf((char*)reset_msg.payload, sizeof(reset_msg.payload),
                                     "\r\n[System] Resetting...\r\n");
            if ((reset_len > 0) && ((size_t)reset_len < sizeof(reset_msg.payload)))
            {
                reset_msg.len = (uint16_t)reset_len;
                UartQueue_PostBytes(MSG_TYPE_STATE, reset_msg.payload, reset_msg.len);
            }
            /* 延迟发送复位确认后再复位，确保消息发送完成 */
            for (volatile uint32_t i = 0U; i < 100000U; i++) { }
            NVIC_SystemReset();
        }

        /* 2A 稳定等待挂起调试命令 */
        char *p_pause2a = strstr(cmd_buf, "DebugPause2A");
        if (p_pause2a != NULL)
        {
            uint32_t tick = osKernelGetTickCount();
            if (Calc_Control_PauseWait2A(tick) != 0U)
            {
                static const uint8_t pause_ok_msg[] = "\r\n[State] STATE:WAIT_2A_PAUSED\r\n";
                s_pause_state_ack_pending = 1U;
                s_resume_state_ack_pending = 0U;
                UartQueue_PostBytes(MSG_TYPE_STATE, pause_ok_msg, (uint16_t)(sizeof(pause_ok_msg) - 1U));
            }
            else
            {
                static const uint8_t pause_fail_msg[] = "\r\n[State] STATE:PAUSE_FAILED\r\n";
                UartQueue_PostBytes(MSG_TYPE_STATE, pause_fail_msg, (uint16_t)(sizeof(pause_fail_msg) - 1U));
            }
        }

        char *p_resume2a = strstr(cmd_buf, "DebugResume2A");
        if (p_resume2a != NULL)
        {
            uint32_t tick = osKernelGetTickCount();
            if (Calc_Control_ResumeWait2A(tick) != 0U)
            {
                static const uint8_t resume_ok_msg[] = "\r\n[State] STATE:WAIT_2A_STABLE_10S\r\n";
                s_resume_state_ack_pending = 1U;
                s_pause_state_ack_pending = 0U;
                UartQueue_PostBytes(MSG_TYPE_STATE, resume_ok_msg, (uint16_t)(sizeof(resume_ok_msg) - 1U));
            }
            else
            {
                static const uint8_t resume_fail_msg[] = "\r\n[State] STATE:RESUME_FAILED\r\n";
                UartQueue_PostBytes(MSG_TYPE_STATE, resume_fail_msg, (uint16_t)(sizeof(resume_fail_msg) - 1U));
            }
        }
    }
}
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  Output_Control_Init();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */

  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */

  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */

  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  uartMsgQueueHandle = osMessageQueueNew(16, sizeof(UartMsg_t), NULL);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  sampleFilterTaskHandle = osThreadNew(StartSampleFilterTask, NULL, &sampleFilterTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  emergencyTaskHandle = osThreadNew(StartEmergencyTask, NULL, &emergencyTask_attributes);
  calcControlTaskHandle = osThreadNew(StartCalcControlTask, NULL, &calcControlTask_attributes);
  uartTaskHandle = osThreadNew(StartUartTask, NULL, &uartTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */

  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartSampleFilterTask */
/**
  * @brief  Function implementing the sampleFilterTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSampleFilterTask */
void StartSampleFilterTask(void *argument)
{
  /* USER CODE BEGIN StartSampleFilterTask */
  extern ADC_HandleTypeDef hadc1;

  static char measure_buf[128];
  static char code_buf[96];
  static uint32_t print_tick = 0;

  DAC_Control_Start(0U);

  HAL_StatusTypeDef init_res = Measure_Init(&hadc1);
  if (init_res != HAL_OK)
  {
      Error_Handler();
  }

  for(;;)
  {
    uint32_t current_tick = osKernelGetTickCount();

    /* 高频采样与滑动平均滤波 */
    Measure_Update();

    /* 每隔 1000ms 将数据投递到串口队列中发送 */
    if ((current_tick - print_tick) >= 1000U)
    {
        if (Measure_IsReady())
        {
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
                UartQueue_PostBytes(MSG_TYPE_MEASURE, (const uint8_t*)measure_buf, (uint16_t)measure_len);
                print_tick = current_tick;

                int code_len = snprintf(code_buf, sizeof(code_buf),
                                        "[Code] 0:%u,1:%u,2:%u,3:%u,4:%u\r\n",
                                        (unsigned int)Measure_GetRawCode(0),
                                        (unsigned int)Measure_GetRawCode(1),
                                        (unsigned int)Measure_GetRawCode(2),
                                        (unsigned int)Measure_GetRawCode(3),
                                        (unsigned int)Measure_GetRawCode(4));

                if ((code_len > 0) && ((size_t)code_len < sizeof(code_buf)))
                {
                    UartQueue_PostBytes(MSG_TYPE_MEASURE, (const uint8_t*)code_buf, (uint16_t)code_len);
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

    osDelay(1);
  }
  /* USER CODE END StartSampleFilterTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void StartEmergencyTask(void *argument)
{
    Output_Protection_Input_t prot_in;
    Output_Protection_Output_t prot_out;
    Output_Protection_State_t last_state = OUTPUT_PROTECTION_NORMAL;
    uint8_t last_output_enabled = Output_Control_IsEnabled();

    Output_Protection_Init();

    for (;;)
    {
        uint32_t tick = osKernelGetTickCount();

        if (!Measure_IsReady())
        {
            Output_Control_Disable();
            osDelay(2);
            continue;
        }

        prot_in.temperature_c = Measure_GetTemp();
        prot_in.vo_out_v = Measure_GetVoOut();
        prot_in.co_out_a = Measure_GetCoOut();
        prot_in.tick_ms = tick;

        Output_Protection_Update(&prot_in, &prot_out);

        if (prot_out.disable_output)
        {
            Output_Control_Disable();
        }
        else if (prot_out.enable_output)
        {
            (void)Output_Control_Enable();
        }

        if (prot_out.reset_calc_control)
        {
            if (calcControlTaskHandle != NULL)
            {
                (void)osThreadFlagsSet(calcControlTaskHandle, CALC_CONTROL_RESET_FLAG);
            }
        }

        uint8_t current_output_enabled = Output_Control_IsEnabled();
        if (current_output_enabled != last_output_enabled)
        {
            last_output_enabled = current_output_enabled;

            UartMsg_t of_state_msg;
            const char *of_state_str = (current_output_enabled != 0U) ? "OF_ENABLED" : "OF_DISABLED";
            int of_state_len = snprintf((char*)of_state_msg.payload,
                                        sizeof(of_state_msg.payload),
                                        "\r\n[State] STATE:%s\r\n",
                                        of_state_str);
            if ((of_state_len > 0) && ((size_t)of_state_len < sizeof(of_state_msg.payload)))
            {
                of_state_msg.len = (uint16_t)of_state_len;
                UartQueue_PostBytes(MSG_TYPE_STATE, of_state_msg.payload, of_state_msg.len);
            }
        }

        Output_Protection_State_t current_state = Output_Protection_GetState();
        if (current_state != last_state)
        {
            last_state = current_state;

            const char* state_str = "UNKNOWN";
            if (current_state == OUTPUT_PROTECTION_NORMAL) state_str = "NORMAL";
            else if (current_state == OUTPUT_PROTECTION_TRIPPED) state_str = "TRIPPED";
            else if (current_state == OUTPUT_PROTECTION_RECOVERY_WAIT) state_str = "RECOVERY_WAIT";

            UartMsg_t state_msg;
            int state_len = snprintf((char*)state_msg.payload, sizeof(state_msg.payload), "\r\n[State] STATE:%s\r\n", state_str);
            if ((state_len > 0) && ((size_t)state_len < sizeof(state_msg.payload)))
            {
                state_msg.len = (uint16_t)state_len;
                UartQueue_PostBytes(MSG_TYPE_STATE, state_msg.payload, state_msg.len);
            }
        }

        osDelay(2);
    }
}

void StartCalcControlTask(void *argument)
{
    Calc_Control_Input_t calc_in;
    Calc_Control_Output_t calc_out;
    Calc_Control_State_t last_state = CALC_CONTROL_WAIT_SAFE;

    Calc_Control_Init();

    for (;;)
    {
        if (!Measure_IsReady())
        {
            osDelay(10);
            continue;
        }

        uint32_t reset_flags = osThreadFlagsClear(CALC_CONTROL_RESET_FLAG);
        uint8_t reset_request = 0U;
        if ((reset_flags & osFlagsError) != 0U)
        {
            /* osThreadFlagsClear 调用失败，不做处理 */
        }
        else if ((reset_flags & CALC_CONTROL_RESET_FLAG) != 0U)
        {
            reset_request = 1U;
        }

        calc_in.vo_out_v = Measure_GetVoOut();
        calc_in.co_out_a = Measure_GetCoOut();
        calc_in.tick_ms = osKernelGetTickCount();
        calc_in.safe_allowed = (Output_Protection_GetState() == OUTPUT_PROTECTION_NORMAL);
        calc_in.reset_request = reset_request;
        /* 交流线阻限功率输入：交流双通道采样 + 直流母线电压 */
        calc_in.vac_ch1_v = Measure_GetV1In();
        calc_in.vac_ch2_v = Measure_GetV2In();
        calc_in.vdc_sample_v = calc_in.vo_out_v;

        Calc_Control_Update(&calc_in, &calc_out);

        Calc_Control_State_t current_state = Calc_Control_GetState();
        uint8_t wait2a_paused = Calc_Control_IsWait2APaused();

        /* 开路检测错误上报 */
        if (calc_out.open_circuit_error)
        {
            UartMsg_t error_msg;
            int error_len = snprintf((char*)error_msg.payload, sizeof(error_msg.payload),
                                      "\r\n[Error] OPEN_CIRCUIT_DETECTED\r\n");
            if ((error_len > 0) && ((size_t)error_len < sizeof(error_msg.payload)))
            {
                error_msg.len = (uint16_t)error_len;
                UartQueue_PostBytes(MSG_TYPE_STATE, error_msg.payload, error_msg.len);
            }
        }

        /* 稳定判定过程上报 */
        if (calc_out.stable_update)
        {
            UartMsg_t stable_msg;
            int stable_len = snprintf((char*)stable_msg.payload, sizeof(stable_msg.payload),
                                       "\r\n[Stable] Count:%u/%u Delta:%0.1fmA Wait:%ums\r\n",
                                       calc_out.stable_count,
                                       calc_out.stable_target,
                                       calc_out.stable_delta_ma,
                                       (unsigned int)calc_out.stable_wait_ms);
            if ((stable_len > 0) && ((size_t)stable_len < sizeof(stable_msg.payload)))
            {
                stable_msg.len = (uint16_t)stable_len;
                UartQueue_PostBytes(MSG_TYPE_STATE, stable_msg.payload, stable_msg.len);
            }
        }

        if ((current_state == CALC_CONTROL_WAIT_3A_STABLE) &&
            (calc_out.change_current != 0U) &&
            (calc_out.set_current_a == 3.0f))
        {
            PostCalcStateLine("SET_3A");
        }
        else if ((current_state == CALC_CONTROL_WAIT_2A_STABLE) &&
                 (calc_out.change_current != 0U) &&
                 (calc_out.i1 > 0.0f))
        {
            PostCalcStateLine("LATCH_3A");
            PostCalcStateLine("SET_2A");
        }

        if (calc_out.publish_calc_report != 0U)
        {
            PostCalcStateLine("LATCH_2A");
            PostCalcStateLine("CALC_RESISTANCE");
        }

        if (calc_out.change_current)
        {
            Output_Control_Status_t control_status = Output_Control_SetCurrent(calc_out.set_current_a);
            if (control_status != OUTPUT_CONTROL_OK)
            {
                (void)osThreadFlagsSet(calcControlTaskHandle, CALC_CONTROL_RESET_FLAG);
            }
        }

        if (calc_out.publish_calc_report)
        {
            UartMsg_t report_msg;
            int report_len = snprintf((char*)report_msg.payload, sizeof(report_msg.payload),
                                      "\r\n[Calc] R:%0.3fR U1:%0.2fV I1:%0.2fA U2:%0.2fV I2:%0.2fA IOC:%0.2fA\r\n",
                                      calc_out.calculated_resistance,
                                      calc_out.u1, calc_out.i1,
                                      calc_out.u2, calc_out.i2,
                                      calc_out.ioc);
            if ((report_len > 0) && ((size_t)report_len < sizeof(report_msg.payload)))
            {
                report_msg.len = (uint16_t)report_len;
                UartQueue_PostBytes(MSG_TYPE_CALC, report_msg.payload, report_msg.len);
            }

            UartMsg_t debug_ioc_msg;
            int debug_ioc_len = snprintf((char*)debug_ioc_msg.payload, sizeof(debug_ioc_msg.payload),
                                         "\r\n[DebugIOC] dU:%0.2fV dI:%0.2fA Rraw:%0.3fR Vac:%0.2fV VoutAvg:%0.2fV Pin:%0.1fW Pout:%0.1fW IOC:%0.2fA\r\n",
                                         calc_out.du_v,
                                         calc_out.di_a,
                                         calc_out.r_raw_ohm,
                                         calc_out.vac_eff_v,
                                         calc_out.vout_avg_v,
                                         calc_out.pin_w,
                                         calc_out.pout_w,
                                         calc_out.ioc);
            if ((debug_ioc_len > 0) && ((size_t)debug_ioc_len < sizeof(debug_ioc_msg.payload)))
            {
                debug_ioc_msg.len = (uint16_t)debug_ioc_len;
                UartQueue_PostBytes(MSG_TYPE_CALC, debug_ioc_msg.payload, debug_ioc_msg.len);
            }

            /* 交流线阻限功率诊断帧：上报折算限值与 DAC 量化码 */
            UartMsg_t ll_msg;
            int ll_len = snprintf((char*)ll_msg.payload, sizeof(ll_msg.payload),
                                  "\r\n[LineLimit] Iac:%0.2fA Idc:%0.2fA DAC:%u Retest:%u\r\n",
                                  calc_out.iac_limit_a,
                                  calc_out.idc_limit_a,
                                  (unsigned int)calc_out.dac_code,
                                  (unsigned int)calc_out.need_retest);
            if ((ll_len > 0) && ((size_t)ll_len < sizeof(ll_msg.payload)))
            {
                ll_msg.len = (uint16_t)ll_len;
                UartQueue_PostBytes(MSG_TYPE_CALC, ll_msg.payload, ll_msg.len);
            }

            /* 安全链未通过时上报需要重测，便于上位机感知回退 */
            if (calc_out.need_retest)
            {
                UartMsg_t retest_msg;
                int retest_len = snprintf((char*)retest_msg.payload, sizeof(retest_msg.payload),
                                          "\r\n[State] STATE:NEED_RETEST\r\n");
                if ((retest_len > 0) && ((size_t)retest_len < sizeof(retest_msg.payload)))
                {
                    retest_msg.len = (uint16_t)retest_len;
                    UartQueue_PostBytes(MSG_TYPE_STATE, retest_msg.payload, retest_msg.len);
                }
            }
        }

        /* 如果处于 2A 等待挂起状态，通过虚拟状态映射上报 WAIT_2A_PAUSED */
        if ((current_state == CALC_CONTROL_WAIT_2A_STABLE) && (wait2a_paused != 0U))
        {
            if (last_state != 0xFEU)
            {
                last_state = 0xFEU;
                if (s_pause_state_ack_pending != 0U)
                {
                    s_pause_state_ack_pending = 0U;
                }
                else
                {
                    static const uint8_t paused_msg[] = "\r\n[State] STATE:WAIT_2A_PAUSED\r\n";
                    UartQueue_PostBytes(MSG_TYPE_STATE, paused_msg, (uint16_t)(sizeof(paused_msg) - 1U));
                }
            }
        }
        else if (current_state != last_state)
        {
            if ((current_state == CALC_CONTROL_WAIT_2A_STABLE) && (s_resume_state_ack_pending != 0U))
            {
                s_resume_state_ack_pending = 0U;
                continue;
            }

            last_state = current_state;

            const char* state_str = "WAIT_SAFE";
            switch (current_state)
            {
                case CALC_CONTROL_WAIT_SAFE: state_str = "WAIT_SAFE"; break;
                case CALC_CONTROL_SET_3A: state_str = "SET_3A"; break;
                case CALC_CONTROL_WAIT_3A_STABLE: state_str = "WAIT_3A_STABLE"; break;
                case CALC_CONTROL_LATCH_3A: state_str = "LATCH_3A"; break;
                case CALC_CONTROL_SET_2A: state_str = "SET_2A"; break;
                case CALC_CONTROL_WAIT_2A_STABLE: state_str = "WAIT_2A_STABLE_10S"; break;
                case CALC_CONTROL_LATCH_2A: state_str = "LATCH_2A"; break;
                case CALC_CONTROL_CALC_RESISTANCE: state_str = "CALC_RESISTANCE"; break;
                case CALC_CONTROL_MONITOR: state_str = "MONITOR"; break;
            }

            PostCalcStateLine(state_str);
        }

        osDelay(10);
    }
}

void StartUartTask(void *argument)
{
  extern UART_HandleTypeDef huart1;

  static UartMsg_t s_backlog_msg;
  static uint8_t s_backlog_valid = 0U;
  static uint8_t s_backlog_retry_count = 0U;

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
  UartQueue_PostBytes(MSG_TYPE_BOOT, (const uint8_t*)task2_msg, (uint16_t)(sizeof(task2_msg) - 1U));

  for(;;)
  {
    UartDma_Poll(&g_uart1_dma, Usart1_RxParser_Callback);

    uint8_t has_msg = 0U;

    if (s_backlog_valid != 0U)
    {
        has_msg = 1U;
    }
    else
    {
        if (osMessageQueueGet(uartMsgQueueHandle, &s_backlog_msg, NULL, 0U) == osOK)
        {
            s_backlog_valid = 1U;
            s_backlog_retry_count = 0U;
            has_msg = 1U;
        }
    }

    if (has_msg != 0U)
    {
        if (UartDma_Transmit_NonBlocking(&g_uart1_dma, s_backlog_msg.payload, s_backlog_msg.len) == HAL_OK)
        {
            s_backlog_valid = 0U;
            s_backlog_retry_count = 0U;
        }
        else
        {
            s_backlog_retry_count++;
            if (s_backlog_retry_count >= UART_BACKLOG_RETRY_MAX)
            {
                s_backlog_valid = 0U;
                s_backlog_retry_count = 0U;
            }
            else
            {
                osDelay(UART_BACKLOG_BACKOFF_MS);
            }
        }
    }

    osDelay(5);
  }
}

static void UartQueue_PostBytes(UartMsgType_t type, const uint8_t *data, uint16_t len)
{
    if ((uartMsgQueueHandle == NULL) || (data == NULL) || (len == 0U))
    {
        return;
    }

    UartMsg_t msg;
    msg.len = (len > UART_APP_MSG_MAX_LEN) ? UART_APP_MSG_MAX_LEN : len;
    memcpy(msg.payload, data, msg.len);

    osStatus_t put_status = osMessageQueuePut(uartMsgQueueHandle, &msg, 0U, 0U);
    if (put_status != osOK)
    {
        if (type == MSG_TYPE_STATE)
        {
            UartMsg_t dropped_msg;
            if (osMessageQueueGet(uartMsgQueueHandle, &dropped_msg, NULL, 0U) == osOK)
            {
                (void)osMessageQueuePut(uartMsgQueueHandle, &msg, 0U, 0U);
            }
        }
    }
}

static void PostCalcStateLine(const char *state_str)
{
    UartMsg_t state_msg;
    int state_len;

    if (state_str == NULL)
    {
        return;
    }

    state_len = snprintf((char*)state_msg.payload, sizeof(state_msg.payload),
                         "\r\n[State] STATE:%s\r\n", state_str);
    if ((state_len > 0) && ((size_t)state_len < sizeof(state_msg.payload)))
    {
        state_msg.len = (uint16_t)state_len;
        UartQueue_PostBytes(MSG_TYPE_STATE, state_msg.payload, state_msg.len);
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    portDISABLE_INTERRUPTS();
    OF_EN_GPIO_Port->BRR = (uint32_t)OF_EN_Pin;
    DAC1->DHR12R1 = 0U;
    for (;;)
    {
    }
}
/* USER CODE END Application */
