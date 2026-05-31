#include "output_protection.h"

#include <stddef.h>

void OutputProtection_Init(OutputProtectionState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->overtemp_active = 0U;
    state->overvoltage_active = 0U;
    state->overcurrent_latched = 0U;
}

uint32_t OutputProtection_Update(OutputProtectionState_t *state, float temp_c, float vo_out_v, float co_out_a)
{
    if (state == NULL)
    {
        return OUTPUT_PROTECTION_FAULT_NONE;
    }

    if (state->overtemp_active == 0U)
    {
        if (temp_c > OUTPUT_PROTECTION_OVERTEMP_TRIP_C)
        {
            state->overtemp_active = 1U;
        }
    }
    else if (temp_c < OUTPUT_PROTECTION_OVERTEMP_CLEAR_C)
    {
        state->overtemp_active = 0U;
    }

    if (state->overvoltage_active == 0U)
    {
        if (vo_out_v > OUTPUT_PROTECTION_OVERVOLT_TRIP_V)
        {
            state->overvoltage_active = 1U;
        }
    }
    else if (vo_out_v < OUTPUT_PROTECTION_OVERVOLT_CLEAR_V)
    {
        state->overvoltage_active = 0U;
    }

    if ((state->overcurrent_latched == 0U) && (co_out_a > OUTPUT_PROTECTION_OVERCURRENT_TRIP_A))
    {
        state->overcurrent_latched = 1U;
    }

    return OutputProtection_GetFaultMask(state);
}

uint32_t OutputProtection_GetFaultMask(const OutputProtectionState_t *state)
{
    uint32_t fault_mask = OUTPUT_PROTECTION_FAULT_NONE;

    if (state == NULL)
    {
        return OUTPUT_PROTECTION_FAULT_NONE;
    }

    if (state->overtemp_active != 0U)
    {
        fault_mask |= OUTPUT_PROTECTION_FAULT_OVERTEMP;
    }
    if (state->overvoltage_active != 0U)
    {
        fault_mask |= OUTPUT_PROTECTION_FAULT_OVERVOLT;
    }
    if (state->overcurrent_latched != 0U)
    {
        fault_mask |= OUTPUT_PROTECTION_FAULT_OVERCURRENT;
    }

    return fault_mask;
}

uint8_t OutputProtection_ShouldForceOff(const OutputProtectionState_t *state)
{
    return (OutputProtection_GetFaultMask(state) != OUTPUT_PROTECTION_FAULT_NONE) ? 1U : 0U;
}
