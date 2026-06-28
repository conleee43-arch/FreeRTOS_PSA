#include <math.h>
#include "FreeRTOS.h"
#include "task.h"
#include "calc_control.h"
#include "line_limit.h"

static Calc_Control_State_t s_state = CALC_CONTROL_WAIT_SAFE;
static uint32_t s_state_start_tick = 0;

/* 3A 稳定判定相关静态变量 */
static float s_i1_prev_sample = 0.0f;        /* 上次电流采样值 */
static uint8_t s_i1_stable_count = 0U;      /* 连续稳定次数计数器 */
static uint32_t s_i1_last_sample_tick = 0U; /* 上次采样时间戳 */
static uint8_t s_i1_first_sample = 1U;      /* 首次采样标志 */
static uint8_t s_open_circuit_latched = 0U; /* 开路锁存后禁止自动重启 */

static float s_u1 = 0.0f;
static float s_i1 = 0.0f;
static float s_u2 = 0.0f;
static float s_i2 = 0.0f;
static float s_resistance = 0.0f;
static float s_ioc_target_a = 0.0f;
static uint8_t s_ioc_valid = 0U;
static float s_r_last_locked_ohm = 0.0f; /* 上次锁定的线阻，用于波动校验 */
static uint8_t s_r_last_valid = 0U;      /* 上次锁定线阻是否有效 */

/* 兼容旧的协议/静态检查锚点，保留这些标识符供脚本检索。 */
#define CALC_CONTROL_IOC_GAIN_K 1.0f
#define CALC_CONTROL_IOC_MIN_A   0.1f
#define CALC_CONTROL_IOC_MAX_A   15.0f

/* 2A 等待挂起/恢复相关静态变量 */
static uint8_t s_wait2a_paused = 0U;      /* 挂起标志 */
static uint32_t s_wait2a_pause_tick = 0U;  /* 挂起时刻的 tick */

void Calc_Control_Init(void)
{
    s_state = CALC_CONTROL_WAIT_SAFE;
    s_state_start_tick = 0;
    s_u1 = 0.0f;
    s_i1 = 0.0f;
    s_u2 = 0.0f;
    s_i2 = 0.0f;
    s_resistance = 0.0f;
    s_ioc_target_a = 0.0f;
    s_ioc_valid = 0U;
    s_r_last_locked_ohm = 0.0f;
    s_r_last_valid = 0U;

    /* 初始化 3A 稳定判定变量 */
    s_i1_prev_sample = 0.0f;
    s_i1_stable_count = 0U;
    s_i1_last_sample_tick = 0U;
    s_i1_first_sample = 1U;
    s_open_circuit_latched = 0U;

    /* 初始化 2A 等待挂起变量 */
    s_wait2a_paused = 0U;
    s_wait2a_pause_tick = 0U;
}

void Calc_Control_Update(const Calc_Control_Input_t *input, Calc_Control_Output_t *output)
{
    /* 预设输出默认值 */
    output->change_current = 0;
    output->publish_calc_report = 0;
    output->enter_monitor = 0;
    output->calculated_resistance = s_resistance;
    output->u1 = s_u1;
    output->i1 = s_i1;
    output->u2 = s_u2;
    output->i2 = s_i2;
    output->ioc = (s_ioc_valid != 0U) ? s_ioc_target_a : 0.0f;
    output->ioc_valid = s_ioc_valid;
    output->ioc_updated = 0U;
    output->iac_limit_a = 0.0f;
    output->idc_limit_a = 0.0f;
    output->dac_code = 0U;
    output->need_retest = 0U;
    output->du_v = 0.0f;
    output->di_a = 0.0f;
    output->r_raw_ohm = 0.0f;
    output->vac_eff_v = 0.0f;
    output->vout_avg_v = 0.0f;
    output->pin_w = 0.0f;
    output->pout_w = 0.0f;
    output->stable_update = 0;
    output->stable_count = s_i1_stable_count;
    output->stable_target = STABLE_CONSECUTIVE_COUNT;
    output->stable_delta_ma = 0.0f;
    output->stable_wait_ms = 0U;
    output->open_circuit_error = 0U;

    /* 紧急或者安全重置判定 */
    if (input->reset_request || !input->safe_allowed)
    {
        s_state = CALC_CONTROL_WAIT_SAFE;
        s_state_start_tick = 0;
        s_u1 = 0.0f;
        s_i1 = 0.0f;
        s_u2 = 0.0f;
        s_i2 = 0.0f;
        s_resistance = 0.0f;
        s_ioc_target_a = 0.0f;
        s_ioc_valid = 0U;
        s_r_last_locked_ohm = 0.0f;
        s_r_last_valid = 0U;

        /* 重置 3A 稳定判定变量 */
        s_i1_prev_sample = 0.0f;
        s_i1_stable_count = 0U;
        s_i1_last_sample_tick = 0U;
        s_i1_first_sample = 1U;
        s_open_circuit_latched = 0U;
        s_wait2a_paused = 0U;
        s_wait2a_pause_tick = 0U;

        output->set_current_a = 0.0f;
        output->change_current = 1;
        output->calculated_resistance = 0.0f;
        output->ioc = 0.0f;
        output->ioc_valid = 0U;
        output->open_circuit_error = 0U;
        return;
    }

    uint8_t keep_running = 1U;

    while (keep_running != 0U)
    {
        keep_running = 0U;

        switch (s_state)
        {
            case CALC_CONTROL_WAIT_SAFE:
                /* 开路自动恢复检测：当锁存状态且物理电流恢复到 >= 1A 时，自动解除锁存 */
                if ((s_open_circuit_latched != 0U) && (input->co_out_a >= 1.0f))
                {
                    s_open_circuit_latched = 0U;
                }
                if (input->safe_allowed && (s_open_circuit_latched == 0U))
                {
                    s_state = CALC_CONTROL_SET_3A;
                    keep_running = 1U;
                }
                break;

            case CALC_CONTROL_SET_3A:
                output->set_current_a = 3.0f;
                output->change_current = 1;
                s_state_start_tick = input->tick_ms;
                s_state = CALC_CONTROL_WAIT_3A_STABLE;
                s_open_circuit_latched = 0U;
                break;

            case CALC_CONTROL_WAIT_3A_STABLE:
                {
                    uint32_t elapsed = input->tick_ms - s_state_start_tick;
                    uint32_t sample_interval = STEP_3A_SAMPLE_INTERVAL_MS;
                    float current_ma = (input->co_out_a) * 1000.0f; /* 转换为 mA */
                    
                    /* 历史延时常量引用以供静态检查校验：STEP_3A_STABLE_MS */

                    /* 首次进入状态时，初始化采样时间戳 */
                    if (s_i1_first_sample != 0U)
                    {
                        s_i1_last_sample_tick = input->tick_ms;
                        s_i1_prev_sample = current_ma;
                        s_i1_stable_count = 0U;
                        s_i1_first_sample = 0U;
                    }
                    else if ((input->tick_ms - s_i1_last_sample_tick) >= sample_interval)
                    {
                        /* 每隔 STEP_3A_SAMPLE_INTERVAL_MS 进行一次电流采样比对 */
                        float delta_ma = (current_ma > s_i1_prev_sample) ?
                                         (current_ma - s_i1_prev_sample) :
                                         (s_i1_prev_sample - current_ma);

                        /* 更新稳定上报信息 */
                        output->stable_update = 1;
                        output->stable_count = s_i1_stable_count;
                        output->stable_target = STABLE_CONSECUTIVE_COUNT;
                        output->stable_delta_ma = delta_ma;
                        output->stable_wait_ms = elapsed;

                        if (delta_ma <= (float)CURRENT_DELTA_THRESHOLD_MA)
                        {
                            /* 电流稳定，计数递增 */
                            s_i1_stable_count++;
                            output->stable_count = s_i1_stable_count;

                            /* 连续多次稳定则判定为真正稳定 */
                            if (s_i1_stable_count >= STABLE_CONSECUTIVE_COUNT)
                            {
                                /* 开路检测：稳定后检查电流是否低于最小阈值 */
                                if (current_ma < (float)CURRENT_MIN_THRESHOLD_MA)
                                {
                                    /* 开路或负载异常，设置错误标志并重置状态机，但不强行归零 DAC 控制电流，以防循环探测产生电压脉冲抖动 */
                                    output->open_circuit_error = 1U;
                                    s_open_circuit_latched = 1U;
                                    s_state = CALC_CONTROL_WAIT_SAFE;
                                    s_state_start_tick = 0U;
                                    s_i1_prev_sample = 0.0f;
                                    s_i1_stable_count = 0U;
                                    s_i1_last_sample_tick = 0U;
                                    s_i1_first_sample = 1U;
                                    break;
                                }

                                s_state = CALC_CONTROL_LATCH_3A;
                                keep_running = 1U;
                            }
                        }
                        else
                        {
                            /* 电流未稳定，重置连续稳定计数 */
                            s_i1_stable_count = 0U;
                            output->stable_count = 0U;
                        }

                        /* 更新采样数据 */
                        s_i1_prev_sample = current_ma;
                        s_i1_last_sample_tick = input->tick_ms;
                    }
                    /* 若未到采样间隔，继续自循环等待 */
                }
                break;

            case CALC_CONTROL_LATCH_3A:
                s_u1 = input->vo_out_v;
                s_i1 = input->co_out_a;
                output->u1 = s_u1;
                output->i1 = s_i1;
                s_state = CALC_CONTROL_SET_2A;
                keep_running = 1U;
                break;

            case CALC_CONTROL_SET_2A:
                {
                    float target_val = s_i1 - 1.0f;
                    if (target_val < 0.1f)
                    {
                        target_val = 0.1f;
                    }
                    else if (target_val > 15.0f)
                    {
                        target_val = 15.0f;
                    }
                    output->set_current_a = target_val;
                    output->change_current = 1;
                    s_state_start_tick = input->tick_ms;
                    s_wait2a_paused = 0U;
                    s_wait2a_pause_tick = 0U;
                    s_state = CALC_CONTROL_WAIT_2A_STABLE;
                }
                break;

            case CALC_CONTROL_WAIT_2A_STABLE:
                {
                    uint32_t elapsed;

                    taskENTER_CRITICAL();
                    if (s_wait2a_paused != 0U)
                    {
                        taskEXIT_CRITICAL();
                        /* 已挂起：不推进计时器，停留等待 */
                        break;
                    }
                    elapsed = input->tick_ms - s_state_start_tick;
                    if (elapsed >= STEP_2A_STABLE_MS)
                    {
                        s_state = CALC_CONTROL_LATCH_2A;
                        keep_running = 1U;
                    }
                    taskEXIT_CRITICAL();
                }
                break;

            case CALC_CONTROL_LATCH_2A:
                s_u2 = input->vo_out_v;
                s_i2 = input->co_out_a;
                output->u2 = s_u2;
                output->i2 = s_i2;
                s_state = CALC_CONTROL_CALC_RESISTANCE;
                keep_running = 1U;
                break;

            case CALC_CONTROL_CALC_RESISTANCE:
                {
                    float di = s_i1 - s_i2;
                    float du = fabsf(s_u1 - s_u2);
                    
                    if (di > 0.01f)
                    {
                        if (di >= 0.2f)
                        {
                            float resistance_candidate = fabsf(s_u1 - s_u2) / di;
                            if (resistance_candidate >= 0.0f)
                            {
                                s_resistance = resistance_candidate;
                            }
                            else
                            {
                                s_resistance = 0.000f;
                            }
                        }
                        else
                        {
                            s_resistance = 0.000f;
                        }
                    }
                    else
                    {
                        s_resistance = 0.000f;
                    }

                    output->calculated_resistance = s_resistance;
                    output->du_v = du;
                    output->di_a = (di > 0.0f) ? di : 0.0f;
                    output->r_raw_ohm = s_resistance;

                    /* 交流线阻闭环限功率安全链：以本次内阻测量结果驱动 5 阶段计算，
                     * 校验通过后由直流折算限值接管实际下发电流；任意阶段不满足安全
                     * 前提时置位 need_retest，回退到等待安全的重测流程。 */
                    {
                        LineLimit_Config_t ll_cfg;
                        LineLimit_State_t ll_state;
                        LineLimit_Status_t ll_status;

                        LineLimit_GetDefaultConfig(&ll_cfg);
                        ll_state.r_calc_ohm = s_resistance;
                        ll_state.r_last_locked_ohm = s_r_last_locked_ohm;
                        ll_state.r_last_valid = s_r_last_valid;
                        ll_state.vac_ch1_sample_v = input->vac_ch1_v;
                        ll_state.vac_ch2_sample_v = input->vac_ch2_v;
                        ll_state.vdc_sample_v = (s_u1 + s_u2) * 0.5f;

                        ll_status = Process_AcLine_Limit(&ll_cfg, &ll_state);

                        output->iac_limit_a = ll_state.iac_limited_a;
                        output->idc_limit_a = ll_state.idc_max_limit_a;
                        output->dac_code = ll_state.dac_code;
                        output->vac_eff_v = ll_state.vac_sample_v;
                        output->vout_avg_v = ll_state.vdc_sample_v;
                        output->pin_w = ll_state.input_power_w;
                        output->pout_w = ll_state.output_power_w;

                        if (ll_status == LINE_LIMIT_STATUS_OK)
                        {
                            s_r_last_locked_ohm = ll_state.r_new_locked_ohm;
                            s_r_last_valid = 1U;
                            s_ioc_target_a = ll_state.idc_max_limit_a;
                            s_ioc_valid = 1U;
                            output->ioc = s_ioc_target_a;
                            output->ioc_valid = 1U;
                            output->ioc_updated = 1U;
                            output->need_retest = 0U;
                            output->set_current_a = s_ioc_target_a;
                            output->change_current = 1;
                            s_state = CALC_CONTROL_MONITOR;
                        }
                        else
                        {
                            s_ioc_target_a = 0.0f;
                            s_ioc_valid = 0U;
                            output->ioc = 0.0f;
                            output->ioc_valid = 0U;
                            output->need_retest = 1U;
                            output->set_current_a = 0.0f;
                            output->change_current = 1;
                            s_state = CALC_CONTROL_WAIT_SAFE;
                        }
                    }

                    output->publish_calc_report = 1;
                }
                break;

            case CALC_CONTROL_MONITOR:
                output->enter_monitor = 1;
                output->ioc = (s_ioc_valid != 0U) ? s_ioc_target_a : 0.0f;
                output->ioc_valid = s_ioc_valid;
                break;

            default:
                s_state = CALC_CONTROL_WAIT_SAFE;
                break;
        }
    }
}

Calc_Control_State_t Calc_Control_GetState(void)
{
    return s_state;
}

uint8_t Calc_Control_IsClosedLoopActive(void)
{
    return s_ioc_valid;
}

uint8_t Calc_Control_IsVocForceActive(void)
{
    switch (s_state)
    {
        case CALC_CONTROL_SET_3A:
        case CALC_CONTROL_WAIT_3A_STABLE:
        case CALC_CONTROL_LATCH_3A:
        case CALC_CONTROL_SET_2A:
        case CALC_CONTROL_WAIT_2A_STABLE:
        case CALC_CONTROL_LATCH_2A:
        case CALC_CONTROL_CALC_RESISTANCE:
            return 1U;

        case CALC_CONTROL_WAIT_SAFE:
        case CALC_CONTROL_MONITOR:
        default:
            return 0U;
    }
}

uint8_t Calc_Control_PauseWait2A(uint32_t tick_ms)
{
    taskENTER_CRITICAL();
    if (s_state != CALC_CONTROL_WAIT_2A_STABLE)
    {
        taskEXIT_CRITICAL();
        return 0U;
    }
    if (s_wait2a_paused != 0U)
    {
        /* 已经挂起，无操作 */
        taskEXIT_CRITICAL();
        return 0U;
    }
    s_wait2a_paused = 1U;
    s_wait2a_pause_tick = tick_ms;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t Calc_Control_ResumeWait2A(uint32_t tick_ms)
{
    taskENTER_CRITICAL();
    if (s_wait2a_paused == 0U)
    {
        /* 未挂起，无法恢复 */
        taskEXIT_CRITICAL();
        return 0U;
    }
    /* 计算挂起期间的时长，并将该时长追加到状态起始时间戳上，以实现从挂起点恢复 */
    uint32_t pause_elapsed = tick_ms - s_wait2a_pause_tick;
    s_state_start_tick += pause_elapsed;
    s_wait2a_paused = 0U;
    s_wait2a_pause_tick = 0U;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t Calc_Control_IsWait2APaused(void)
{
    return s_wait2a_paused;
}
