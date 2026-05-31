#ifndef __OUTPUT_PROTECTION_H__
#define __OUTPUT_PROTECTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define OUTPUT_PROTECTION_OVERTEMP_TRIP_C       85.0f
#define OUTPUT_PROTECTION_OVERTEMP_CLEAR_C      75.0f
#define OUTPUT_PROTECTION_OVERVOLT_TRIP_V       450.0f
#define OUTPUT_PROTECTION_OVERVOLT_CLEAR_V      445.0f
#define OUTPUT_PROTECTION_OVERCURRENT_TRIP_A    15.0f

typedef enum
{
    OUTPUT_PROTECTION_FAULT_NONE        = 0U,
    OUTPUT_PROTECTION_FAULT_OVERTEMP    = (1U << 0),
    OUTPUT_PROTECTION_FAULT_OVERVOLT    = (1U << 1),
    OUTPUT_PROTECTION_FAULT_OVERCURRENT = (1U << 2)
} OutputProtectionFault_t;

typedef struct
{
    uint8_t overtemp_active;
    uint8_t overvoltage_active;
    uint8_t overcurrent_latched;
} OutputProtectionState_t;

void OutputProtection_Init(OutputProtectionState_t *state);
uint32_t OutputProtection_Update(OutputProtectionState_t *state, float temp_c, float vo_out_v, float co_out_a);
uint32_t OutputProtection_GetFaultMask(const OutputProtectionState_t *state);
uint8_t OutputProtection_ShouldForceOff(const OutputProtectionState_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __OUTPUT_PROTECTION_H__ */
