/**
  ******************************************************************************
  * @file           : buzzer.h
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

#ifndef BUZZER_H
#define BUZZER_H

#include "main.h"

/** @brief Configure PA8 as TIM1 channel 1 PWM, initially silent. */
HAL_StatusTypeDef Buzzer_Init(void);

/** @brief Start the configured audible PWM tone. */
HAL_StatusTypeDef Buzzer_Start(void);

/** @brief Stop PWM and leave the buzzer inactive. */
HAL_StatusTypeDef Buzzer_Stop(void);

#endif /* BUZZER_H */
