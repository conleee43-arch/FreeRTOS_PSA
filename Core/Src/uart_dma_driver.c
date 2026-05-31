/**
  ******************************************************************************
  * @file    uart_dma_driver.c
  * @author  Antigravity (Google DeepMind Team)
  * @brief   基于 STM32 HAL 库的高效、安全、自愈式非阻塞 UART DMA 驱动源文件。
  ******************************************************************************
  */

#include "uart_dma_driver.h"
#include <string.h>

/* 最大实例支持数量 */
#define UART_DMA_MAX_INSTANCES 4U

/* 全局句柄注册表，用于在 HAL 中断中分发事件 */
static UartDma_Handler_t *g_uart_dma_instances[UART_DMA_MAX_INSTANCES] = {NULL};
static uint8_t g_uart_dma_instance_count = 0U;

/* === 内部私有辅助函数声明 === */
static UartDma_Handler_t *UartDma_FindInstance(const UART_HandleTypeDef *huart);
static void UartDma_StartNextTransmit(UartDma_Handler_t *handler);

/**
 * @brief  初始化 UART DMA 驱动实例
 */
HAL_StatusTypeDef UartDma_Init(UartDma_Handler_t *handler, 
                               UART_HandleTypeDef *huart,
                               uint8_t *rx_dma_buf, 
                               uint32_t rx_dma_size,
                               uint8_t *rx_main_buf, 
                               uint32_t rx_main_size)
{
    /* 1. 安全入参空指针检查 */
    if ((handler == NULL) || (huart == NULL) || (rx_dma_buf == NULL) || (rx_main_buf == NULL)) {
        return HAL_ERROR;
    }
    if ((rx_dma_size == 0U) || (rx_main_size == 0U)) {
        return HAL_ERROR;
    }

    /* 2. 检查并注册当前实例到全局表中，以便中断回调匹配 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    UartDma_Handler_t *exist = UartDma_FindInstance(huart);
    if (exist == NULL) {
        if (g_uart_dma_instance_count >= UART_DMA_MAX_INSTANCES) {
            __set_PRIMASK(primask);
            return HAL_ERROR; /* 注册实例已达上限 */
        }
        g_uart_dma_instances[g_uart_dma_instance_count] = handler;
        g_uart_dma_instance_count++;
    }
    
    __set_PRIMASK(primask);

    /* 3. 初始化控制句柄成员变量 */
    handler->huart = huart;
    handler->rx_dma_buf = rx_dma_buf;
    handler->rx_dma_buf_size = rx_dma_size;
    handler->rx_main_buf = rx_main_buf;
    handler->rx_main_buf_size = rx_main_size;
    handler->rx_data_len = 0U;
    handler->rx_completed_flag = 0U;
    handler->rx_pending_flag = 0U;

    handler->tx_queue_head = 0U;
    handler->tx_queue_tail = 0U;
    handler->tx_queue_count = 0U;
    (void)memset(handler->tx_queue, 0, sizeof(handler->tx_queue));
    (void)memset(handler->tx_item_lens, 0, sizeof(handler->tx_item_lens));
    (void)memset(handler->tx_dma_buf, 0, sizeof(handler->tx_dma_buf));
    handler->tx_busy = 0U;

    /* 4. 首次开启不定长 DMA 接收中断 */
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(huart, rx_dma_buf, (uint16_t)rx_dma_size);
    if (status == HAL_OK) {
        /* 极致优化：禁用 DMA 接收的半传输 (HT) 中断，仅保留传输完成 (TC) 与串口空闲 (IDLE) 中断 */
        if (huart->hdmarx != NULL) {
            __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
        }
        handler->rx_pending_flag = 0U;
    } else {
        /* 瞬间失败标记为 Pending，在主循环轮询中自愈重建接收 */
        handler->rx_pending_flag = 1U;
    }

    return HAL_OK;
}

/**
 * @brief  非阻塞方式发送数据（多线程/中断并发安全）
 */
HAL_StatusTypeDef UartDma_Transmit_NonBlocking(UartDma_Handler_t *handler, const uint8_t *data, uint16_t len)
{
    /* 1. 安全入参检查 */
    if ((handler == NULL) || (data == NULL) || (len == 0U) || (len > UART_DMA_TX_ITEM_MAX_LEN)) {
        return HAL_ERROR;
    }

    /* 2. 进入临界区，基于 PRIMASK 硬件级原子保护 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 3. 检查环形二维发送队列是否已满 */
    if (handler->tx_queue_count >= UART_DMA_TX_QUEUE_SIZE) {
        __set_PRIMASK(primask); /* 退出临界区，避免死锁 */
        return HAL_BUSY;
    }

    /* 4. 将应用层数据拷贝到环形队列队尾，并更新指针与计数 */
    (void)memcpy(handler->tx_queue[handler->tx_queue_tail], data, len);
    handler->tx_item_lens[handler->tx_queue_tail] = len;
    handler->tx_queue_tail = (handler->tx_queue_tail + 1U) % UART_DMA_TX_QUEUE_SIZE;
    handler->tx_queue_count++;

    /* 5. 若 DMA 发送器处于空闲状态，直接触发拉出数据链式发送的第一发 */
    if (handler->tx_busy == 0U) {
        handler->tx_busy = 1U;
        UartDma_StartNextTransmit(handler);
    }

    /* 6. 恢复中断，退出临界区 */
    __set_PRIMASK(primask);

    return HAL_OK;
}

/**
 * @brief  驱动的主循环/任务轮询解析与自愈维护函数
 */
void UartDma_Poll(UartDma_Handler_t *handler, UartDma_RxCallback_t rx_callback)
{
    if (handler == NULL) {
        return;
    }

    /* 1. 接收自愈逻辑：若接收标志处于挂起状态 (Pending)，尝试安全重新开启 DMA 接收 */
    if (handler->rx_pending_flag != 0U) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();

        HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(handler->huart, 
                                                                handler->rx_dma_buf, 
                                                                (uint16_t)handler->rx_dma_buf_size);
        if (status == HAL_OK) {
            if (handler->huart->hdmarx != NULL) {
                __HAL_DMA_DISABLE_IT(handler->huart->hdmarx, DMA_IT_HT);
            }
            handler->rx_pending_flag = 0U; /* 自愈重建成功 */
        }
        
        __set_PRIMASK(primask);
    }

    /* 2. 数据解析回调：当接收完成标志置位时，进行安全的业务数据派发 */
    if (handler->rx_completed_flag != 0U) {
        if (rx_callback != NULL) {
            /* 回调应用层处理二级主缓冲区数据 */
            rx_callback(handler->rx_main_buf, (uint16_t)handler->rx_data_len);
        }

        /* 派发完成后，进入临界区复位接收状态机变量 */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        
        handler->rx_completed_flag = 0U;
        handler->rx_data_len = 0U;
        
        __set_PRIMASK(primask);
    }
}

/**
 * @brief  清空发送消息环形队列，并安全中止当前正在进行的 DMA 物理发送
 */
HAL_StatusTypeDef UartDma_Flush(UartDma_Handler_t *handler)
{
    if (handler == NULL) {
        return HAL_ERROR;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 1. 复位环形队列及 busy 控制状态 */
    handler->tx_queue_head = 0U;
    handler->tx_queue_tail = 0U;
    handler->tx_queue_count = 0U;
    handler->tx_busy = 0U;
    (void)memset(handler->tx_item_lens, 0, sizeof(handler->tx_item_lens));

    /* 2. 强行终止当前的物理 DMA 发送 */
    (void)HAL_UART_AbortTransmit(handler->huart);

    __set_PRIMASK(primask);
    return HAL_OK;
}

/* ============================================================================== */
/*                              内部静态辅助函数实现                              */
/* ============================================================================== */

/**
 * @brief  从全局注册表中寻找匹配的 UART DMA 驱动实例
 * @param  huart: 串口句柄指针
 * @retval 匹配的 UartDma_Handler_t 实例指针，未找到则返回 NULL
 */
static UartDma_Handler_t *UartDma_FindInstance(const UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return NULL;
    }
    for (uint8_t i = 0U; i < g_uart_dma_instance_count; i++) {
        if ((g_uart_dma_instances[i] != NULL) && (g_uart_dma_instances[i]->huart == huart)) {
            return g_uart_dma_instances[i];
        }
    }
    return NULL;
}

/**
 * @brief  从环形发送消息队列中拉取下一帧数据并启动实际 DMA 发送 (非阻塞链式发动机)
 * @note   调用此内部函数前已由外部保证进入临界区或处于中断中，多并发安全。
 */
static void UartDma_StartNextTransmit(UartDma_Handler_t *handler)
{
    if (handler->tx_queue_count > 0U) {
        uint16_t head = handler->tx_queue_head;
        uint16_t len = handler->tx_item_lens[head];

        /* 将环形队头数据深拷贝到独占 DMA 发送缓存区，规避发送期间应用层并发篡改数据风险 */
        (void)memcpy(handler->tx_dma_buf, handler->tx_queue[head], len);

        /* 更新环形队列头部指针和队列计数 */
        handler->tx_queue_head = (head + 1U) % UART_DMA_TX_QUEUE_SIZE;
        handler->tx_queue_count--;

        /* 开启 HAL 库物理 DMA 发送 */
        HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(handler->huart, handler->tx_dma_buf, len);
        if (status != HAL_OK) {
            /* 物理发送失败，清除忙标志，防止发送通道死锁 */
            handler->tx_busy = 0U;
        } else {
            handler->tx_busy = 1U;
        }
    } else {
        handler->tx_busy = 0U;
    }
}

/* ============================================================================== */
/*                             HAL 全局弱函数回调重写                              */
/* ============================================================================== */

/**
 * @brief  UART DMA 不定长接收中断回调 (由空闲中断 IDLE 或满缓存 TC 自动触发)
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    UartDma_Handler_t *handler = UartDma_FindInstance(huart);
    if (handler != NULL) {
        /* 1. 显式调用 HAL_UART_AbortReceive，强制规避并重置 HAL 库内部接收状态机冲突 */
        (void)HAL_UART_AbortReceive(huart);

        /* 2. 将一级物理 DMA 缓冲区中的接收数据深拷贝到二级应用主缓冲区 */
        uint32_t copy_size = (Size < handler->rx_main_buf_size) ? (uint32_t)Size : handler->rx_main_buf_size;
        if (copy_size > 0U) {
            (void)memcpy(handler->rx_main_buf, handler->rx_dma_buf, copy_size);
            handler->rx_data_len = copy_size;
            handler->rx_completed_flag = 1U; /* 标记有新包接收完成 */
        }

        /* 3. 自愈重建：尝试重新开启下一轮不定长 DMA 接收 */
        HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(huart, 
                                                                handler->rx_dma_buf, 
                                                                (uint16_t)handler->rx_dma_buf_size);
        if (status == HAL_OK) {
            /* 重新禁用半传输中断，避免产生 HT 开销 */
            if (huart->hdmarx != NULL) {
                __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
            }
            handler->rx_pending_flag = 0U;
        } else {
            /* 启动失败（例如资源瞬间繁忙），挂起 Pending 标志，交由 Poll 后台线程自愈重启 */
            handler->rx_pending_flag = 1U;
        }
    }
}

/**
 * @brief  UART 发送完成中断回调
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    UartDma_Handler_t *handler = UartDma_FindInstance(huart);
    if (handler != NULL) {
        /* 进入临界区，避免多任务拉出与中断拉出时的并发指针错乱 */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();

        handler->tx_busy = 0U;
        /* 链式调用：拉出发送队列里的下一帧数据发送 */
        UartDma_StartNextTransmit(handler);

        __set_PRIMASK(primask);
    }
}

/**
 * @brief  UART 错误处理回调（全面自愈，抗帧错误/溢出ORE挂死）
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    UartDma_Handler_t *handler = UartDma_FindInstance(huart);
    if (handler != NULL) {
        /* 1. 安全清除各类错误状态寄存器标志 (防止由于 ORE 溢出中断死循环，导致系统完全挂起) */
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);

        /* 2. 强行终止当前的接收与发送，强行让 HAL 状态机归位到 READY */
        (void)HAL_UART_AbortReceive(huart);
        (void)HAL_UART_AbortTransmit(huart);

        /* 3. 复位发送繁忙状态（物理发送已中止） */
        handler->tx_busy = 0U;

        /* 4. 彻底自愈：重新拉起新的一轮不定长 DMA 接收 */
        HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(huart, 
                                                                handler->rx_dma_buf, 
                                                                (uint16_t)handler->rx_dma_buf_size);
        if (status == HAL_OK) {
            if (huart->hdmarx != NULL) {
                __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
            }
            handler->rx_pending_flag = 0U;
        } else {
            /* 状态仍然错乱时置位 Pending，交由后台轮询继续尝试，确保永远自愈 */
            handler->rx_pending_flag = 1U;
        }
    }
}
