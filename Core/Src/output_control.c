#include "output_control.h"
#include "dac_control.h"

void Output_Control_Init(void)
{
    Output_Control_Disable();
}

void Output_Control_Enable(void)
{
    HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_SET);
}

void Output_Control_Disable(void)
{
    HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_RESET);
    DAC_Control_UpdatePfcTargetCurrent(0.0f);
    DAC_Control_SetPfcCurrent(0.0f);
}

void Output_Control_SetCurrent(float current_A)
{
    DAC_Control_UpdatePfcTargetCurrent(current_A);
    DAC_Control_SetPfcCurrent(current_A);
}

void Output_Control_ClearFaultOutput(void)
{
    Output_Control_Disable();
}
