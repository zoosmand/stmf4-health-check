/**
  ******************************************************************************
  * @file           : rs485.h
  * @brief          : Onboard RS485 interface declarations.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 29.07.2026
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

#ifndef RS485_H
#define RS485_H

#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>

/**
  * @brief Initialize USART2 and the onboard RS485 transceiver control.
  * @retval (HAL_StatusTypeDef) HAL_OK when the interface is ready.
  */
HAL_StatusTypeDef Rs485_Init(void);

/**
  * @brief Transmit one complete buffer and then release the RS485 bus.
  * @param data (const uint8_t*) Buffer to transmit; must not be null.
  * @param length (size_t) Number of bytes to transmit, up to UINT16_MAX.
  * @note Calls from tasks are serialized by a priority-inheriting mutex.
  *       Calling from interrupt context is not supported.
  * @retval (HAL_StatusTypeDef) HAL status reported by the blocking transfer.
  */
HAL_StatusTypeDef Rs485_Transmit(const uint8_t* data, size_t length);

#endif /* RS485_H */
