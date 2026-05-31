/**
  ******************************************************************************
  * @file    uart_dma_driver.h
  * @author  Antigravity (Google DeepMind Team)
  * @brief   基于 STM32 HAL 库的高效、安全、自愈式非阻塞 UART DMA 驱动头文件。
  *          完美兼容裸机以及 FreeRTOS 多任务高频并发调用。
  ******************************************************************************
  */

#ifndef UART_DMA_DRIVER_H
#define UART_DMA_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* 适配 STM32G4 系列 HAL 库 */
#include "stm32g4xx_hal.h"

/* 
 * 驱动参数配置宏（可根据实际需求微调）
 */
#ifndef UART_DMA_TX_QUEUE_SIZE
#define UART_DMA_TX_QUEUE_SIZE     16U  /**< 发送二维消息队列深度（帧数） */
#endif

#ifndef UART_DMA_TX_ITEM_MAX_LEN
#define UART_DMA_TX_ITEM_MAX_LEN   128U /**< 单次发送的最大数据长度（字节） */
#endif

/**
 * @brief UART DMA 驱动实例控制句柄结构体
 */
typedef struct {
    UART_HandleTypeDef *huart;              /**< 关联的 STM32 HAL UART 句柄指针 */
    
    /* === 接收 (RX) 控制段 === */
    uint8_t *rx_dma_buf;                    /**< 一级物理 DMA 接收缓冲区指针（需由外部静态分配） */
    uint32_t rx_dma_buf_size;               /**< 一级物理 DMA 接收缓冲区容量大小 */
    uint8_t *rx_main_buf;                   /**< 二级应用层主接收缓冲区指针 */
    uint32_t rx_main_buf_size;              /**< 二级应用层主接收缓冲区容量大小 */
    volatile uint32_t rx_data_len;          /**< 缓存中当前已收到的有效数据字节长度 */
    volatile uint8_t rx_completed_flag;     /**< 帧接收完毕就绪标志（1: 收到新的一包数据） */
    volatile uint8_t rx_pending_flag;       /**< 中断中重启接收失败的 Pending 标志（1: 待后台Poll中自愈重启） */
    
    /* === 发送 (TX) 控制段 === */
    uint8_t tx_queue[UART_DMA_TX_QUEUE_SIZE][UART_DMA_TX_ITEM_MAX_LEN]; /**< 线程安全的环形二维发送消息缓冲区 */
    uint16_t tx_item_lens[UART_DMA_TX_QUEUE_SIZE];                      /**< 环形队列中每帧数据的实际长度记录 */
    volatile uint16_t tx_queue_head;        /**< 环形队列头指针（出队发送端） */
    volatile uint16_t tx_queue_tail;        /**< 环形队列尾指针（入队写入端） */
    volatile uint16_t tx_queue_count;       /**< 当前队列中积压的待发消息帧总数 */
    uint8_t tx_dma_buf[UART_DMA_TX_ITEM_MAX_LEN];                       /**< 独立物理 DMA 发送主缓冲区 */
    volatile uint8_t tx_busy;               /**< DMA 发送状态标志（1: 正在发送，0: 空闲） */
} UartDma_Handler_t;

/**
 * @brief 接收数据处理回调函数指针类型
 * @param data 接收到的数据首地址
 * @param len 接收到的数据长度
 */
typedef void (*UartDma_RxCallback_t)(const uint8_t *data, uint16_t len);

/* ============================================================================== */
/*                                核心 API 暴露接口                              */
/* ============================================================================== */

/**
 * @brief  初始化 UART DMA 驱动实例
 * @note   在初始化过程中，会自动禁用 DMA 接收的半传输中断 (HT)，以实现性能最大化。
 * @param  handler: 驱动句柄指针
 * @param  huart: STM32 HAL 串口句柄指针 (如 &huart1)
 * @param  rx_dma_buf: 外部传入的一级物理接收缓冲区指针（必须为全局/静态生存期）
 * @param  rx_dma_size: 一级物理接收缓冲区大小（建议设计为可能的最大包长的 2 倍）
 * @param  rx_main_buf: 外部传入的二级应用层接收缓冲区指针（用于应用层读取解析）
 * @param  rx_main_size: 二级主接收缓冲区大小
 * @retval HAL_StatusTypeDef: HAL_OK 表示成功，HAL_ERROR 表示参数错误或实例注册已满
 */
HAL_StatusTypeDef UartDma_Init(UartDma_Handler_t *handler, 
                               UART_HandleTypeDef *huart,
                               uint8_t *rx_dma_buf, 
                               uint32_t rx_dma_size,
                               uint8_t *rx_main_buf, 
                               uint32_t rx_main_size);

/**
 * @brief  非阻塞方式发送数据（线程安全，支持并发）
 * @details 该函数只负责将数据快速压入环形队列并立即返回。若 DMA 发送引擎空闲，
 *          会自动触发首次 DMA 传送，并在发送完成中断 (TC) 中链式发送后续数据，绝不阻塞。
 * @param  handler: 驱动句柄指针
 * @param  data: 待发送数据缓冲区的首地址
 * @param  len: 待发送数据长度（不能超过单帧最大限制 UART_DMA_TX_ITEM_MAX_LEN）
 * @retval HAL_StatusTypeDef: 
 *         - HAL_OK: 成功压入队列并成功启动发送/等待发送
 *         - HAL_BUSY: 发送队列已满（高频发送拥堵，应稍后重试）
 *         - HAL_ERROR: 参数错误（如指针为空或长度超限）
 */
HAL_StatusTypeDef UartDma_Transmit_NonBlocking(UartDma_Handler_t *handler, const uint8_t *data, uint16_t len);

/**
 * @brief  驱动的主循环/任务轮询解析与自愈维护函数
 * @note   应当在 bare-metal 的 main 函数 while(1) 循环内，或者 FreeRTOS 的用户任务中高频轮询调用。
 * @param  handler: 驱动句柄指针
 * @param  rx_callback: 接收完成后的应用层解析回调函数（传入收到的数据首地址和长度）
 * @retval 无
 */
void UartDma_Poll(UartDma_Handler_t *handler, UartDma_RxCallback_t rx_callback);

/**
 * @brief  清空发送消息环形队列，并安全中止当前正在进行的 DMA 物理发送
 * @param  handler: 驱动句柄指针
 * @retval HAL_StatusTypeDef: HAL_OK 清理成功，HAL_ERROR 参数错误
 */
HAL_StatusTypeDef UartDma_Flush(UartDma_Handler_t *handler);

#ifdef __cplusplus
}
#endif

#endif /* UART_DMA_DRIVER_H */
