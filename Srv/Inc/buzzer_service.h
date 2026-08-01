/**
  ******************************************************************************
  * @file           : buzzer_service.h
  * @brief          : Non-blocking audible alert service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 01.08.2026
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

#ifndef BUZZER_SERVICE_H
#define BUZZER_SERVICE_H

#include "main.h"

/** @brief Initialize PWM, create the service task, and schedule a self-test. */
ErrorStatus BuzzerService_Init(void);

/**
  * @brief Schedule one failure alert; concurrent requests are safely coalesced.
  * @retval (ErrorStatus) SUCCESS when the service accepted the request.
  */
ErrorStatus BuzzerService_Alert(void);

#endif /* BUZZER_SERVICE_H */
