#ifndef __CALC_CONTROL_H__
#define __CALC_CONTROL_H__

#include "main.h"

/* 内阻计算九段状态机状态 */
typedef enum {
    CALC_CONTROL_WAIT_SAFE = 0,
    CALC_CONTROL_SET_3A = 1,
    CALC_CONTROL_WAIT_3A_STABLE = 2,
    CALC_CONTROL_LATCH_3A = 3,
    CALC_CONTROL_SET_2A = 4,
    CALC_CONTROL_WAIT_2A_STABLE = 5,
    CALC_CONTROL_LATCH_2A = 6,
    CALC_CONTROL_CALC_RESISTANCE = 7,
    CALC_CONTROL_MONITOR = 8
} Calc_Control_State_t;

/* 输入参数 */
typedef struct {
    float vo_out_v;
    float co_out_a;
    uint32_t tick_ms;
    uint8_t safe_allowed;  /* 保护状态机是否处于 NORMAL 状态 */
    uint8_t reset_request; /* 外部重置请求 */
} Calc_Control_Input_t;

/* 输出动作 */
typedef struct {
    float set_current_a;
    uint8_t change_current;
    uint8_t publish_calc_report;
    uint8_t enter_monitor;
    float calculated_resistance;
    float u1;
    float i1;
    float u2;
    float i2;
} Calc_Control_Output_t;

/* 等待电流稳定时长常数定义 */
#define STEP_3A_STABLE_MS           100U
#define STEP_2A_STABLE_MS           100U

void Calc_Control_Init(void);
void Calc_Control_Update(const Calc_Control_Input_t *input, Calc_Control_Output_t *output);
Calc_Control_State_t Calc_Control_GetState(void);

#endif /* __CALC_CONTROL_H__ */
