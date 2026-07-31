/**
  ******************************************************************************
  * @file           : tls_platform.h
  * @brief          : STM32 platform services used by Mbed TLS.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 30.07.2026
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

#ifndef TLS_PLATFORM_H
#define TLS_PLATFORM_H

#include "main.h"

/**
  * @brief Initialize entropy, Mbed TLS memory, and crypto serialization.
  * @retval (HAL_StatusTypeDef) HAL_OK when every platform facility is ready.
  */
HAL_StatusTypeDef TlsPlatform_Init(void);

/**
  * @brief Obtain exclusive access to the shared Mbed TLS platform state.
  * @retval (HAL_StatusTypeDef) HAL_OK when the caller owns the crypto lock.
  * @note The caller must be a FreeRTOS task and must call TlsPlatform_Unlock().
  */
HAL_StatusTypeDef TlsPlatform_Lock(void);

/**
  * @brief Release exclusive access to the shared Mbed TLS platform state.
  * @note The caller must own the crypto lock.
  */
void TlsPlatform_Unlock(void);

/**
  * @brief Fill a buffer from the STM32 hardware random-number generator.
  * @param output (uint8_t*) Non-null output buffer.
  * @param length (size_t) Number of random bytes requested.
  * @retval (HAL_StatusTypeDef) HAL_OK when the complete buffer was filled.
  */
HAL_StatusTypeDef TlsPlatform_Random(uint8_t* output, size_t length);

#endif /* TLS_PLATFORM_H */
