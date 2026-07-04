#include "output_protection.h"

static volatile Output_Protection_State_t s_state = OUTPUT_PROTECTION_TRIPPED;
static uint32_t s_recovery_start_tick = 0;

void Output_Protection_Init(void)
{
    s_state = OUTPUT_PROTECTION_TRIPPED;
    s_recovery_start_tick = 0;
}

void Output_Protection_Update(const Output_Protection_Input_t *input, Output_Protection_Output_t *output)
{
    output->disable_output = 0;
    output->enable_output = 0;
    output->reset_calc_control = 0;

    /* 检查是否触发越限阈值 */
    uint8_t is_fault_active = (input->temperature_c >= OTP_TRIP_THRESHOLD) ||
                              (input->vo_out_v >= OVP_TRIP_THRESHOLD) ||
                              (input->co_out_a >= OCP_TRIP_THRESHOLD);

    /* 恢复观察期仅保留温度回落判定 */
    uint8_t is_recovery_temp_safe = (input->temperature_c < OTP_RECOVERY_THRESHOLD);

    switch (s_state)
    {
        case OUTPUT_PROTECTION_NORMAL:
            if (is_fault_active)
            {
                s_state = OUTPUT_PROTECTION_TRIPPED;
                output->disable_output = 1;
                output->reset_calc_control = 1;
            }
            break;

        case OUTPUT_PROTECTION_TRIPPED:
            if (is_recovery_temp_safe)
            {
                s_state = OUTPUT_PROTECTION_RECOVERY_WAIT;
                s_recovery_start_tick = input->tick_ms;
            }
            else
            {
                /* 持续强制执行关断 */
                output->disable_output = 1;
                output->reset_calc_control = 1;
            }
            break;

        case OUTPUT_PROTECTION_RECOVERY_WAIT:
            if (is_fault_active || !is_recovery_temp_safe)
            {
                s_state = OUTPUT_PROTECTION_TRIPPED;
                output->disable_output = 1;
                output->reset_calc_control = 1;
            }
            else if ((input->tick_ms - s_recovery_start_tick) >= RECOVERY_OBSERVE_MS)
            {
                s_state = OUTPUT_PROTECTION_NORMAL;
                output->enable_output = 1;
            }
            break;

        default:
            s_state = OUTPUT_PROTECTION_NORMAL;
            break;
    }
}

Output_Protection_State_t Output_Protection_GetState(void)
{
    return s_state;
}
