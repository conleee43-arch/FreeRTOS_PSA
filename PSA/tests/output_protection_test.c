#include <assert.h>
#include <stdint.h>

#include "output_protection.h"

static void test_overtemperature_hysteresis(void)
{
    OutputProtectionState_t state;

    OutputProtection_Init(&state);
    assert(OutputProtection_Update(&state, 84.9f, 440.0f, 10.0f) == OUTPUT_PROTECTION_FAULT_NONE);
    assert(OutputProtection_ShouldForceOff(&state) == 0U);

    assert(OutputProtection_Update(&state, 85.1f, 440.0f, 10.0f) == OUTPUT_PROTECTION_FAULT_OVERTEMP);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    assert(OutputProtection_Update(&state, 80.0f, 440.0f, 10.0f) == OUTPUT_PROTECTION_FAULT_OVERTEMP);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    assert(OutputProtection_Update(&state, 74.9f, 440.0f, 10.0f) == OUTPUT_PROTECTION_FAULT_NONE);
    assert(OutputProtection_ShouldForceOff(&state) == 0U);
}

static void test_overvoltage_hysteresis(void)
{
    OutputProtectionState_t state;

    OutputProtection_Init(&state);
    assert(OutputProtection_Update(&state, 30.0f, 449.9f, 10.0f) == OUTPUT_PROTECTION_FAULT_NONE);

    assert(OutputProtection_Update(&state, 30.0f, 450.1f, 10.0f) == OUTPUT_PROTECTION_FAULT_OVERVOLT);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    assert(OutputProtection_Update(&state, 30.0f, 445.0f, 10.0f) == OUTPUT_PROTECTION_FAULT_OVERVOLT);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    assert(OutputProtection_Update(&state, 30.0f, 444.9f, 10.0f) == OUTPUT_PROTECTION_FAULT_NONE);
    assert(OutputProtection_ShouldForceOff(&state) == 0U);
}

static void test_overcurrent_latches_until_restart(void)
{
    OutputProtectionState_t state;

    OutputProtection_Init(&state);
    assert(OutputProtection_Update(&state, 30.0f, 440.0f, 14.9f) == OUTPUT_PROTECTION_FAULT_NONE);

    assert(OutputProtection_Update(&state, 30.0f, 440.0f, 15.1f) == OUTPUT_PROTECTION_FAULT_OVERCURRENT);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    assert(OutputProtection_Update(&state, 30.0f, 440.0f, 0.0f) == OUTPUT_PROTECTION_FAULT_OVERCURRENT);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);

    OutputProtection_Init(&state);
    assert(OutputProtection_Update(&state, 30.0f, 440.0f, 0.0f) == OUTPUT_PROTECTION_FAULT_NONE);
    assert(OutputProtection_ShouldForceOff(&state) == 0U);
}

static void test_faults_can_stack(void)
{
    OutputProtectionState_t state;
    uint32_t mask;

    OutputProtection_Init(&state);
    mask = OutputProtection_Update(&state, 90.0f, 451.0f, 16.0f);

    assert((mask & OUTPUT_PROTECTION_FAULT_OVERTEMP) != 0U);
    assert((mask & OUTPUT_PROTECTION_FAULT_OVERVOLT) != 0U);
    assert((mask & OUTPUT_PROTECTION_FAULT_OVERCURRENT) != 0U);
    assert(OutputProtection_ShouldForceOff(&state) != 0U);
}

int main(void)
{
    test_overtemperature_hysteresis();
    test_overvoltage_hysteresis();
    test_overcurrent_latches_until_restart();
    test_faults_can_stack();
    return 0;
}
