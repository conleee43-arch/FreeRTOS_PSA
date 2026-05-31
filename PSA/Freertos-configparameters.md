| 参数类别 / 配置项 (Parameters / Configuration) | 设置值 (Value) |
|---|---|
| API | |
| └─ FreeRTOS API | CMSIS v2 |
| Versions | |
| └─ FreeRTOS version | 10.3.1 |
| └─ CMSIS-RTOS version | 2.00 |
| MPU/FPU | |
| └─ ENABLE_MPU | Disabled |
| └─ ENABLE_FPU | Disabled |
| Kernel settings | |
| └─ USE_PREEMPTION | Enabled |
| └─ CPU_CLOCK_HZ | SystemCoreClock |
| └─ TICK_RATE_HZ | 1000 |
| └─ MAX_PRIORITIES | 56 |
| └─ MINIMAL_STACK_SIZE | 128 Words |
| └─ MAX_TASK_NAME_LEN | 16 → **24** |
| └─ USE_16_BIT_TICKS | Disabled |
| └─ IDLE_SHOULD_YIELD | Enabled |
| └─ USE_MUTEXES | Enabled |
| └─ USE_RECURSIVE_MUTEXES | Enabled → **Disabled** |
| └─ USE_COUNTING_SEMAPHORES | Enabled |
| └─ QUEUE_REGISTRY_SIZE | 8 |
| └─ USE_APPLICATION_TASK_TAG | Disabled |
| └─ ENABLE_BACKWARD_COMPATIBILITY | Enabled |
| └─ USE_PORT_OPTIMISED_TASK_SELECTION | Disabled |
| └─ USE_TICKLESS_IDLE | Disabled |
| └─ USE_TASK_NOTIFICATIONS | Enabled |
| └─ RECORD_STACK_HIGH_ADDRESS | Disabled |
| Memory management settings | |
| └─ Memory Allocation | Dynamic / Static → **Dynamic only** |
| └─ TOTAL_HEAP_SIZE | 16384 → **24576** Bytes |
| └─ Memory Management scheme | heap_4 |
| Hook function related definitions | |
| └─ USE_IDLE_HOOK | Disabled |
| └─ USE_TICK_HOOK | Disabled |
| └─ USE_MALLOC_FAILED_HOOK | Disabled |
| └─ USE_DAEMON_TASK_STARTUP_HOOK | Disabled |
| └─ CHECK_FOR_STACK_OVERFLOW | Disabled → **Option 1** ⚠️ |
| Run time and task stats gathering related definitions | |
| └─ GENERATE_RUN_TIME_STATS | Disabled → **Enabled** |
| └─ USE_TRACE_FACILITY | Enabled |
| └─ USE_STATS_FORMATTING_FUNCTIONS | Disabled → **Enabled** |
| Co-routine related definitions | |
| └─ USE_CO_ROUTINES | Disabled |
| └─ MAX_CO_ROUTINE_PRIORITIES | 2 |
| Software timer definitions | |
| └─ USE_TIMERS | Enabled |
| └─ TIMER_TASK_PRIORITY | 2 |
| └─ TIMER_QUEUE_LENGTH | 10 |
| └─ TIMER_TASK_STACK_DEPTH | 256 Words |
| Interrupt nesting behaviour configuration | |
| └─ LIBRARY_LOWEST_INTERRUPT_PRIORITY | 15 |
| └─ LIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY | 5 |
| Added with 10.2.1 support | |
| └─ MESSAGE_BUFFER_LENGTH_TYPE | size_t |
| └─ USE_POSIX_ERRNO | Disabled |
| CMSIS-RTOS V2 flags | |
| └─ USE_OS2_THREAD_SUSPEND_RESUME | Enabled |
| └─ USE_OS2_THREAD_ENUMERATE | Enabled |
| └─ USE_OS2_EVENTFLAGS_FROM_ISR | Enabled |
| └─ USE_OS2_THREAD_FLAGS | Enabled |
| └─ USE_OS2_TIMER | Enabled |
| └─ USE_OS2_MUTEX | Enabled |


