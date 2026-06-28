/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
static volatile uint8_t s_pw_level = 0U;
static volatile uint8_t s_pw_initialized = 0U;

void PW_Input_InitShadow(void)
{
  GPIO_PinState pin_state = HAL_GPIO_ReadPin(EXTI_PB0_GPIO_Port, EXTI_PB0_Pin);
  s_pw_level = (pin_state == GPIO_PIN_SET) ? 1U : 0U;
  s_pw_initialized = 1U;
}

uint8_t PW_Input_GetLevel(void)
{
  if (s_pw_initialized == 0U)
  {
    PW_Input_InitShadow();
  }

  return s_pw_level;
}

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = EXTI_PB0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;						//PSA板单独测试需要上拉，其他添加需要修改为NOPULL
  HAL_GPIO_Init(EXTI_PB0_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

}

/* USER CODE BEGIN 2 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == EXTI_PB0_Pin)
  {
    /* 当有触发信号后进入该回调函数中读取引脚状态 */
    GPIO_PinState pin_state = HAL_GPIO_ReadPin(EXTI_PB0_GPIO_Port, EXTI_PB0_Pin);
    s_pw_level = (pin_state == GPIO_PIN_SET) ? 1U : 0U;
    s_pw_initialized = 1U;
  }
}
/* USER CODE END 2 */
