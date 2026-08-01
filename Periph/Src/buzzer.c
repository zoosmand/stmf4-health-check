/**
  ******************************************************************************
  * @file           : buzzer.c
  * @brief          : Passive buzzer PWM driver.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 01.08.2026
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017-2026 Dmitry Slobodchikov
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "buzzer.h"

#define BUZZER_TIMER_PRESCALER 167U
#define BUZZER_TIMER_PERIOD    399U
#define BUZZER_TIMER_PULSE     200U

static TIM_HandleTypeDef buzzerTimer;

HAL_StatusTypeDef Buzzer_Init(void) {
  buzzerTimer.Instance = TIM1;
  buzzerTimer.Init.Prescaler = BUZZER_TIMER_PRESCALER;
  buzzerTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
  buzzerTimer.Init.Period = BUZZER_TIMER_PERIOD;
  buzzerTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  buzzerTimer.Init.RepetitionCounter = 0U;
  buzzerTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&buzzerTimer) != HAL_OK)
    return HAL_ERROR;

  TIM_OC_InitTypeDef channel = {
    .OCMode = TIM_OCMODE_PWM1,
    .Pulse = BUZZER_TIMER_PULSE,
    .OCPolarity = TIM_OCPOLARITY_HIGH,
    .OCNPolarity = TIM_OCNPOLARITY_HIGH,
    .OCFastMode = TIM_OCFAST_DISABLE,
    .OCIdleState = TIM_OCIDLESTATE_RESET,
    .OCNIdleState = TIM_OCNIDLESTATE_RESET,
  };
  if (HAL_TIM_PWM_ConfigChannel(
        &buzzerTimer, &channel, TIM_CHANNEL_1
      ) != HAL_OK) {
    return HAL_ERROR;
  }
  return Buzzer_Stop();
}

HAL_StatusTypeDef Buzzer_Start(void) {
  return HAL_TIM_PWM_Start(&buzzerTimer, TIM_CHANNEL_1);
}

HAL_StatusTypeDef Buzzer_Stop(void) {
  return HAL_TIM_PWM_Stop(&buzzerTimer, TIM_CHANNEL_1);
}
