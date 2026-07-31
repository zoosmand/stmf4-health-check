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
#include "api_service.h"
#include "health_check_config.h"
#include "health_check_log.h"
#include "health_check_service.h"
#include "lwip.h"
#include "main.h"
#include "task.h"
#include "temperature_service.h"
#include "time_service.h"
#include "tls_platform.h"
#include "tls_server_credentials.h"

#define DEFAULT_TASK_STACK_DEPTH 128U
#define NETWORK_TASK_STACK_DEPTH 1024U
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
  printf("RTOS init: health-check config store.\r\n");
  if (HealthCheckConfig_Init() != HAL_OK)
    return RTOS_STATUS_TASK_ERROR;
  printf("RTOS init: health-check log store.\r\n");
  if (HealthCheckLog_Init() != HAL_OK)
    return RTOS_STATUS_TASK_ERROR;
  printf("RTOS init: TLS server credential store.\r\n");
  if (TlsServerCredentials_Init() != HAL_OK)
    return RTOS_STATUS_TASK_ERROR;

  printf("RTOS init: API task.\r\n");
  if (ApiService_Init() != HAL_OK)
    return RTOS_STATUS_TASK_ERROR;
  printf("RTOS init: temperature task.\r\n");
  if (TemperatureService_Init() != SUCCESS)
    return RTOS_STATUS_TASK_ERROR;
  printf("RTOS init: time task.\r\n");
  if (TimeService_Init() != SUCCESS)
    return RTOS_STATUS_TASK_ERROR;
  printf("RTOS init: health-check task.\r\n");
  if (HealthCheckService_Init() != SUCCESS)
    return RTOS_STATUS_TASK_ERROR;

  printf("RTOS init: default task.\r\n");
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

  printf("RTOS init: network task.\r\n");
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

  printf("Default task: started.\r\n");
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

  printf("Network task: started.\r\n");
  if (TlsPlatform_Init() != HAL_OK)
    Error_Handler();
  printf("Network task: TLS platform ready.\r\n");
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
  printf(
    "FreeRTOS stack overflow: %s\r\n",
    taskName != NULL ? taskName : "unknown"
  );
  Error_Handler();
}
