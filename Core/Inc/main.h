/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Common application declarations.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.01.2026
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

#ifndef MAIN_H
#define MAIN_H

#include "stm32f4xx_hal.h"

#define FLASH_CS_PIN       GPIO_PIN_3
#define FLASH_CS_GPIO_PORT GPIOE

extern SPI_HandleTypeDef hspi2;

void Error_Handler(void);

#endif /* MAIN_H */
