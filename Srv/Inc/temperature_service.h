/**
  ******************************************************************************
  * @file           : temperature_service.h
  * @brief          : Periodic DS18B20 measurement service.
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

#ifndef TEMPERATURE_SERVICE_H
#define TEMPERATURE_SERVICE_H

#include "main.h"

/**
  * @brief Initialize the one-wire bus and create its measurement task.
  * @retval (ErrorStatus) SUCCESS when the service task is created.
  */
ErrorStatus TemperatureService_Init(void);

#endif /* TEMPERATURE_SERVICE_H */
