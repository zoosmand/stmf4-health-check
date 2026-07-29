/**
  ******************************************************************************
  * @file           : rtos.c
  * @brief          : FreeRTOS application tasks and diagnostic hooks.
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

#include "rtos.h"

#include "FreeRTOS.h"
#include "lwip.h"
#include "main.h"
#include "task.h"
#include "temperature_service.h"
#include "time_service.h"

#define DEFAULT_TASK_STACK_DEPTH 128U
#define NETWORK_TASK_STACK_DEPTH 256U
#define DEFAULT_TASK_PERIOD_MS   1000U
#define NETWORK_TASK_PERIOD_MS   1U

static IWDG_HandleTypeDef watchdog;
static StaticTask_t defaultTaskControlBlock;
static StackType_t defaultTaskStack[DEFAULT_TASK_STACK_DEPTH];
static StaticTask_t networkTaskControlBlock;
static StackType_t networkTaskStack[NETWORK_TASK_STACK_DEPTH];

/**
  * @brief Reserved application task for future health-check coordination.
  * @param argument (void*) Unused task argument.
  */
static void rtos_DefaultTask(void* argument);

/**
  * @brief Poll the raw lwIP stack from one exclusive execution context.
  * @param argument (void*) Unused task argument.
  */
static void rtos_NetworkTask(void* argument);

Rtos_StatusTypeDef Rtos_Init(void) {
  if (TemperatureService_Init() != SUCCESS)
    return RTOS_STATUS_TASK_ERROR;
  if (TimeService_Init() != SUCCESS)
    return RTOS_STATUS_TASK_ERROR;

  TaskHandle_t taskHandle = xTaskCreateStatic(
    rtos_DefaultTask,
    "default",
    DEFAULT_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    defaultTaskStack,
    &defaultTaskControlBlock
  );
  if (taskHandle == NULL)
    return RTOS_STATUS_TASK_ERROR;

  taskHandle = xTaskCreateStatic(
    rtos_NetworkTask,
    "network",
    NETWORK_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 2U,
    networkTaskStack,
    &networkTaskControlBlock
  );
  if (taskHandle == NULL)
    return RTOS_STATUS_TASK_ERROR;

  return RTOS_STATUS_OK;
}

static void rtos_DefaultTask(void* argument) {
  (void)argument;

  watchdog.Instance = IWDG;
  watchdog.Init.Prescaler = IWDG_PRESCALER_256;
  watchdog.Init.Reload = 4095U;
  if (HAL_IWDG_Init(&watchdog) != HAL_OK)
    Error_Handler();

  for (;;) {
    if (HAL_IWDG_Refresh(&watchdog) != HAL_OK)
      Error_Handler();
    vTaskDelay(pdMS_TO_TICKS(DEFAULT_TASK_PERIOD_MS));
  }
}

static void rtos_NetworkTask(void* argument) {
  (void)argument;

  if (Lwip_Init() != LWIP_STATUS_OK)
    Error_Handler();

  for (;;) {
    Lwip_Process();
    vTaskDelay(pdMS_TO_TICKS(NETWORK_TASK_PERIOD_MS));
  }
}

void vApplicationStackOverflowHook(
  TaskHandle_t taskHandle,
  char* taskName
) {
  (void)taskHandle;
  (void)taskName;
  Error_Handler();
}
