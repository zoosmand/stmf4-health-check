/**
  ******************************************************************************
  * @file           : heartbeat_service.h
  * @brief          : Human-like status LED heartbeat service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 25.09.2026
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

#ifndef HEARTBEAT_SERVICE_H
#define HEARTBEAT_SERVICE_H

#include "main.h"

/** @brief Configure the status LED and create its heartbeat task. */
ErrorStatus HeartbeatService_Init(void);

#endif /* HEARTBEAT_SERVICE_H */
