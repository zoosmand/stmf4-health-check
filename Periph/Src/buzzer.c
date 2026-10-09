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

#define BUZZER_TIMER_PRESCALER       167U
#define BUZZER_TIMER_COUNTER_HZ      1000000UL
#define BUZZER_DEFAULT_FREQUENCY_HZ  2500U
#define BUZZER_MIN_FREQUENCY_HZ      500U
#define BUZZER_MAX_FREQUENCY_HZ      5000U

Platform_StatusTypeDef Buzzer_SetFrequency(uint16_t frequencyHz) {
  if ((frequencyHz < BUZZER_MIN_FREQUENCY_HZ)
      || (frequencyHz > BUZZER_MAX_FREQUENCY_HZ)) {
    return PLATFORM_STATUS_ERROR;
  }
  uint32_t period = (BUZZER_TIMER_COUNTER_HZ / frequencyHz) - 1U;
  TIM1->ARR = period;
  TIM1->CCR1 = (period + 1U) / 2U;
  TIM1->EGR = TIM_EGR_UG;
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Buzzer_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
  (void)RCC->APB2ENR;
  Platform_GpioConfigure(
    GPIOA, 8U, PLATFORM_GPIO_MODE_ALTERNATE, PLATFORM_GPIO_PULL_DOWN,
    PLATFORM_GPIO_SPEED_VERY_HIGH, 1U
  );
  TIM1->PSC = BUZZER_TIMER_PRESCALER;
  if (Buzzer_SetFrequency(BUZZER_DEFAULT_FREQUENCY_HZ)
      != PLATFORM_STATUS_OK) {
    return PLATFORM_STATUS_ERROR;
  }
  TIM1->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->BDTR = TIM_BDTR_MOE;
  return Buzzer_Stop();
}

Platform_StatusTypeDef Buzzer_Start(void) {
  TIM1->CCER |= TIM_CCER_CC1E;
  TIM1->BDTR |= TIM_BDTR_MOE;
  TIM1->CR1 |= TIM_CR1_CEN;
  return PLATFORM_STATUS_OK;
}

Platform_StatusTypeDef Buzzer_Stop(void) {
  TIM1->CR1 &= ~TIM_CR1_CEN;
  TIM1->CCER &= ~TIM_CCER_CC1E;
  TIM1->BDTR &= ~TIM_BDTR_MOE;
  TIM1->CNT = 0U;
  return PLATFORM_STATUS_OK;
}
