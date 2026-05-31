# FreeRTOS Include Definitions 配置

## 推荐配置说明

| Include definitions | 默认值 | 推荐值 | 说明 |
|---|:---:|:---:|---|
| `vTaskPrioritySet` | Enabled | **Enabled** ✅ | 动态调整任务优先级 |
| `uxTaskPriorityGet` | Enabled | **Enabled** ✅ | 获取任务优先级 |
| `vTaskDelete` | Enabled | **Enabled** ✅ | 删除任务释放资源 |
| `vTaskCleanupResources` | Disabled | **Disabled** ✅ | 很少需要，节省代码 |
| `vTaskSuspend` | Enabled | **Enabled** ✅ | 挂起任务（暂停/恢复） |
| `vTaskDelayUntil` | Enabled | **Enabled** ✅ | 精确周期性延时，**核心必需** |
| `vTaskDelay` | Enabled | **Enabled** ✅ | 普通延时 |
| `xTaskGetSchedulerState` | Enabled | **Enabled** ✅ | 检查调度器状态 |
| `xTaskResumeFromISR` | Enabled | **Enabled** ✅ | 从中断恢复任务，**必需** |
| `xQueueGetMutexHolder` | Enabled | **Enabled** ✅ | 获取互斥锁持有者 |
| `xSemaphoreGetMutexHolder` | Disabled | **Disabled** ✅ | 与上面重复，保持禁用 |
| `pcTaskGetTaskName` | Disabled | **Disabled** ✅ | 调试用，节省代码 |
| `uxTaskGetStackHighWaterMark` | Enabled | **Enabled** ✅ | 调试时检查栈使用，**建议保留** |
| `xTaskGetCurrentTaskHandle` | Enabled | **Enabled** ✅ | 获取当前任务句柄 |
| `eTaskGetState` | Enabled | **Enabled** ✅ | 获取任务状态 |
| `xEventGroupSetBitFromISR` | Disabled | **Disabled** ✅ | 不使用事件组，保持禁用 |
| `xTimerPendFunctionCall` | Enabled | **Enabled** ✅ | 定时器回调，**核心必需** |
| `xTaskAbortDelay` | Disabled | **Disabled** ✅ | 调试用，节省代码 |
| `xTaskGetHandle` | Disabled | **Disabled** ✅ | 调试用，节省代码 |
| `uxTaskGetStackHighWaterMark2` | Disabled | **Disabled** ✅ | 调试用，节省代码 |

## 分类说明

### ✅ 核心必选（保持 Enabled）

| 函数 | 用途 |
|------|------|
| `vTaskDelayUntil` | 精确延时，周期性任务必需 |
| `vTaskDelay` | 普通延时 |
| `vTaskSuspend` / `vTaskResumeFromISR` | 任务暂停/恢复 |
| `vTaskPrioritySet` / `uxTaskPriorityGet` | 优先级管理 |
| `xTimerPendFunctionCall` | 定时器回调 |
| `vTaskDelete` | 资源清理 |

### ❌ 可禁用（节省代码空间）

| 函数 | 原因 |
|------|------|
| `vTaskCleanupResources` | 很少使用 |
| `xSemaphoreGetMutexHolder` | 与 `xQueueGetMutexHolder` 重复 |
| `pcTaskGetTaskName` | 调试用 |
| `xEventGroupSetBitFromISR` | 不使用事件组 |
| `xTaskAbortDelay` | 很少使用 |
| `xTaskGetHandle` | 调试用 |
| `uxTaskGetStackHighWaterMark2` | 调试用，与 1 不同版本 |