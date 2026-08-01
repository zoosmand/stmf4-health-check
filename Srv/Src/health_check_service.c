/**
  ******************************************************************************
  * @file           : health_check_service.c
  * @brief          : Periodic HTTPS resource health-check service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 30.07.2026
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

#include "health_check_service.h"

#include "FreeRTOS.h"
#include "buzzer_service.h"
#include "health_check_config.h"
#include "health_check_log.h"
#include "lwip.h"
#include "rtc.h"
#include "task.h"
#include "tls_transport.h"

#include <stdio.h>

#define HEALTH_CHECK_TASK_STACK_DEPTH 2048U
#define HEALTH_CHECK_WAIT_MS          1000U
#define HEALTH_CHECK_OK_STATUS        200U

static StaticTask_t healthCheckTaskControlBlock;
static StackType_t healthCheckTaskStack[HEALTH_CHECK_TASK_STACK_DEPTH];

/**
  * @brief Run one check against a configured resource and record the result.
  * @param index (uint8_t) Configured resource slot.
  * @param resource (const HealthCheckConfig_ResourceTypeDef*) Occupied,
  *        enabled resource to check.
  */
static void healthCheckService_CheckResource(
  uint8_t index,
  const HealthCheckConfig_ResourceTypeDef* resource
) {
  printf(
    "HTTPS check: https://%s%s\r\n", resource->host, resource->path
  );
  TlsTransport_ResultTypeDef result;
  TlsTransport_Head(
    resource->host,
    resource->port,
    resource->path,
    resource->trustAnchorId,
    &result
  );

  if (result.status == TLS_TRANSPORT_OK) {
    printf("TLS: %s, %s, certificate valid\r\n",
      result.tlsVersion,
      result.cipherSuite);
    printf("HTTP HEAD: %u, %lu ms\r\n",
      result.httpStatus,
      (unsigned long)result.elapsedMs);
  } else {
    printf("HTTPS failure: stage=%u, detail=%d, %lu ms\r\n",
      (unsigned int)result.status,
      result.detail,
      (unsigned long)result.elapsedMs);
  }

  uint8_t resourceHealthy = ((result.status == TLS_TRANSPORT_OK)
      && (result.httpStatus == HEALTH_CHECK_OK_STATUS))
    ? 1U
    : 0U;
  printf(
    "Resource health: %s\r\n", resourceHealthy != 0U ? "OK" : "FAILED"
  );

  if ((resourceHealthy == 0U) && (BuzzerService_Alert() != SUCCESS))
    printf("Buzzer alert scheduling failed.\r\n");

  (void)HealthCheckLog_Append(index, &result);
}

static void healthCheckService_Task(void* argument) {
  (void)argument;

  for (;;) {
    if ((Lwip_IsReady() == 0U) || (Rtc_IsSynchronized() == 0U)) {
      vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_WAIT_MS));
      continue;
    }

    HealthCheckConfig_ResourceTypeDef resources[
      HEALTH_CHECK_CONFIG_MAX_RESOURCES
    ];
    HealthCheckConfig_GetResources(resources);
    for (uint8_t index = 0U; index < HEALTH_CHECK_CONFIG_MAX_RESOURCES; ++index) {
      if ((resources[index].occupied == 0U)
          || (resources[index].enabled == 0U))
        continue;
      healthCheckService_CheckResource(index, &resources[index]);
    }

    vTaskDelay(pdMS_TO_TICKS(
      HealthCheckConfig_GetPeriodSeconds() * 1000U
    ));
  }
}

ErrorStatus HealthCheckService_Init(void) {
  TaskHandle_t task = xTaskCreateStatic(
    healthCheckService_Task,
    "health-check",
    HEALTH_CHECK_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    healthCheckTaskStack,
    &healthCheckTaskControlBlock
  );
  return task != NULL ? SUCCESS : ERROR;
}
