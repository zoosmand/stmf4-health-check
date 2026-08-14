/**
  ******************************************************************************
  * @file           : rs232.h
  * @brief          : Onboard RS232 interface declarations.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.08.2026
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

#ifndef RS232_H
#define RS232_H

#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>

/**
  * @brief Initialize USART1 for the onboard RS232 transceiver.
  * @retval (HAL_StatusTypeDef) HAL_OK when the interface is ready.
  */
HAL_StatusTypeDef Rs232_Init(void);

/**
  * @brief Transmit one complete buffer through the RS232 interface.
  * @param data (const uint8_t*) Buffer to transmit; must not be null.
  * @param length (size_t) Number of bytes to transmit, up to UINT16_MAX.
  * @retval (HAL_StatusTypeDef) HAL status reported by the blocking transfer.
  * @note This function blocks for at most one second per HAL transfer.
  */
HAL_StatusTypeDef Rs232_Transmit(const uint8_t* data, size_t length);

#endif /* RS232_H */
