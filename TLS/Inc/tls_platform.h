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

HAL_StatusTypeDef TlsPlatform_Init(void);

#endif /* TLS_PLATFORM_H */
