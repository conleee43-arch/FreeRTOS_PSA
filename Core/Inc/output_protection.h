#ifndef __OUTPUT_PROTECTION_H__
#define __OUTPUT_PROTECTION_H__

#include "main.h"

/* 状态枚举 */
typedef enum {
    OUTPUT_PROTECTION_NORMAL = 0,
    OUTPUT_PROTECTION_TRIPPED = 1,
    OUTPUT_PROTECTION_RECOVERY_WAIT = 2
} Output_Protection_State_t;

/* 输入参数 */
typedef struct {
    float temperature_c;
    float vo_out_v;
    float co_out_a;
    uint32_t tick_ms;
} Output_Protection_Input_t;

/* 输出动作 */
typedef struct {
    uint8_t disable_output;
    uint8_t enable_output;
    uint8_t reset_calc_control;
} Output_Protection_Output_t;

/* 保护阈值与迟滞点常数定义 */
#define OTP_TRIP_THRESHOLD          85.0f   /* 过温跳闸：>= 85°C */
#define OTP_RECOVERY_THRESHOLD      75.0f   /* 过温恢复：< 75°C */

#define OVP_TRIP_THRESHOLD          520.0f  /* 过压跳闸：>= 520V */
#define OCP_TRIP_THRESHOLD          12.0f   /* 过流跳闸：>= 12A */

#define RECOVERY_OBSERVE_MS         2000U   /* 自恢复观察期 2000ms */

void Output_Protection_Init(void);
void Output_Protection_Update(const Output_Protection_Input_t *input, Output_Protection_Output_t *output);
Output_Protection_State_t Output_Protection_GetState(void);

#endif /* __OUTPUT_PROTECTION_H__ */
