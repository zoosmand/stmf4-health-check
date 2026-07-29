/**
  ******************************************************************************
  * @file           : time_service.h
  * @brief          : NTP synchronization and RTC reporting service.
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

#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include "main.h"

ErrorStatus TimeService_Init(void);

#endif /* TIME_SERVICE_H */
