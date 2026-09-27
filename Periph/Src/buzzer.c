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

Platform_StatusTypeDef Buzzer_Init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
  (void)RCC->APB2ENR;
  Platform_GpioConfigure(
    GPIOA, 8U, PLATFORM_GPIO_MODE_ALTERNATE, PLATFORM_GPIO_PULL_DOWN,
    PLATFORM_GPIO_SPEED_VERY_HIGH, 1U
  );
  TIM1->PSC = BUZZER_TIMER_PRESCALER;
  TIM1->ARR = BUZZER_TIMER_PERIOD;
  TIM1->CCR1 = BUZZER_TIMER_PULSE;
  TIM1->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
  TIM1->CCER = TIM_CCER_CC1E;
  TIM1->BDTR = TIM_BDTR_MOE;
  TIM1->EGR = TIM_EGR_UG;
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
