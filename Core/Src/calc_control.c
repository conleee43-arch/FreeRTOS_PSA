#include "calc_control.h"

static Calc_Control_State_t s_state = CALC_CONTROL_WAIT_SAFE;
static uint32_t s_state_start_tick = 0;

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

        output->set_current_a = 0.0f;
        output->change_current = 1;
        output->calculated_resistance = 0.0f;
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
                if ((input->tick_ms - s_state_start_tick) >= STEP_3A_STABLE_MS)
                {
                    s_state = CALC_CONTROL_LATCH_3A;
                    keep_running = 1U;
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
