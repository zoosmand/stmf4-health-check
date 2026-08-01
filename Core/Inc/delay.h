/**
  ******************************************************************************
  * @file           : delay.h
  * @brief          : Cortex cycle-counter microsecond delay interface.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 28.07.2026
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

#ifndef DELAY_H
#define DELAY_H

#include <stdint.h>

/**
  * @brief Enable the Cortex-M4 cycle counter used for short delays.
  */
void Delay_Init(void);

/**
  * @brief Block for the requested number of microseconds.
  * @param microseconds (uint32_t) Delay duration in microseconds.
  */
void Delay_Microseconds(uint32_t microseconds);

#endif /* DELAY_H */
