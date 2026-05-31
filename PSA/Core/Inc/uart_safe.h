#ifndef __UART_SAFE_H__
#define __UART_SAFE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "usart.h"

#define UART_SAFE_RX_BUFFER_SIZE        32U
#define UART_SAFE_MAIN_BUFFER_SIZE      (UART_SAFE_RX_BUFFER_SIZE + 1U)
#define MSG_QUEUE_SIZE                  32U
#define MSG_MAX_LEN                     200U
#define UART_SAFE_DMA_BUFFER_SIZE       256U
#define UART_SAFE_RESET_DELAY_MS        100U

/* RX DMA + 空闲中断初始化入口。 */
void UART_Safe_Init(UART_HandleTypeDef *huart);

/* 非阻塞日志发送入口，支持多行拆分与自动格式化。 */
void UART_Safe_Transmit(const char *message);

/* 主循环轮询接口，完成命令解析与延时复位调度。 */
void Process_UART_Commands(void);

/* 返回当前发送队列占用率，范围 0~100。 */
uint8_t UART_Safe_GetBufferUsage(void);

/* 强制终止当前 DMA 发送并清空待发队列。 */
void UART_Safe_FlushBuffer(void);

/* 供 main.c 读取当前任务运行状态。 */
uint8_t UART_Safe_IsTaskRunning(void);

/* 返回最近一次 Stop 命令计算出的运行时长。 */
uint32_t UART_Safe_GetLastDurationMs(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_SAFE_H__ */
