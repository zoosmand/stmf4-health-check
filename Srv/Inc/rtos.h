/**
  ******************************************************************************
  * @file           : rtos.h
  * @brief          : FreeRTOS application task initialization.
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

#ifndef RTOS_H
#define RTOS_H

/**
  * @brief Result of creating the statically allocated application tasks.
  */
typedef enum {
  RTOS_STATUS_OK = 0,
  RTOS_STATUS_TASK_ERROR
} Rtos_StatusTypeDef;

/**
  * @brief Create the default and Ethernet service tasks.
  * @retval (Rtos_StatusTypeDef) RTOS_STATUS_OK when every task was created.
  */
Rtos_StatusTypeDef Rtos_Init(void);

#endif /* RTOS_H */
