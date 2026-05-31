# LOGIC uart_safe (UART 安全逻辑)

## 目的

`uart_safe` 是本代码库中主要的手动维护的通信辅助模块。它将 `USART1` 转换为两个实用的通道：

1. 使用 DMA + 接收至空闲 (receive-to-idle) 的非阻塞命令接收通道
2. 使用队列化 DMA 发送路径的非阻塞日志输出通道

事实来源：
- `Core/Src/uart_safe.c`
- `Core/Inc/uart_safe.h`

## 在系统中的公共角色

主要集成点：
- `UART_Safe_Init(UART_HandleTypeDef *huart)`
- `UART_Safe_Transmit(const char *message)`
- `Process_UART_Commands(void)`
- `UART_Safe_GetBufferUsage(void)`
- `UART_Safe_FlushBuffer(void)`
- `UART_Safe_IsTaskRunning(void)`
- `UART_Safe_GetLastDurationMs(void)`

主循环使用此模块来：
- 启动 (arm) 串口接收
- 解析传入的命令
- 决定是否应翻转 `OF_EN` 引脚
- 输出格式化日志而不会阻塞前台循环

## RX (接收) 行为

### 传输模型
- 在 `USART1` 上使用 `HAL_UARTEx_ReceiveToIdle_DMA()`
- RX DMA 缓冲区大小：`32` 字节
- 主解析缓冲区大小：`33` 字节（包含字符串终止符）

### RX 事件处理
当 `HAL_UARTEx_RxEventCallback()` 触发时：
1. 驱动程序过滤检查是否为配置的 `USART1` 句柄
2. 终止当前接收状态以复位 DMA/HAL 接收状态
3. 将接收到的数据大小限制在固定缓冲区范围内
4. 将数据复制到主解析缓冲区
5. 追加 `\0`
6. 设置接收标志并存储接收长度
7. 清空原始 RX 缓冲区
8. 重新启动接收至空闲 (receive-to-idle) DMA

如果重新启动失败，该模块将设置一个挂起的重新启动标志，并从前台路径重试。

### RX 恢复
`HAL_UART_ErrorCallback()` 会终止接收，清空 RX 缓冲区，尝试重新启动接收至空闲 DMA，如果未立即成功启动，则允许前台重试。

## TX (发送) 行为

### 队列模型
- 环形队列深度：`32`
- 最大存储行长度：`200` 字节
- 独立的 DMA 发送缓冲区：`256` 字节
- 如果队列已满，新的日志将被丢弃而不是阻塞等待

### 消息规范化
`UART_Safe_Transmit()` 接受多行输入，并在 `\r` / `\n` 处进行拆分。

对于每一个逻辑行：
- 如果它看起来已经像是一个标准的格式化日志行，则直接将其加入队列。
- 否则，它将被规范化为：
  - `[<tick> ms][<LEVEL>][<MODULE>] <body>`

### 日志级别检测
当前的逻辑从消息文本中推导日志级别：
- 包含 `ERROR` / `Error` / `error` -> `ERROR`
- 包含 `WARNING` / `Warning` / `warning` -> `WARNING`
- 否则 -> `INFO`

### 模块处理
- 默认模块名称：`System`
- 如果某行以 `[MODULE]` 开头，该模块名称将被提取并成为当前的模块上下文。
- 如果某行以 `>` 开头，该行将继承先前记住的模块名称。

### DMA 发送调度
TX 调度器：
- 如果 DMA 繁忙或队列为空，则快速退出
- 进入一个屏蔽中断的短暂临界区
- 在移除队列头部数据之前将 DMA 标记为繁忙
- 将选定的日志行复制到 DMA 缓冲区中
- 在临界区之外启动 `HAL_UART_Transmit_DMA()`
- 如果 DMA 启动失败，则将消息回滚到队列中

### TX 完成
`HAL_UART_TxCpltCallback()` 会清除繁忙标志，并立即尝试发送队列中的下一条消息。

## 命令行为

`Process_UART_Commands()` 旨在用于从主循环进行前台轮询。

支持的命令子字符串：
- `Start`
- `Stop`
- `ReSet System`
- `SetDAC:`
- `TestADC` / `TEST_ADC`
- `SetPeriod:` (动态报告周期)
- `Status` (系统状态查询)

### `Start`
- 设置 `task_running = 1`
- 存储 `task_start_tick = HAL_GetTick()`
- 输出一条确认该命令的日志

### `Stop`
- 如果任务正在运行，则计算自 `task_start_tick` 以来经过的时间
- 以毫秒为单位存储最后一次运行的持续时间
- 清除 `task_running`
- 输出一条包含测量的持续时间的日志

### `ReSet System`
- 设置延迟复位请求
- 在 `UART_SAFE_RESET_DELAY_MS` 之后调度复位
- 在复位之前输出一条警告日志
- 实际的复位稍后在使用 `NVIC_SystemReset()` 的前台服务逻辑中执行

### `SetDAC:`
- 提取并解析传入的设定电流参数（例如 `SetDAC:4.50`）
- 限制设定电流在 0.0A 到 10.0A 的安全范围
- 动态更新内存中的目标 PFC 电流设定值，供 50ms DAC 定时调度任务实时输出
- 输出设定成功确认或错误提示日志

### `TestADC` / `TEST_ADC`
- 触发高精度 ADC 6 通道功能测试
- 计算 6 个常规转换通道（Rank 1 ~ Rank 6）在引脚端的实际模拟电压值（基于实时校准的 $V_{\text{REF+}}$）
- 自动检测并输出片上温度计算结果和内部参考电压理论对照值
- 使用安全串口队列格式化输出完整的通道测试报告，不阻塞主循环前台

### `SetPeriod:<ms>`
- 动态修改测量数据报告周期（毫秒）
- 有效范围：20 ~ 5000 ms
- 示例：`SetPeriod:100` 将报告周期设置为 100ms
- 修改后立即生效，无需复位

### `Status`
- 查询系统当前状态摘要
- 输出内容：报告周期、保护状态、UART 缓冲区使用率
- 输出内容：最新测量值（电压、电流、温度、VREF）
- 输出内容：ADC 更新计数、DAC 目标电流、任务运行状态

### 未知命令
- 未知输入会被记录为警告日志，并且不会阻塞系统。

## 前台交互

`Process_UART_Commands()` 还提供以下服务：
- 推进挂起的 TX 队列
- 延迟的 RX 重新启动尝试
- 延迟的复位请求

这使 ISR (中断服务例程) 的工作保持简短，并将较重的控制行为推给主循环。

## 运行意义

该模块是目前主要的自定义运行时行为，未来的贡献者和智能体在更改以下内容之前必须理解该模块：
- 串口命令协议
- 日志格式化
- DMA 发送策略
- 主循环任务的启用/禁用语义

## 何时更新此文档

在发生以下更改后进行更新：
- 支持的命令字符串
- RX/TX 缓冲区大小调整
- 队列行为
- DMA 调度行为
- 复位时序
- 日志格式化或模块继承规则
