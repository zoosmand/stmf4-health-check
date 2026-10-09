/**
  ******************************************************************************
  * @file           : api_service.h
  * @brief          : HTTPS management API service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
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

#ifndef API_SERVICE_H
#define API_SERVICE_H

#include "main.h"

/**
  * @brief Create the statically allocated HTTPS management API task.
  * @retval (Platform_StatusTypeDef) PLATFORM_STATUS_OK when task creation
  *         succeeds.
  */
Platform_StatusTypeDef ApiService_Init(void);

#endif /* API_SERVICE_H */
