#ifndef __LINE_LIMIT_H__
#define __LINE_LIMIT_H__

#include "main.h"

typedef enum
{
    LINE_LIMIT_STATUS_OK = 0,
    LINE_LIMIT_STATUS_NEED_RETEST = 1,
    LINE_LIMIT_STATUS_INVALID_ARG = 2
} LineLimit_Status_t;

typedef struct
{
    float ik_const;
    float efficiency;
    float r_min_ohm;
    float r_max_ohm;
    float r_delta_max_ohm;
    float single_ch_max_a;
    float dual_ch_max_a;
    float ac_online_threshold_v;
    float min_valid_vac_v;
    float min_valid_vdc_v;
    float idc_fullscale_a;
    uint16_t dac_fullscale_code;
} LineLimit_Config_t;

typedef struct
{
    float r_calc_ohm;
    float r_last_locked_ohm;
    uint8_t r_last_valid;       /* 上次锁定阻值是否有效（首次测量为 0，跳过波动校验） */
    float vac_ch1_sample_v;
    float vac_ch2_sample_v;
    float vdc_sample_v;

    uint8_t online_channel_count;
    float r_new_locked_ohm;
    float iac_theoretical_a;
    float iac_limited_a;
    float vac_sample_v;
    float input_power_w;
    float output_power_w;
    float idc_max_limit_a;
    uint16_t dac_code;
} LineLimit_State_t;

void LineLimit_GetDefaultConfig(LineLimit_Config_t *cfg);
LineLimit_Status_t Process_AcLine_Limit(const LineLimit_Config_t *cfg,
                                        LineLimit_State_t *state);

#endif /* __LINE_LIMIT_H__ */
