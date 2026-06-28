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
    /* 交流线阻限功率算法输入采样 */
    float vac_ch1_v;       /* 交流通道 1 电压采样 */
    float vac_ch2_v;       /* 交流通道 2 电压采样 */
    float vdc_sample_v;    /* 直流母线电压采样 */
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
    float ioc;  /* 当前闭环目标 IOC；无效时为 0A */
    uint8_t ioc_valid;    /* 当前是否已建立有效闭环 IOC 目标 */
    uint8_t ioc_updated;  /* 本次 update 是否刚刚生成新的 IOC 目标 */
    /* 交流线阻限功率诊断输出 */
    float iac_limit_a;
    float idc_limit_a;
    uint16_t dac_code;
    uint8_t need_retest;
    float du_v;            /* 本次锁存点电压差绝对值 */
    float di_a;            /* 本次锁存点电流差 */
    float r_raw_ohm;       /* 未经过额外状态裁剪的原始电阻计算值 */
    float vac_eff_v;       /* 本次闭环采用的有效输入电压 */
    float vout_avg_v;      /* 本次闭环采用的平均输出电压 */
    float pin_w;           /* 本次估算的输入功率 */
    float pout_w;          /* 本次估算的输出功率 */
    /* 3A 稳定判定上报信息 */
    uint8_t stable_update;      /* 稳定判定数据更新标志，每次 WAIT_3A_STABLE 采样时置 1 */
    uint8_t stable_count;        /* 当前连续稳定次数 0~STABLE_CONSECUTIVE_COUNT */
    uint8_t stable_target;      /* 稳定目标次数 = STABLE_CONSECUTIVE_COUNT */
    float stable_delta_ma;       /* 当前电流差值，单位 mA */
    uint32_t stable_wait_ms;     /* 自进入 WAIT_3A_STABLE 以来的等待时间，单位 ms */
    /* 开路检测错误标志 */
    uint8_t open_circuit_error;  /* 检测到开路或电流过低的错误标志 */
} Calc_Control_Output_t;

/* 等待电流稳定时长常数定义 */
#define STEP_3A_STABLE_MS           100U
#define STEP_2A_STABLE_MS           10000U

/* 电流稳定判定参数定义 */
#define CURRENT_DELTA_THRESHOLD_MA   500U   /* 电流差值判定阈值，单位：mA */
#define STABLE_CONSECUTIVE_COUNT    5U     /* 连续稳定判定次数 */
#define STEP_3A_SAMPLE_INTERVAL_MS  100U    /* 电流采样间隔，单位：ms */
#define STEP_3A_POLL_INTERVAL_MS    10U     /* 状态机轮询周期，单位：ms */
#define CURRENT_MIN_THRESHOLD_MA     1000U   /* 最小电流阈值，开路检测用，单位：mA */
#define CALC_CONTROL_IOC_GAIN_K      1.0f   /* 阶段 A 闭环 IOC 增益 */
#define CALC_CONTROL_IOC_MIN_A       0.1f   /* 闭环 IOC 最小钳位 */
#define CALC_CONTROL_IOC_MAX_A       15.0f  /* 闭环 IOC 最大钳位 */

void Calc_Control_Init(void);
void Calc_Control_Update(const Calc_Control_Input_t *input, Calc_Control_Output_t *output);
Calc_Control_State_t Calc_Control_GetState(void);
uint8_t Calc_Control_IsClosedLoopActive(void);
uint8_t Calc_Control_IsVocForceActive(void);

/* 2A 等待挂起/恢复 API */
/**
 * @brief  挂起 2A 稳定等待计时（仅在 WAIT_2A_STABLE 状态生效）
 * @return 1 = 挂起成功，0 = 当前不在 WAIT_2A_STABLE 状态，无操作
 */
uint8_t Calc_Control_PauseWait2A(uint32_t tick_ms);

/**
 * @brief  恢复 2A 稳定等待计时（从挂起点继续）
 * @return 1 = 恢复成功，0 = 未处于挂起状态，无操作
 */
uint8_t Calc_Control_ResumeWait2A(uint32_t tick_ms);

/**
 * @brief  查询当前是否处于 2A 稳定等待挂起状态
 * @return 1 = 已挂起，0 = 未挂起
 */
uint8_t Calc_Control_IsWait2APaused(void);

#endif /* __CALC_CONTROL_H__ */
