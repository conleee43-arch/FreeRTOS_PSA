#ifndef __OUTPUT_CONTROL_H__
#define __OUTPUT_CONTROL_H__

#include "main.h"

void Output_Control_Init(void);
void Output_Control_Enable(void);
void Output_Control_Disable(void);
void Output_Control_SetCurrent(float current_A);
void Output_Control_ClearFaultOutput(void);

#endif
