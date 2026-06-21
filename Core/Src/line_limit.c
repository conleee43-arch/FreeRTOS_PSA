/**
 * @file    line_limit.c
 * @brief   交流线阻闭环限功率算法（纯计算，无计时/无外部依赖）。
 *
 * 安全链共 5 个阶段：
 *   1. 阻抗范围 / 波动校验
 *   2. 理论交流最大电流
 *   3. 单 / 双通道在线统计与硬件限幅
 *   4. 交流到直流功率折算
 *   5. DAC 线性量化（含饱和）
 *
 * 任意阶段不满足安全前提时返回 NEED_RETEST，调用方据此回到内阻重测流程。
 */

#include "line_limit.h"

void LineLimit_GetDefaultConfig(LineLimit_Config_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }

    cfg->ik_const = 20.0f;
    cfg->efficiency = 0.98f;
    cfg->r_min_ohm = 0.2f;
    cfg->r_max_ohm = 15.0f;
    cfg->r_delta_max_ohm = 0.5f;
    cfg->single_ch_max_a = 10.0f;
    cfg->dual_ch_max_a = 15.0f;
    cfg->ac_online_threshold_v = 50.0f;
    cfg->min_valid_vac_v = 50.0f;
    cfg->min_valid_vdc_v = 10.0f;
    cfg->idc_fullscale_a = 15.0f;
    cfg->dac_fullscale_code = 4095U;
}

LineLimit_Status_t Process_AcLine_Limit(const LineLimit_Config_t *cfg,
                                        LineLimit_State_t *state)
{
    float r_calc;
    float r_delta;
    uint8_t ch1_online;
    uint8_t ch2_online;
    float hw_limit_a;
    float vac_sample;
    float idc;

    if ((cfg == NULL) || (state == NULL))
    {
        return LINE_LIMIT_STATUS_INVALID_ARG;
    }

    /* 默认输出清零，保证任意提前返回路径输出确定。 */
    state->online_channel_count = 0U;
    state->r_new_locked_ohm = 0.0f;
    state->iac_theoretical_a = 0.0f;
    state->iac_limited_a = 0.0f;
    state->vac_sample_v = 0.0f;
    state->input_power_w = 0.0f;
    state->output_power_w = 0.0f;
    state->idc_max_limit_a = 0.0f;
    state->dac_code = 0U;

    /* 阶段 1：阻抗范围和波动校验 */
    r_calc = state->r_calc_ohm;
    if ((r_calc < cfg->r_min_ohm) || (r_calc > cfg->r_max_ohm))
    {
        return LINE_LIMIT_STATUS_NEED_RETEST;
    }
    /* 仅当上次锁定阻值有效时才做波动校验；首次测量（无有效历史）跳过，
     * 避免与初始/复位清零的 r_last_locked_ohm(=0) 比较导致首次有效值被误判。 */
    if ((state->r_last_valid != 0U) && (state->r_last_locked_ohm > 0.0f))
    {
        r_delta = r_calc - state->r_last_locked_ohm;
        if (r_delta < 0.0f)
        {
            r_delta = -r_delta;
        }
        if (r_delta > cfg->r_delta_max_ohm)
        {
            return LINE_LIMIT_STATUS_NEED_RETEST;
        }
    }
    state->r_new_locked_ohm = r_calc;

    /* 阶段 2：理论交流最大电流 */
    if (state->r_new_locked_ohm <= 0.0f)
    {
        return LINE_LIMIT_STATUS_NEED_RETEST;
    }
    state->iac_theoretical_a = cfg->ik_const / state->r_new_locked_ohm;

    /* 阶段 3：单 / 双通道在线统计 */
    ch1_online = (state->vac_ch1_sample_v > cfg->ac_online_threshold_v) ? 1U : 0U;
    ch2_online = (state->vac_ch2_sample_v > cfg->ac_online_threshold_v) ? 1U : 0U;
    state->online_channel_count = (uint8_t)(ch1_online + ch2_online);
    if (state->online_channel_count == 0U)
    {
        return LINE_LIMIT_STATUS_NEED_RETEST;
    }
    state->iac_limited_a = state->iac_theoretical_a;

    /* 阶段 4：输入功率与输出功率折算 */
    if (state->online_channel_count == 2U)
    {
        vac_sample = (state->vac_ch1_sample_v + state->vac_ch2_sample_v) * 0.5f;
    }
    else if (ch1_online != 0U)
    {
        vac_sample = state->vac_ch1_sample_v;
    }
    else
    {
        vac_sample = state->vac_ch2_sample_v;
    }
    state->vac_sample_v = vac_sample;
    if ((vac_sample < cfg->min_valid_vac_v) ||
        (state->vdc_sample_v < cfg->min_valid_vdc_v))
    {
        return LINE_LIMIT_STATUS_NEED_RETEST;
    }
    state->input_power_w = state->iac_theoretical_a * vac_sample;
    state->output_power_w = state->input_power_w * cfg->efficiency;
    idc = state->output_power_w / state->vdc_sample_v;

    if (state->online_channel_count == 2U)
    {
        hw_limit_a = cfg->dual_ch_max_a;
    }
    else
    {
        hw_limit_a = cfg->single_ch_max_a;
    }
    state->idc_max_limit_a = (idc < hw_limit_a) ? idc : hw_limit_a;

    /* 阶段 5：DAC 线性量化（含饱和） */
    idc = state->idc_max_limit_a;
    if (idc >= cfg->idc_fullscale_a)
    {
        state->dac_code = cfg->dac_fullscale_code;
    }
    else
    {
        state->dac_code = (uint16_t)(((idc * (float)cfg->dac_fullscale_code) /
                                      cfg->idc_fullscale_a) +
                                     0.5f);
    }

    return LINE_LIMIT_STATUS_OK;
}
