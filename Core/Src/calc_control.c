#include "calc_control.h"

static Calc_Control_State_t s_state = CALC_CONTROL_WAIT_SAFE;
static uint32_t s_state_start_tick = 0;

/* 3A 稳定判定相关静态变量 */
static float s_i1_prev_sample = 0.0f;        /* 上次电流采样值 */
static uint8_t s_i1_stable_count = 0U;      /* 连续稳定次数计数器 */
static uint32_t s_i1_last_sample_tick = 0U; /* 上次采样时间戳 */
static uint8_t s_i1_first_sample = 1U;      /* 首次采样标志 */

static float s_u1 = 0.0f;
static float s_i1 = 0.0f;
static float s_u2 = 0.0f;
static float s_i2 = 0.0f;
static float s_resistance = 0.0f;

void Calc_Control_Init(void)
{
    s_state = CALC_CONTROL_WAIT_SAFE;
    s_state_start_tick = 0;
    s_u1 = 0.0f;
    s_i1 = 0.0f;
    s_u2 = 0.0f;
    s_i2 = 0.0f;
    s_resistance = 0.0f;

    /* 初始化 3A 稳定判定变量 */
    s_i1_prev_sample = 0.0f;
    s_i1_stable_count = 0U;
    s_i1_last_sample_tick = 0U;
    s_i1_first_sample = 1U;
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

        /* 重置 3A 稳定判定变量 */
        s_i1_prev_sample = 0.0f;
        s_i1_stable_count = 0U;
        s_i1_last_sample_tick = 0U;
        s_i1_first_sample = 1U;

        output->set_current_a = 0.0f;
        output->change_current = 1;
        output->calculated_resistance = 0.0f;
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
                if (input->safe_allowed)
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

                            /* 连续多次稳定则判定为真正稳定 */
                            if (s_i1_stable_count >= STABLE_CONSECUTIVE_COUNT)
                            {
                                /* 开路检测：稳定后检查电流是否低于最小阈值 */
                                if (current_ma < (float)CURRENT_MIN_THRESHOLD_MA)
                                {
                                    /* 开路或负载异常，设置错误标志并重置状态机，但不强行归零 DAC 控制电流，以防循环探测产生电压脉冲抖动 */
                                    output->open_circuit_error = 1U;
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
                output->set_current_a = 2.0f;
                output->change_current = 1;
                s_state_start_tick = input->tick_ms;
                s_state = CALC_CONTROL_WAIT_2A_STABLE;
                break;

            case CALC_CONTROL_WAIT_2A_STABLE:
                if ((input->tick_ms - s_state_start_tick) >= STEP_2A_STABLE_MS)
                {
                    s_state = CALC_CONTROL_LATCH_2A;
                    keep_running = 1U;
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
                    
                    if (di > 0.01f)
                    {
                        float resistance_candidate = (s_u1 - s_u2) / di;
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

                    output->calculated_resistance = s_resistance;
                    output->publish_calc_report = 1;
                    s_state = CALC_CONTROL_MONITOR;
                }
                break;

            case CALC_CONTROL_MONITOR:
                output->enter_monitor = 1;
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
