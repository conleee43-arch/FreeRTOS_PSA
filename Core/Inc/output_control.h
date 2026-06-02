#ifndef __OUTPUT_CONTROL_H__
#define __OUTPUT_CONTROL_H__

#include "main.h"

typedef enum {
    OUTPUT_CONTROL_OK = 0,
    OUTPUT_CONTROL_REJECTED = 1
} Output_Control_Status_t;

void Output_Control_Init(void);
Output_Control_Status_t Output_Control_Enable(void);
void Output_Control_Disable(void);
Output_Control_Status_t Output_Control_SetCurrent(float current_A);
void Output_Control_ClearFaultOutput(void);

#endif
