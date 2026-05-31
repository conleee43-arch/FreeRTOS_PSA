#include "uart_safe.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dac_control.h"
#include "adc_measure.h"


#define UART_SAFE_DEFAULT_MODULE        "System"
#define UART_SAFE_MODULE_NAME_LEN       24U

static UART_HandleTypeDef *s_huart = NULL;

/* ------------------------------ RX 相关状态 ------------------------------ */
static uint8_t rx_buffer[UART_SAFE_RX_BUFFER_SIZE];
static char main_buffer[UART_SAFE_MAIN_BUFFER_SIZE];
static volatile uint8_t data_received_flag = 0U;
static volatile uint16_t received_length = 0U;
static volatile uint8_t rx_restart_pending = 0U;

/* ------------------------------ TX 队列状态 ------------------------------ */
static char msg_queue[MSG_QUEUE_SIZE][MSG_MAX_LEN];
static volatile uint8_t queue_head = 0U;
static volatile uint8_t queue_tail = 0U;
static volatile uint8_t queue_count = 0U;
static uint8_t dma_buffer[UART_SAFE_DMA_BUFFER_SIZE];
static volatile uint8_t dma_busy = 0U;

/* ------------------------------ 日志格式化状态 ------------------------------ */
static char current_module[UART_SAFE_MODULE_NAME_LEN] = UART_SAFE_DEFAULT_MODULE;

/* ------------------------------ 命令业务状态 ------------------------------ */
static volatile uint8_t task_running = 0U;
static volatile uint32_t task_start_tick = 0U;
static volatile uint32_t task_last_duration_ms = 0U;
static volatile uint8_t reset_requested = 0U;
static volatile uint32_t reset_due_tick = 0U;

static uint32_t UART_Safe_EnterCritical(void);
static void UART_Safe_ExitCritical(uint32_t primask);
static size_t UART_Safe_Strnlen(const char *text, size_t max_len);
static void UART_Safe_TrimString(char *text);
static void UART_Safe_CopyString(char *dest, const char *src, size_t dest_size);
static HAL_StatusTypeDef UART_Safe_StartReceiveToIdle(void);
static void UART_Safe_TrySendNext(void);
static uint8_t UART_Safe_QueuePush(const char *message);
static void UART_Safe_ProcessLine(const char *line);
static uint8_t UART_Safe_IsStandardLogLine(const char *line);
static void UART_Safe_UpdateModuleFromFormattedLine(const char *line);
static const char *UART_Safe_DetectLevel(const char *line);
static uint8_t UART_Safe_ExtractModulePrefix(const char *line, char *module, size_t module_size, const char **body);
static void UART_Safe_EnqueueFormattedLine(const char *line);
static void UART_Safe_EnqueueStandardLine(const char *line);
static void UART_Safe_ServiceResetRequest(void);
static void UART_Safe_ServiceRxRestart(void);

static uint32_t UART_Safe_EnterCritical(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    return primask;
}

static void UART_Safe_ExitCritical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static size_t UART_Safe_Strnlen(const char *text, size_t max_len)
{
    size_t length;

    if (text == NULL)
    {
        return 0U;
    }

    for (length = 0U; length < max_len; ++length)
    {
        if (text[length] == '\0')
        {
            break;
        }
    }

    return length;
}

static void UART_Safe_CopyString(char *dest, const char *src, size_t dest_size)
{
    size_t copy_len;

    if ((dest == NULL) || (src == NULL) || (dest_size == 0U))
    {
        return;
    }

    copy_len = UART_Safe_Strnlen(src, dest_size - 1U);
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

static void UART_Safe_TrimString(char *text)
{
    size_t start;
    size_t end;
    size_t length;

    if (text == NULL)
    {
        return;
    }

    length = strlen(text);
    start = 0U;

    while ((start < length) && ((text[start] == ' ') || (text[start] == '\t') || (text[start] == '\r') || (text[start] == '\n')))
    {
        ++start;
    }

    end = length;
    while ((end > start) && ((text[end - 1U] == ' ') || (text[end - 1U] == '\t') || (text[end - 1U] == '\r') || (text[end - 1U] == '\n')))
    {
        --end;
    }

    if (start > 0U)
    {
        memmove(text, &text[start], end - start);
    }

    text[end - start] = '\0';
}

static HAL_StatusTypeDef UART_Safe_StartReceiveToIdle(void)
{
    HAL_StatusTypeDef status;

    if (s_huart == NULL)
    {
        return HAL_ERROR;
    }

    status = HAL_UARTEx_ReceiveToIdle_DMA(s_huart, rx_buffer, sizeof(rx_buffer));
    if (status != HAL_OK)
    {
        return status;
    }

    if (s_huart->hdmarx != NULL)
    {
        __HAL_DMA_DISABLE_IT(s_huart->hdmarx, DMA_IT_HT);
    }

    return HAL_OK;
}

static uint8_t UART_Safe_IsStandardLogLine(const char *line)
{
    const char *stamp_end;
    const char *level_start;
    const char *level_end;
    const char *module_start;
    const char *module_end;

    if ((line == NULL) || (line[0] != '['))
    {
        return 0U;
    }

    stamp_end = strstr(line, " ms]");
    if ((stamp_end == NULL) || (stamp_end[4] != '['))
    {
        return 0U;
    }

    level_start = &stamp_end[4];
    level_end = strchr(level_start, ']');
    if ((level_end == NULL) || (level_end[1] != '['))
    {
        return 0U;
    }

    module_start = &level_end[1];
    module_end = strchr(module_start, ']');
    if (module_end == NULL)
    {
        return 0U;
    }

    return 1U;
}

static void UART_Safe_UpdateModuleFromFormattedLine(const char *line)
{
    const char *stamp_end;
    const char *level_end;
    const char *module_start;
    const char *module_end;
    size_t module_len;

    if (!UART_Safe_IsStandardLogLine(line))
    {
        return;
    }

    stamp_end = strstr(line, " ms]");
    level_end = strchr(&stamp_end[4], ']');
    module_start = &level_end[2];
    module_end = strchr(module_start, ']');
    if (module_end == NULL)
    {
        return;
    }

    module_len = (size_t)(module_end - module_start);
    if ((module_len == 0U) || (module_len >= UART_SAFE_MODULE_NAME_LEN))
    {
        return;
    }

    memcpy(current_module, module_start, module_len);
    current_module[module_len] = '\0';
}

static const char *UART_Safe_DetectLevel(const char *line)
{
    if ((strstr(line, "ERROR") != NULL) || (strstr(line, "Error") != NULL) || (strstr(line, "error") != NULL))
    {
        return "ERROR";
    }

    if ((strstr(line, "WARNING") != NULL) || (strstr(line, "Warning") != NULL) || (strstr(line, "warning") != NULL))
    {
        return "WARNING";
    }

    return "INFO";
}

static uint8_t UART_Safe_ExtractModulePrefix(const char *line, char *module, size_t module_size, const char **body)
{
    const char *end_bracket;
    size_t module_len;

    if ((line == NULL) || (module == NULL) || (body == NULL) || (module_size == 0U))
    {
        return 0U;
    }

    if (line[0] != '[')
    {
        return 0U;
    }

    end_bracket = strchr(line, ']');
    if ((end_bracket == NULL) || (end_bracket == (line + 1)))
    {
        return 0U;
    }

    module_len = (size_t)(end_bracket - (line + 1));
    if (module_len >= module_size)
    {
        module_len = module_size - 1U;
    }

    memcpy(module, line + 1, module_len);
    module[module_len] = '\0';

    *body = end_bracket + 1;
    while ((**body == ' ') || (**body == '\t'))
    {
        ++(*body);
    }

    return 1U;
}

static uint8_t UART_Safe_QueuePush(const char *message)
{
    uint32_t primask;
    uint8_t head_index;
    size_t message_len;

    if (message == NULL)
    {
        return 0U;
    }

    message_len = UART_Safe_Strnlen(message, MSG_MAX_LEN - 1U);
    if (message_len == 0U)
    {
        return 0U;
    }

    primask = UART_Safe_EnterCritical();

    if (queue_count >= MSG_QUEUE_SIZE)
    {
        UART_Safe_ExitCritical(primask);
        return 0U;
    }

    head_index = queue_head;
    memset(msg_queue[head_index], 0, sizeof(msg_queue[head_index]));
    memcpy(msg_queue[head_index], message, message_len);
    msg_queue[head_index][message_len] = '\0';

    queue_head = (uint8_t)((queue_head + 1U) % MSG_QUEUE_SIZE);
    ++queue_count;

    UART_Safe_ExitCritical(primask);

    return 1U;
}

static void UART_Safe_EnqueueStandardLine(const char *line)
{
    char prepared_line[MSG_MAX_LEN];
    size_t length;

    memset(prepared_line, 0, sizeof(prepared_line));
    UART_Safe_CopyString(prepared_line, line, sizeof(prepared_line));
    UART_Safe_TrimString(prepared_line);
    if (prepared_line[0] == '\0')
    {
        return;
    }

    length = strlen(prepared_line);
    if ((length + 2U) < sizeof(prepared_line))
    {
        prepared_line[length] = '\r';
        prepared_line[length + 1U] = '\n';
        prepared_line[length + 2U] = '\0';
    }

    if (UART_Safe_QueuePush(prepared_line) != 0U)
    {
        UART_Safe_UpdateModuleFromFormattedLine(prepared_line);
        UART_Safe_TrySendNext();
    }
}

static void UART_Safe_EnqueueFormattedLine(const char *line)
{
    char module[UART_SAFE_MODULE_NAME_LEN];
    char body[MSG_MAX_LEN];
    char formatted_line[MSG_MAX_LEN];
    const char *body_ptr;
    const char *level;
    int prefix_len;
    size_t copy_len;
    size_t room_len;
    uint32_t tick;

    memset(module, 0, sizeof(module));
    memset(body, 0, sizeof(body));
    memset(formatted_line, 0, sizeof(formatted_line));

    if (line[0] == '>')
    {
        UART_Safe_CopyString(module, current_module, sizeof(module));
        body_ptr = line + 1;
        while ((*body_ptr == ' ') || (*body_ptr == '\t'))
        {
            ++body_ptr;
        }
    }
    else if (UART_Safe_ExtractModulePrefix(line, module, sizeof(module), &body_ptr) != 0U)
    {
        UART_Safe_CopyString(current_module, module, sizeof(current_module));
    }
    else
    {
        UART_Safe_CopyString(module, UART_SAFE_DEFAULT_MODULE, sizeof(module));
        body_ptr = line;
    }

    UART_Safe_CopyString(body, body_ptr, sizeof(body));
    UART_Safe_TrimString(body);
    if (body[0] == '\0')
    {
        return;
    }

    level = UART_Safe_DetectLevel(body);
    tick = HAL_GetTick();

    prefix_len = snprintf(formatted_line, sizeof(formatted_line), "[%lu ms][%s][%s] ", (unsigned long)tick, level, module);
    if ((prefix_len < 0) || ((size_t)prefix_len >= sizeof(formatted_line)))
    {
        return;
    }

    room_len = sizeof(formatted_line) - (size_t)prefix_len - 3U;
    copy_len = UART_Safe_Strnlen(body, room_len);
    memcpy(&formatted_line[prefix_len], body, copy_len);
    formatted_line[prefix_len + (int)copy_len] = '\r';
    formatted_line[prefix_len + (int)copy_len + 1] = '\n';
    formatted_line[prefix_len + (int)copy_len + 2] = '\0';

    if (UART_Safe_QueuePush(formatted_line) != 0U)
    {
        UART_Safe_TrySendNext();
    }
}

static void UART_Safe_ProcessLine(const char *line)
{
    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    if (UART_Safe_IsStandardLogLine(line) != 0U)
    {
        UART_Safe_EnqueueStandardLine(line);
    }
    else
    {
        UART_Safe_EnqueueFormattedLine(line);
    }
}

static void UART_Safe_TrySendNext(void)
{
    uint32_t primask;
    uint16_t tx_length;
    uint8_t tail_index;
    HAL_StatusTypeDef status;

    if ((s_huart == NULL) || (dma_busy != 0U) || (queue_count == 0U))
    {
        return;
    }

    tx_length = 0U;

    primask = UART_Safe_EnterCritical();

    if ((dma_busy != 0U) || (queue_count == 0U))
    {
        UART_Safe_ExitCritical(primask);
        return;
    }

    dma_busy = 1U;
    tail_index = queue_tail;
    tx_length = (uint16_t)UART_Safe_Strnlen(msg_queue[tail_index], MSG_MAX_LEN - 1U);

    memset(dma_buffer, 0, sizeof(dma_buffer));
    if (tx_length > 0U)
    {
        memcpy(dma_buffer, msg_queue[tail_index], tx_length);
    }

    queue_tail = (uint8_t)((queue_tail + 1U) % MSG_QUEUE_SIZE);
    --queue_count;

    UART_Safe_ExitCritical(primask);

    if (tx_length == 0U)
    {
        dma_busy = 0U;
        return;
    }

    status = HAL_UART_Transmit_DMA(s_huart, dma_buffer, tx_length);
    if (status != HAL_OK)
    {
        primask = UART_Safe_EnterCritical();

        if (queue_count < MSG_QUEUE_SIZE)
        {
            queue_tail = (queue_tail == 0U) ? (MSG_QUEUE_SIZE - 1U) : (queue_tail - 1U);
            memcpy(msg_queue[queue_tail], dma_buffer, tx_length);
            msg_queue[queue_tail][tx_length] = '\0';
            ++queue_count;
        }

        dma_busy = 0U;
        UART_Safe_ExitCritical(primask);
    }
}

static void UART_Safe_ServiceRxRestart(void)
{
    if (rx_restart_pending == 0U)
    {
        return;
    }

    if (UART_Safe_StartReceiveToIdle() == HAL_OK)
    {
        rx_restart_pending = 0U;
    }
}

static void UART_Safe_ServiceResetRequest(void)
{
    if (reset_requested == 0U)
    {
        return;
    }

    if ((int32_t)(HAL_GetTick() - reset_due_tick) >= 0)
    {
        reset_requested = 0U;
        NVIC_SystemReset();
    }
}

void UART_Safe_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(main_buffer, 0, sizeof(main_buffer));
    memset(msg_queue, 0, sizeof(msg_queue));
    memset(dma_buffer, 0, sizeof(dma_buffer));

    queue_head = 0U;
    queue_tail = 0U;
    queue_count = 0U;
    dma_busy = 0U;
    data_received_flag = 0U;
    received_length = 0U;
    rx_restart_pending = 0U;
    task_running = 0U;
    task_start_tick = 0U;
    task_last_duration_ms = 0U;
    reset_requested = 0U;
    reset_due_tick = 0U;

    UART_Safe_CopyString(current_module, UART_SAFE_DEFAULT_MODULE, sizeof(current_module));

    if (UART_Safe_StartReceiveToIdle() != HAL_OK)
    {
        Error_Handler();
    }
}

void UART_Safe_Transmit(const char *message)
{
    char line_buffer[UART_SAFE_DMA_BUFFER_SIZE];
    size_t line_index;
    char current_char;

    if (message == NULL)
    {
        return;
    }

    memset(line_buffer, 0, sizeof(line_buffer));
    line_index = 0U;

    while (1)
    {
        current_char = *message;

        if ((current_char == '\r') || (current_char == '\n') || (current_char == '\0'))
        {
            if (line_index > 0U)
            {
                line_buffer[line_index] = '\0';
                UART_Safe_TrimString(line_buffer);
                if (line_buffer[0] != '\0')
                {
                    UART_Safe_ProcessLine(line_buffer);
                }
                line_index = 0U;
                memset(line_buffer, 0, sizeof(line_buffer));
            }

            if (current_char == '\0')
            {
                break;
            }
        }
        else if (line_index < (sizeof(line_buffer) - 1U))
        {
            line_buffer[line_index] = current_char;
            ++line_index;
        }

        ++message;
    }
}

void Process_UART_Commands(void)
{
    uint32_t primask;
    char local_buffer[UART_SAFE_MAIN_BUFFER_SIZE];
    char log_buffer[MSG_MAX_LEN];

    UART_Safe_TrySendNext();
    UART_Safe_ServiceRxRestart();
    UART_Safe_ServiceResetRequest();

    if (data_received_flag == 0U)
    {
        return;
    }

    memset(local_buffer, 0, sizeof(local_buffer));

    primask = UART_Safe_EnterCritical();
    if (data_received_flag != 0U)
    {
        memcpy(local_buffer, main_buffer, sizeof(main_buffer));
        memset(main_buffer, 0, sizeof(main_buffer));
        data_received_flag = 0U;
        received_length = 0U;
    }
    UART_Safe_ExitCritical(primask);

    UART_Safe_TrimString(local_buffer);
    if (local_buffer[0] == '\0')
    {
        return;
    }

    if (strstr(local_buffer, "Start") != NULL)
    {
        task_running = 1U;
        task_start_tick = HAL_GetTick();
        UART_Safe_Transmit("[UART] Start command accepted");
    }
    else if (strstr(local_buffer, "Stop") != NULL)
    {
        if (task_running != 0U)
        {
            task_last_duration_ms = HAL_GetTick() - task_start_tick;
        }
        else
        {
            task_last_duration_ms = 0U;
        }

        task_running = 0U;

        snprintf(log_buffer, sizeof(log_buffer), "[UART] Stop command accepted, duration = %lu ms", (unsigned long)task_last_duration_ms);
        UART_Safe_Transmit(log_buffer);
    }
    else if (strstr(local_buffer, "ReSet System") != NULL)
    {
        reset_requested = 1U;
        reset_due_tick = HAL_GetTick() + UART_SAFE_RESET_DELAY_MS;
        UART_Safe_Transmit("[UART] Warning: system reset scheduled");
    }
    else if (strstr(local_buffer, "SetDAC:") != NULL)
    {
        float target_val = 4.00f;
        char *p_val = strchr(local_buffer, ':');
        if (p_val != NULL)
        {
            p_val++;
            char *endptr;
            target_val = strtof(p_val, &endptr);
            if (p_val != endptr)
            {
                DAC_Control_UpdatePfcTargetCurrent(target_val);
                snprintf(log_buffer, sizeof(log_buffer), "[UART] DAC current set to %.2f A", target_val);
                UART_Safe_Transmit(log_buffer);
            }
            else
            {
                UART_Safe_Transmit("[UART] ERROR: Invalid DAC current value");
            }
        }
    }
    else if ((strstr(local_buffer, "TestADC") != NULL) || (strstr(local_buffer, "TEST_ADC") != NULL))
    {
        UART_Safe_Transmit("[UART] Executing ADC Functional Test...");
        Measure_ADC_FunctionalTest();
    }
    else if (strstr(local_buffer, "SetPeriod:") != NULL)
    {
        /* 动态修改报告周期: SetPeriod:100 */
        char *p_val = strchr(local_buffer, ':');
        if (p_val != NULL)
        {
            uint16_t new_period = (uint16_t)atoi(p_val + 1);
            if (new_period >= 20U && new_period <= 5000U)
            {
                extern volatile uint16_t g_report_period_ms;
                g_report_period_ms = new_period;
                snprintf(log_buffer, sizeof(log_buffer),
                         "[UART] Report period set to %u ms", (unsigned int)new_period);
            }
            else
            {
                snprintf(log_buffer, sizeof(log_buffer),
                         "[UART] ERROR: Period must be 20~5000 ms");
            }
            UART_Safe_Transmit(log_buffer);
        }
    }
    else if (strstr(local_buffer, "Status") != NULL)
    {
        /* 系统状态查询命令 */
        extern volatile uint16_t g_report_period_ms;
        extern volatile uint8_t  g_protection_enabled;
        const Measure_Data_t *p_data = Measure_GetDataPtr();

        UART_Safe_Transmit("=================== System Status ===================");
        snprintf(log_buffer, sizeof(log_buffer),
                 "[Status] Report:%ums Prot:%s Buf:%u%%",
                 (unsigned int)g_report_period_ms,
                 g_protection_enabled ? "ON" : "OFF",
                 (unsigned int)UART_Safe_GetBufferUsage());
        UART_Safe_Transmit(log_buffer);

        if (p_data != NULL && p_data->data_valid)
        {
            snprintf(log_buffer, sizeof(log_buffer),
                     "[Status] V1:%.1fV V2:%.1fV CO:%.2fA VO:%.1fV T:%.1fC Vref:%.4fV",
                     p_data->v1_in, p_data->v2_in, p_data->co_out,
                     p_data->vo_out, p_data->temp_c, p_data->vref_plus);
            UART_Safe_Transmit(log_buffer);
            snprintf(log_buffer, sizeof(log_buffer),
                     "[Status] ADC update count: %lu", (unsigned long)p_data->update_cnt);
            UART_Safe_Transmit(log_buffer);
        }
        snprintf(log_buffer, sizeof(log_buffer),
                 "[Status] DAC target: %.2f A", DAC_Control_GetPfcTargetCurrent());
        UART_Safe_Transmit(log_buffer);
        snprintf(log_buffer, sizeof(log_buffer),
                 "[Status] Task running: %s", task_running ? "YES" : "NO");
        UART_Safe_Transmit(log_buffer);
        UART_Safe_Transmit("====================================================");
    }
    else
    {
        snprintf(log_buffer, sizeof(log_buffer), "[UART] Warning: unknown command -> %s", local_buffer);
        UART_Safe_Transmit(log_buffer);
    }
}

uint8_t UART_Safe_GetBufferUsage(void)
{
    return (uint8_t)((queue_count * 100U) / MSG_QUEUE_SIZE);
}

void UART_Safe_FlushBuffer(void)
{
    uint32_t primask;

    if (s_huart != NULL)
    {
        (void)HAL_UART_AbortTransmit(s_huart);
    }

    primask = UART_Safe_EnterCritical();
    queue_head = 0U;
    queue_tail = 0U;
    queue_count = 0U;
    dma_busy = 0U;
    memset(msg_queue, 0, sizeof(msg_queue));
    memset(dma_buffer, 0, sizeof(dma_buffer));
    UART_Safe_ExitCritical(primask);
}

uint8_t UART_Safe_IsTaskRunning(void)
{
    return task_running;
}

uint32_t UART_Safe_GetLastDurationMs(void)
{
    return task_last_duration_ms;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uint16_t safe_size;

    if ((huart != s_huart) || (huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    /* 先显式终止接收，确保 DMA 计数器与 HAL 状态机被完整复位。 */
    (void)HAL_UART_AbortReceive(huart);

    safe_size = Size;
    if (safe_size > UART_SAFE_RX_BUFFER_SIZE)
    {
        safe_size = UART_SAFE_RX_BUFFER_SIZE;
    }

    memset(main_buffer, 0, sizeof(main_buffer));
    if (safe_size > 0U)
    {
        memcpy(main_buffer, rx_buffer, safe_size);
    }
    main_buffer[safe_size] = '\0';

    received_length = safe_size;
    data_received_flag = 1U;

    memset(rx_buffer, 0, sizeof(rx_buffer));

    if (UART_Safe_StartReceiveToIdle() != HAL_OK)
    {
        rx_restart_pending = 1U;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != s_huart) || (huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    dma_busy = 0U;
    UART_Safe_TrySendNext();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart != s_huart) || (huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    dma_busy = 0U;
    (void)HAL_UART_AbortReceive(huart);
    memset(rx_buffer, 0, sizeof(rx_buffer));

    if (UART_Safe_StartReceiveToIdle() != HAL_OK)
    {
        rx_restart_pending = 1U;
    }

    UART_Safe_TrySendNext();
}
