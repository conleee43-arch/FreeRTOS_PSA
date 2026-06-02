#include "output_control.h"
#include "dac_control.h"
#include "output_protection.h"
#include "FreeRTOS.h"
#include "task.h"

void Output_Control_Init(void)
{
    Output_Control_Disable();
}

Output_Control_Status_t Output_Control_Enable(void)
{
    taskENTER_CRITICAL();
    if (Output_Protection_GetState() != OUTPUT_PROTECTION_NORMAL)
    {
        taskEXIT_CRITICAL();
        return OUTPUT_CONTROL_REJECTED;
    }

    HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_SET);
    taskEXIT_CRITICAL();
    return OUTPUT_CONTROL_OK;
}

void Output_Control_Disable(void)
{
    HAL_GPIO_WritePin(OF_EN_GPIO_Port, OF_EN_Pin, GPIO_PIN_RESET);
    DAC_Control_UpdatePfcTargetCurrent(0.0f);
    DAC_Control_SetPfcCurrent(0.0f);
}

Output_Control_Status_t Output_Control_SetCurrent(float current_A)
{
    taskENTER_CRITICAL();
    if ((Output_Protection_GetState() != OUTPUT_PROTECTION_NORMAL) && (current_A != 0.0f))
    {
        taskEXIT_CRITICAL();
        return OUTPUT_CONTROL_REJECTED;
    }

    float clamped_val = current_A;
    if (!(current_A >= 0.0f && current_A <= 10.0f))
    {
        if (current_A < 0.0f)
        {
            clamped_val = 0.0f;
        }
        else if (current_A > 10.0f)
        {
            clamped_val = 10.0f;
        }
        else
        {
            clamped_val = 0.0f;
        }
    }
    DAC_Control_UpdatePfcTargetCurrent(clamped_val);
    DAC_Control_SetPfcCurrent(clamped_val);
    taskEXIT_CRITICAL();
    return OUTPUT_CONTROL_OK;
}

void Output_Control_ClearFaultOutput(void)
{
    Output_Control_Disable();
}
