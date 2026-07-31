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
#include "w25q64.h"

#define DEFAULT_TASK_STACK_DEPTH 128U
#define NETWORK_TASK_STACK_DEPTH 1024U
#define STARTUP_TASK_STACK_DEPTH 512U
#define DEFAULT_TASK_PERIOD_MS   1000U
#define NETWORK_TASK_PERIOD_MS   1U

static IWDG_HandleTypeDef watchdog;
static StaticTask_t defaultTaskControlBlock;
static StackType_t defaultTaskStack[DEFAULT_TASK_STACK_DEPTH];
static StaticTask_t networkTaskControlBlock;
static StackType_t networkTaskStack[NETWORK_TASK_STACK_DEPTH];
static StaticTask_t startupTaskControlBlock;
static StackType_t startupTaskStack[STARTUP_TASK_STACK_DEPTH];

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
static void rtos_StartupTask(void* argument);

Rtos_StatusTypeDef Rtos_Init(void) {
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

  printf("RTOS init: startup task.\r\n");
  taskHandle = xTaskCreateStatic(
    rtos_StartupTask,
    "startup",
    STARTUP_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 3U,
    startupTaskStack,
    &startupTaskControlBlock
  );
  if (taskHandle == NULL)
    return RTOS_STATUS_TASK_ERROR;

  return RTOS_STATUS_OK;
}

static void rtos_StartupTask(void* argument) {
  (void)argument;

  if (W25Q64_Init() != HAL_OK)
    Error_Handler();
  printf("W25Q64 flash ready.\r\n");
  if ((HealthCheckConfig_Init() != HAL_OK)
      || (HealthCheckLog_Init() != HAL_OK)
      || (TlsServerCredentials_Init() != HAL_OK)) {
    Error_Handler();
  }

  if ((ApiService_Init() != HAL_OK)
      || (TemperatureService_Init() != SUCCESS)
      || (TimeService_Init() != SUCCESS)
      || (HealthCheckService_Init() != SUCCESS)) {
    Error_Handler();
  }

  TaskHandle_t networkTask = xTaskCreateStatic(
    rtos_NetworkTask,
    "network",
    NETWORK_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 2U,
    networkTaskStack,
    &networkTaskControlBlock
  );
  if (networkTask == NULL)
    Error_Handler();
  printf("Startup task: services ready.\r\n");

  for (;;)
    vTaskDelay(pdMS_TO_TICKS(1000U));
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
