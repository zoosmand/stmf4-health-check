/**
  ******************************************************************************
  * @file           : factory_reset_service.h
  * @brief          : Recoverable physical-button factory reset service.
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

#ifndef FACTORY_RESET_SERVICE_H
#define FACTORY_RESET_SERVICE_H

#include "main.h"

/** @brief Finish an interrupted reset before persistent stores are opened. */
Platform_StatusTypeDef FactoryResetService_Recover(void);

/** @brief Configure S1 and create the factory-reset monitoring task. */
ErrorStatus FactoryResetService_Init(void);

#endif /* FACTORY_RESET_SERVICE_H */
